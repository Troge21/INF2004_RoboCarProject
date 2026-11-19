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

/* I2C timing delay in microseconds (5 us gives ~100 kHz standard mode) */
#define I2C_HALF_CLOCK_US           (5U)

/* Filter history buffer */
static GY511_ACCEL_DATA_T s_filter_buf[GY511_FILTER_WINDOW_SIZE];
static uint8_t s_filter_idx = 0U;
static uint8_t s_filter_count = 0U;

/* -------------------------------------------------------------------------
 * Low-Level I2C Bit-Bang Implementation
 * Uses open-drain behavior:
 *   HIGH: Release the GPIO as input. The bus pull-ups bring it high.
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
    (void)gpio_set_val(GY511_PIN_SDA, 0U);
    (void)gpio_set_pin(GY511_PIN_SDA, GPIO_MODE_OUT);
}

static ER i2c_scl_high(void)
{
    uint32_t timeout = 1000U;
    (void)gpio_set_pin(GY511_PIN_SCL, GPIO_MODE_IN);
    while (gpio_get_val(GY511_PIN_SCL) == 0U) {
        if (timeout == 0U) return E_IO;
        WaitUsec(1U);
        timeout--;
    }
    return E_OK;
}

static void i2c_scl_low(void)
{
    (void)gpio_set_val(GY511_PIN_SCL, 0U);
    (void)gpio_set_pin(GY511_PIN_SCL, GPIO_MODE_OUT);
}

static uint8_t i2c_sda_read(void)
{
    return (uint8_t)(gpio_get_val(GY511_PIN_SDA) != 0U ? 1U : 0U);
}

static ER i2c_start(void)
{
    i2c_sda_high();
    i2c_delay();
    if (i2c_scl_high() != E_OK) return E_IO;
    i2c_delay();
    if (i2c_sda_read() == 0U) return E_IO;
    i2c_sda_low();
    i2c_delay();
    i2c_scl_low();
    i2c_delay();
    return E_OK;
}

static ER i2c_finish(ER transfer)
{
    ER clock;
    i2c_sda_low();
    i2c_delay();
    clock = i2c_scl_high();
    i2c_delay();
    i2c_sda_high();
    i2c_delay();
    if (transfer != E_OK) return transfer;
    return clock == E_OK && i2c_sda_read() != 0U ? E_OK : E_IO;
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
        if (i2c_scl_high() != E_OK) return E_IO;
        i2c_delay();
        i2c_scl_low();
    }

    /* Release SDA for ACK */
    i2c_sda_high();
    i2c_delay();
    if (i2c_scl_high() != E_OK) return E_IO;
    i2c_delay();
    ack = i2c_sda_read();
    i2c_scl_low();
    i2c_delay();

    return (ack == 0U) ? E_OK : E_IO;
}

static ER i2c_read_byte(uint8_t *out, BOOL ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    i2c_sda_high();
    for (i = 0U; i < 8U; i++) {
        byte <<= 1;
        i2c_delay();
        if (i2c_scl_high() != E_OK) return E_IO;
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
    if (i2c_scl_high() != E_OK) return E_IO;
    i2c_delay();
    i2c_scl_low();
    i2c_sda_high();
    i2c_delay();

    *out = byte;
    return E_OK;
}

/* -------------------------------------------------------------------------
 * Low-Level Register Read/Write Helpers
 * ------------------------------------------------------------------------- */

static ER lsm303_write_reg(uint8_t reg, uint8_t val)
{
    ER er = i2c_start();
    if (er == E_OK) er = i2c_write_byte((uint8_t)(GY511_I2C_ADDR_ACCEL << 1));
    if (er == E_OK) {
        er = i2c_write_byte(reg);
    }
    if (er == E_OK) {
        er = i2c_write_byte(val);
    }
    return i2c_finish(er);
}

static ER lsm303_read_bytes(uint8_t reg, uint8_t *p_buf, uint8_t len)
{
    ER er;
    uint8_t i;

    if ((p_buf == NULL) || (len == 0U)) {
        return E_PAR;
    }

    er = i2c_start();
    if (er == E_OK) er = i2c_write_byte((uint8_t)(GY511_I2C_ADDR_ACCEL << 1));

    /* Sub-address: set MSB for multi-byte auto-increment if len > 1 */
    if (er == E_OK)
        er = i2c_write_byte((uint8_t)(len > 1U ? (reg | LSM303_AUTO_INCREMENT) : reg));

    /* Repeated start */
    if (er == E_OK) er = i2c_start();
    if (er == E_OK) er = i2c_write_byte((uint8_t)((GY511_I2C_ADDR_ACCEL << 1) | 0x01U));

    for (i = 0U; i < len && er == E_OK; i++)
        er = i2c_read_byte(&p_buf[i], i < (len - 1U));
    return i2c_finish(er);
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
    if (i2c_scl_high() != E_OK) return E_IO;
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

    /* Hold both bytes of each axis stable through the read. */
    er = lsm303_write_reg(LSM303_CTRL_REG4_A, LSM303_SCALE_2G_HR | LSM303_BLOCK_DATA_UPDATE);
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
    ER er = i2c_start();
    /* Accelerometer write address */
    if (er == E_OK) er = i2c_write_byte((uint8_t)(GY511_I2C_ADDR_ACCEL << 1));
    return i2c_finish(er);
}

/* The device returns a signed 12-bit number left-aligned in two bytes. */
static int16_t decode_axis(const uint8_t *bytes)
{
    unsigned raw = ((unsigned)bytes[1] << 4) | (bytes[0] >> 4);
    return (int16_t)((int)raw - ((raw & 0x800U) ? 4096 : 0));
}

EXPORT ER drv_gy511_read_accel(GY511_SAMPLE_T *sample)
{
    uint8_t raw_buf[7];
    int32_t sum_x = 0;
    int32_t sum_y = 0;
    int32_t sum_z = 0;
    uint8_t i;
    ER er;

    if (sample == NULL) {
        return E_PAR;
    }

    /* Read data-ready followed by the six acceleration bytes. */
    er = lsm303_read_bytes(LSM303_STATUS_REG_A, raw_buf, sizeof(raw_buf));
    if (er != E_OK) {
        return er;
    }

    if ((raw_buf[0] & LSM303_ACCEL_DATA_READY) == 0U) return E_BUSY;

    /* Update moving average window */
    sample->raw = (GY511_ACCEL_DATA_T){
        decode_axis(&raw_buf[1]), decode_axis(&raw_buf[3]), decode_axis(&raw_buf[5])
    };
    s_filter_buf[s_filter_idx] = sample->raw;

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

    sample->filtered.x_mg = (int16_t)(sum_x / (int32_t)s_filter_count);
    sample->filtered.y_mg = (int16_t)(sum_y / (int32_t)s_filter_count);
    sample->filtered.z_mg = (int16_t)(sum_z / (int32_t)s_filter_count);

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
