/*
 * drv_gy511.h
 * Subsystem 4: GY-511 (LSM303DLHC) Accelerometer & Terrain Sensing Driver
 *
 * Hardware interface:
 *   SDA: GP2 (I2C1)
 *   SCL: GP3 (I2C1)
 *   7-bit I2C Slave Address (Accelerometer): 0x19
 *
 * Conforms to Barr-C Coding Standard and micro T-Kernel 3.0 conventions.
 */

#ifndef DRV_GY511_H
#define DRV_GY511_H

#include <tk/tkernel.h>
#include <bsp/libbsp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard fixed-width aliases using micro T-Kernel types */
typedef H   int16_t;
typedef UH  uint16_t;
typedef UB  uint8_t;
typedef W   int32_t;
typedef UW  uint32_t;

/* -------------------------------------------------------------------------
 * Hardware Pin & Bus Configuration
 * ------------------------------------------------------------------------- */
#define GY511_PIN_SDA               (2U)
#define GY511_PIN_SCL               (3U)

/* LSM303DLHC 7-bit I2C addresses */
#define GY511_I2C_ADDR_ACCEL        (0x19U)
#define GY511_I2C_ADDR_MAG          (0x1EU)

/* LSM303DLHC Accelerometer Registers */
#define LSM303_CTRL_REG1_A          (0x20U)
#define LSM303_CTRL_REG2_A          (0x21U)
#define LSM303_CTRL_REG3_A          (0x22U)
#define LSM303_CTRL_REG4_A          (0x23U)
#define LSM303_CTRL_REG5_A          (0x24U)
#define LSM303_CTRL_REG6_A          (0x25U)
#define LSM303_REFERENCE_A          (0x26U)
#define LSM303_STATUS_REG_A         (0x27U)
#define LSM303_OUT_X_L_A            (0x28U)
#define LSM303_OUT_X_H_A            (0x29U)
#define LSM303_OUT_Y_L_A            (0x2AU)
#define LSM303_OUT_Y_H_A            (0x2BU)
#define LSM303_OUT_Z_L_A            (0x2CU)
#define LSM303_OUT_Z_H_A            (0x2DU)

/* Auto-increment bit for multi-byte I2C read */
#define LSM303_AUTO_INCREMENT       (0x80U)

/* CTRL_REG1_A configuration: 100 Hz ODR, normal mode, X/Y/Z enable */
#define LSM303_ODR_100HZ_XYZ_EN     (0x57U)

/* CTRL_REG4_A configuration: +/- 2g scale, High-Resolution mode (1 mg/LSB) */
#define LSM303_SCALE_2G_HR          (0x08U)

/* Moving average window size for chassis vibration filtering */
#define GY511_FILTER_WINDOW_SIZE    (5U)

/* -------------------------------------------------------------------------
 * Data Types
 * ------------------------------------------------------------------------- */
typedef struct {
    int16_t x_mg;   /* Longitudinal acceleration (forward/backward) in mg */
    int16_t y_mg;   /* Lateral acceleration (left/right) in mg */
    int16_t z_mg;   /* Normal acceleration (vertical) in mg */
} GY511_ACCEL_DATA_T;

/* -------------------------------------------------------------------------
 * Driver API Declarations
 * ------------------------------------------------------------------------- */

/*
 * Initialize I2C pins and configure the LSM303DLHC accelerometer.
 * Returns E_OK on success, or a negative micro T-Kernel error code on failure.
 */
EXPORT ER drv_gy511_init(void);

/*
 * Read calibrated raw acceleration vectors in milli-g (mg).
 * Returns E_OK on success, or E_IO on communication error.
 */
EXPORT ER drv_gy511_read_accel(GY511_ACCEL_DATA_T *p_accel);

/*
 * Compute pitch angle in tenths of a degree (e.g., +54 = +5.4 degrees tilt).
 * Positive pitch indicates front of vehicle tilted upwards (climbing).
 */
EXPORT int16_t drv_gy511_calc_pitch_x10(const GY511_ACCEL_DATA_T *p_accel);

/*
 * Perform stationary zero-pitch offset calibration over a number of samples.
 * baseline_offset_x10 is populated with the resting pitch angle.
 */
EXPORT ER drv_gy511_calibrate_baseline(int16_t *p_baseline_x10, uint16_t sample_count);

/*
 * Probe the physical sensor on the I2C bus to confirm presence.
 * Returns E_OK if sensor acknowledges address, or E_IO otherwise.
 */
EXPORT ER drv_gy511_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_GY511_H */
