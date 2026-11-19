/* Whole GY-511 driver with scripted GPIO reads. No physical I2C. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define EXPORT
#define E_OK 0
#define E_PAR (-17)
#define E_IO (-57)
#define E_BUSY (-65)
#define GPIO_MODE_IN 0
#define GPIO_MODE_OUT 1
typedef uint8_t UB;
typedef int16_t H;
typedef uint16_t UH;
typedef int32_t W, ER, BOOL;
typedef uint32_t UW, UINT;

static unsigned sda[64], sda_count, sda_index, scl_reads, fail_after;
static unsigned sda_stuck, stretch_waits;

static void WaitUsec(UW delay)
{
    if (delay == 1U) ++stretch_waits;
}

static ER gpio_set_pin(UINT pin, UINT mode)
{
    assert((pin == 2U || pin == 3U) && mode <= GPIO_MODE_OUT);
    return E_OK;
}

static ER gpio_set_val(UINT pin, UINT value)
{
    assert((pin == 2U || pin == 3U) && value == 0U);
    return E_OK;
}

static UINT gpio_get_val(UINT pin)
{
    if (pin == 3U) return ++scl_reads <= fail_after;
    assert(pin == 2U);
    if (sda_stuck) return 0U;
    assert(sda_index < sda_count);
    return sda[sda_index++];
}

#include "../../app_program/subsystem4/drv_gy511.c"

static void append_byte(unsigned byte)
{
    for (unsigned i = 0; i < 8U; ++i)
        sda[sda_count++] = (byte >> (7U - i)) & 1U;
}

static void append_axis(int axis)
{
    unsigned word = ((unsigned)axis & 0xfffU) << 4;
    append_byte(word & 0xffU);
    append_byte(word >> 8);
}

static void prepare_read(unsigned status, int x, int y, int z)
{
    sda_count = sda_index = scl_reads = stretch_waits = 0U;
    sda_stuck = 0U;
    fail_after = UINT32_MAX;
    sda[sda_count++] = 1; /* START bus idle */
    sda[sda_count++] = 0; /* Write address ACK */
    sda[sda_count++] = 0; /* Register ACK */
    sda[sda_count++] = 1; /* Repeated START line release */
    sda[sda_count++] = 0; /* Read address ACK */
    append_byte(status);
    append_axis(x);
    append_axis(y);
    append_axis(z);
    sda[sda_count++] = 1; /* STOP line release */
}

int main(void)
{
    GY511_SAMPLE_T result;
    fail_after = UINT32_MAX;
    sda_stuck = 1;
    assert(drv_gy511_init() == E_IO && s_filter_count == 0U);
    puts("PASS: stuck SDA cannot appear as a responding sensor.");

    prepare_read(LSM303_ACCEL_DATA_READY, -2048, 2047, 1000);
    assert(drv_gy511_read_accel(&result) == E_OK && sda_index == sda_count);
    assert(result.raw.x_mg == -2048 && result.raw.y_mg == 2047 && result.raw.z_mg == 1000);
    assert(result.filtered.x_mg == -2048 && s_filter_count == 1U);
    puts("PASS: new data decodes signed 12-bit boundaries and advances the filter.");

    prepare_read(0U, 0, 0, 0);
    assert(drv_gy511_read_accel(&result) == E_BUSY && sda_index == sda_count);
    assert(result.raw.x_mg == -2048 && s_filter_count == 1U);
    puts("PASS: data not ready preserves the caller's sample and filter.");

    prepare_read(LSM303_ACCEL_DATA_READY, 0, 0, 1000);
    fail_after = 40;
    assert(drv_gy511_read_accel(&result) == E_IO && sda_index < sda_count);
    assert(result.raw.x_mg == -2048 && s_filter_count == 1U && stretch_waits == 2000U);
    puts("PASS: SCL failure during a burst aborts the sample with bounded wait.");

    prepare_read(LSM303_ACCEL_DATA_READY, 0, 0, 1000);
    assert(drv_gy511_read_accel(&result) == E_OK && sda_index == sda_count);
    assert(result.raw.x_mg == 0 && result.filtered.x_mg == -1024 && s_filter_count == 2U);
    puts("PASS: subsequent valid samples resume filtering after the I/O failure.");
    return 0;
}
