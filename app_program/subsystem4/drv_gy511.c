/*
 * drv_gy511.c
 * Subsystem 4: GY-511 (LSM303DLHC) Accelerometer & Terrain Sensing Driver
 *
 * Implements I2C open-drain communication on GP2 (SDA) and GP3 (SCL)
 * and provides filtered acceleration and integer-based pitch calculations.
 *
 * Conforms to Barr-C Coding Standard and micro T-Kernel 3.0 conventions.
 */

#include "drv_gy511.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

/* I2C timing delay in microseconds (5 us gives ~100 kHz standard mode) */
#define I2C_HALF_CLOCK_US           (5U)

/* Filter history buffer */
static GY511_ACCEL_DATA_T s_filter_buf[GY511_FILTER_WINDOW_SIZE];
static uint8_t s_filter_idx = 0U;
static uint8_t s_filter_count = 0U;

/* -------------------------------------------------------------------------
 * Low-Level I2C Bit-Bang Implementation
 * Uses open-drain behavior:
 *   HIGH: Set GPIO as input with pull-up (line floats high)
 *   LOW : Set GPIO as output driving logic 0
 * ------------------------------------------------------------------------- */

static void i2c_delay(void)
{
    WaitUsec(I2C_HALF_CLOCK_US);
}

static void i2c_sda_high(void)
{
    (void)gpio_set_pin(GY511_PIN_SDA, GPIO_MODE_IN);
}

static void i2c_sda_low(void)
{
    (void)gpio_set_pin(GY511_PIN_SDA, GPIO_MODE_OUT);
    (void)gpio_set_val(GY511_PIN_SDA, 0U);
}

static void i2c_scl_high(void)
{
    uint32_t timeout = 1000U;
    (void)gpio_set_pin(GY511_PIN_SCL, GPIO_MODE_IN);
    /* Clock stretching check */
    while ((gpio_get_val(GY511_PIN_SCL) == 0U) && (timeout > 0U)) {
        WaitUsec(1U);
        timeout--;
    }
}

static void i2c_scl_low(void)
{
    (void)gpio_set_pin(GY511_PIN_SCL, GPIO_MODE_OUT);
    (void)gpio_set_val(GY511_PIN_SCL, 0U);
}

static uint8_t i2c_sda_read(void)
{
    return (uint8_t)(gpio_get_val(GY511_PIN_SDA) != 0U ? 1U : 0U);
}

static void i2c_start(void)
{
    i2c_sda_high();
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_sda_low();
    i2c_delay();
    i2c_scl_low();
    i2c_delay();
}

static void i2c_stop(void)
{
    i2c_sda_low();
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_sda_high();
    i2c_delay();
}

static ER i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0U; i < 8U; i++) {
        if ((byte & 0x80U) != 0U) {
            i2c_sda_high();
        } else {
            i2c_sda_low();
        }
        byte <<= 1;
        i2c_delay();
        i2c_scl_high();
        i2c_delay();
        i2c_scl_low();
    }

    /* Release SDA for ACK */
    i2c_sda_high();
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    ack = i2c_sda_read();
    i2c_scl_low();
    i2c_delay();

    return (ack == 0U) ? E_OK : E_IO;
}

static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    i2c_sda_high();
    for (i = 0U; i < 8U; i++) {
        byte <<= 1;
        i2c_delay();
        i2c_scl_high();
        i2c_delay();
        if (i2c_sda_read() != 0U) {
            byte |= 0x01U;
        }
        i2c_scl_low();
    }

    /* Send ACK or NACK */
    if (ack != 0U) {
        i2c_sda_low();   /* ACK */
    } else {
        i2c_sda_high();  /* NACK */
    }
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_scl_low();
    i2c_sda_high();
    i2c_delay();

    return byte;
}

/* -------------------------------------------------------------------------
 * Low-Level Register Read/Write Helpers
 * ------------------------------------------------------------------------- */

static ER lsm303_write_reg(uint8_t reg, uint8_t val)
{
    ER er;

    i2c_start();
    er = i2c_write_byte((uint8_t)(GY511_I2C_ADDR_ACCEL << 1));
    if (er == E_OK) {
        er = i2c_write_byte(reg);
    }
    if (er == E_OK) {
        er = i2c_write_byte(val);
    }
    i2c_stop();

    return er;
}

static ER lsm303_read_bytes(uint8_t reg, uint8_t *p_buf, uint8_t len)
{
    ER er;
    uint8_t i;

    if ((p_buf == NULL) || (len == 0U)) {
        return E_PAR;
    }

    i2c_start();
    er = i2c_write_byte((uint8_t)(GY511_I2C_ADDR_ACCEL << 1));
    if (er != E_OK) {
        i2c_stop();
        return er;
    }

    /* Sub-address: set MSB for multi-byte auto-increment if len > 1 */
    er = i2c_write_byte((uint8_t)(len > 1U ? (reg | LSM303_AUTO_INCREMENT) : reg));
    if (er != E_OK) {
        i2c_stop();
        return er;
    }

    /* Repeated start */
    i2c_start();
    er = i2c_write_byte((uint8_t)((GY511_I2C_ADDR_ACCEL << 1) | 0x01U));
    if (er != E_OK) {
        i2c_stop();
        return er;
    }

    for (i = 0U; i < len; i++) {
        p_buf[i] = i2c_read_byte((uint8_t)(i < (len - 1U) ? 1U : 0U));
    }
    i2c_stop();

    return E_OK;
}

/* -------------------------------------------------------------------------
 * Fixed-Point Math Helpers (No libm or FPU dependency)
 * ------------------------------------------------------------------------- */

static uint32_t int_sqrt32(uint32_t val)
{
    uint32_t res = 0U;
    uint32_t bit = 1UL << 30;

    while (bit > val) {
        bit >>= 2;
    }
    while (bit != 0U) {
        if (val >= (res + bit)) {
            val -= (res + bit);
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* -------------------------------------------------------------------------
 * Driver Public API
 * ------------------------------------------------------------------------- */

EXPORT ER drv_gy511_init(void)
{
    ER er;

    /* Set up SDA and SCL as high-impedance inputs initially */
    i2c_sda_high();
    i2c_scl_high();
    WaitUsec(1000U);

    /* Verify sensor presence */
    er = drv_gy511_probe();
    if (er != E_OK) {
        return er;
    }

    /* Configure CTRL_REG1_A: 100 Hz output data rate, normal power, enable X, Y, Z */
    er = lsm303_write_reg(LSM303_CTRL_REG1_A, LSM303_ODR_100HZ_XYZ_EN);
    if (er != E_OK) {
        return er;
    }

    /* Configure CTRL_REG4_A: +/- 2g scale, High Resolution (HR = 1, 1 mg/LSB) */
    er = lsm303_write_reg(LSM303_CTRL_REG4_A, LSM303_SCALE_2G_HR);
    if (er != E_OK) {
        return er;
    }

    /* Reset filter buffer */
    s_filter_idx = 0U;
    s_filter_count = 0U;

    return E_OK;
}

EXPORT ER drv_gy511_probe(void)
{
    ER er;

    i2c_start();
    /* Accelerometer write address */
    er = i2c_write_byte((uint8_t)(GY511_I2C_ADDR_ACCEL << 1));
    i2c_stop();

    return er;
}

EXPORT ER drv_gy511_read_accel(GY511_ACCEL_DATA_T *p_accel)
{
    uint8_t raw_buf[6];
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;
    int32_t sum_x = 0;
    int32_t sum_y = 0;
    int32_t sum_z = 0;
    uint8_t i;
    ER er;

    if (p_accel == NULL) {
        return E_PAR;
    }

    /* Burst read 6 acceleration registers starting at OUT_X_L_A */
    er = lsm303_read_bytes(LSM303_OUT_X_L_A, raw_buf, 6U);
    if (er != E_OK) {
        return er;
    }

    /*
     * LSM303DLHC output format: 12-bit left-aligned within 16-bit word.
     * Shift right by 4 bits to get 12-bit signed acceleration.
     * In +/- 2g HR mode, sensitivity is 1 mg/LSB.
     */
    raw_x = (int16_t)(((uint16_t)raw_buf[1] << 8) | (uint16_t)raw_buf[0]);
    raw_y = (int16_t)(((uint16_t)raw_buf[3] << 8) | (uint16_t)raw_buf[2]);
    raw_z = (int16_t)(((uint16_t)raw_buf[5] << 8) | (uint16_t)raw_buf[4]);

    /* Update moving average window */
    s_filter_buf[s_filter_idx].x_mg = (int16_t)(raw_x >> 4);
    s_filter_buf[s_filter_idx].y_mg = (int16_t)(raw_y >> 4);
    s_filter_buf[s_filter_idx].z_mg = (int16_t)(raw_z >> 4);

    s_filter_idx = (uint8_t)((s_filter_idx + 1U) % GY511_FILTER_WINDOW_SIZE);
    if (s_filter_count < GY511_FILTER_WINDOW_SIZE) {
        s_filter_count++;
    }

    /* Calculate smoothed average */
    for (i = 0U; i < s_filter_count; i++) {
        sum_x += s_filter_buf[i].x_mg;
        sum_y += s_filter_buf[i].y_mg;
        sum_z += s_filter_buf[i].z_mg;
    }

    p_accel->x_mg = (int16_t)(sum_x / (int32_t)s_filter_count);
    p_accel->y_mg = (int16_t)(sum_y / (int32_t)s_filter_count);
    p_accel->z_mg = (int16_t)(sum_z / (int32_t)s_filter_count);

    return E_OK;
}

EXPORT int16_t drv_gy511_calc_pitch_x10(const GY511_ACCEL_DATA_T *p_accel)
{
    int32_t ax;
    int32_t ay;
    int32_t az;
    uint32_t yz_sq;
    int32_t denom;
    int32_t r_x1000;
    int32_t r3_x1000;
    int32_t pitch_x10;

    if (p_accel == NULL) {
        return 0;
    }

    ax = (int32_t)p_accel->x_mg;
    ay = (int32_t)p_accel->y_mg;
    az = (int32_t)p_accel->z_mg;

    yz_sq = (uint32_t)((ay * ay) + (az * az));
    denom = (int32_t)int_sqrt32(yz_sq);
    if (denom < 1) {
        denom = 1;
    }

    /* Ratio r scaled by 1000 */
    r_x1000 = (ax * 1000) / denom;
    if (r_x1000 > 1000) {
        r_x1000 = 1000;
    } else if (r_x1000 < -1000) {
        r_x1000 = -1000;
    }

    /*
     * Fast, high-accuracy atan polynomial approximation:
     * deg = r * (57.2958 - 12.2 * r^2)
     * Yields error < 0.3 deg over +/- 20 deg and < 0.7 deg at 30 deg.
     */
    r3_x1000 = ((r_x1000 * r_x1000) / 1000) * r_x1000;
    pitch_x10 = ((573 * r_x1000) - ((122 * r3_x1000) / 1000)) / 1000;

    return (int16_t)pitch_x10;
}

EXPORT ER drv_gy511_calibrate_baseline(int16_t *p_baseline_x10, uint16_t sample_count)
{
    uint16_t i;
    int32_t sum_pitch = 0;
    GY511_ACCEL_DATA_T sample;
    ER er;

    if ((p_baseline_x10 == NULL) || (sample_count == 0U)) {
        return E_PAR;
    }

    for (i = 0U; i < sample_count; i++) {
        er = drv_gy511_read_accel(&sample);
        if (er != E_OK) {
            return er;
        }
        sum_pitch += (int32_t)drv_gy511_calc_pitch_x10(&sample);
        WaitUsec(10000U); /* 10 ms delay between calibration samples */
    }

    *p_baseline_x10 = (int16_t)(sum_pitch / (int32_t)sample_count);
    return E_OK;
}
