/*
 * subsystem4.h
 * Subsystem 4: GY-511 IMU Terrain Monitoring, Hump Detection, Height Estimation
 *              and Run Maximum Retention.
 *
 * Owner: Thaw Zin Htun (Student ID: 2502712, GitHub: thawzin07)
 * Role:  Physics Watchdog
 *
 * Conforms to Barr-C Coding Standard and micro T-Kernel 3.0 conventions.
 */

#ifndef SUBSYSTEM4_H
#define SUBSYSTEM4_H

#include <tk/tkernel.h>
#include "../app.h"
#include "drv_gy511.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * RTOS Configuration & Timing
 * ------------------------------------------------------------------------- */
#define S4_PRI_IMU                  (18U)   /* Priority in assigned 18-19 band */
#define S4_SAMPLE_PERIOD_MS         (20U)   /* 50 Hz sampling rate */
#define S4_CALIBRATION_SAMPLES      (50U)   /* 50 samples = 1.0 s calibration */

/* -------------------------------------------------------------------------
 * Hump Detection & Height Estimation Tuning Parameters (NFR7)
 * ------------------------------------------------------------------------- */
#define S4_TILT_ENTER_THRESH_X10    (35)    /* +3.5 deg pitch triggers ascent */
#define S4_TILT_EXIT_THRESH_X10     (-35)   /* -3.5 deg pitch triggers descent */
#define S4_TILT_FLAT_THRESH_X10     (15)    /* +/- 1.5 deg considered level */

#define S4_MIN_VALID_HUMP_MM        (6U)    /* Reject floor ripples/noise */
#define S4_MAX_VALID_HUMP_MM        (80U)   /* Plausible max obstacle/hump */
#define S4_MIN_TRAVERSAL_MS         (100U)  /* Reject quick transient spikes */
#define S4_MAX_TRAVERSAL_MS         (3000U) /* Max allowable hump duration */

#define S4_WHEEL_CENTRES_MM         (110U) /* Measured physical spacing for roll geometry */
#define S4_TURN_TRACK_MM            (120U) /* Effective spacing from the S2 turn bench */

/* -------------------------------------------------------------------------
 * State Machine & Telemetry Types
 * ------------------------------------------------------------------------- */
typedef enum {
    S4_STATE_INITIALIZING   = 0,
    S4_STATE_CALIBRATING    = 1,
    S4_STATE_MONITORING     = 2,
    S4_STATE_HUMP_ASCENT    = 3,
    S4_STATE_HUMP_DESCENT   = 4,
    S4_STATE_HUMP_VALIDATE  = 5,
    S4_STATE_FAULT          = 6
} S4_STATE_E;

/* Status flags */
#define S4_FLAG_SENSOR_OK           (1U << 0)
#define S4_FLAG_HUMP_ACTIVE         (1U << 1)
#define S4_FLAG_NEW_HUMP_EVENT      (1U << 2)
#define S4_FLAG_MAX_UPDATED         (1U << 3)
#define S4_FLAG_PITCH_VALID         (1U << 4) /* Fresh, calibrated sensor sample */
#define S4_FLAG_ENCODER_HEIGHT     (1U << 5) /* Last accepted height used encoder feedback */
#define S4_FLAG_FAULT               (1U << 7)

typedef enum {
    S4_MOTION_UNAVAILABLE, S4_MOTION_STATIONARY, S4_MOTION_FORWARD, S4_MOTION_REVERSE,
    S4_MOTION_ACCELERATING, S4_MOTION_SLOWING, S4_MOTION_LEFT, S4_MOTION_RIGHT,
    S4_MOTION_CLIMBING, S4_MOTION_DESCENDING, S4_MOTION_PICKUP, S4_MOTION_IMPACT
} S4_MOTION_CLASS;

typedef enum {
    S4_HUMP_NONE, S4_HUMP_FULL_WIDTH, S4_HUMP_LEFT, S4_HUMP_RIGHT
} S4_HUMP_KIND;

/*
 * Interface IF-02 Telemetry Structure:
 * Published to Vehicle Controller and Subsystem 1 (MQTT Telemetry).
 */
typedef struct {
    int16_t     pitch_deg_x10;      /* Current pitch in tenths of a degree */
    int16_t     baseline_pitch_x10; /* Zero-reference pitch from calibration */
    uint16_t    current_hump_mm;    /* Height of most recent completed hump */
    uint16_t    max_hump_mm;        /* Highest valid hump peak for this run */
    uint8_t     state;              /* S4_STATE_E */
    uint8_t     flags;              /* S4_FLAG_* bitmask */
    uint32_t    traversal_time_ms;  /* Duration of the current/last hump */
    uint32_t    timestamp_ms;       /* Snapshot publication time on the S1/S2 source clock */
    uint32_t    pitch_timestamp_ms; /* Last successful acceleration sample on the same clock */
    uint8_t     terrain[6];        /* Compact S4 telemetry extension, shared with S1 */
} HUMP_TELEMETRY_T;

/* -------------------------------------------------------------------------
 * Subsystem 4 Public Lifecycle & Interface APIs
 * ------------------------------------------------------------------------- */

/* Phase 1: Create the snapshot semaphore and publish initializing state */
EXPORT ER subsystem4_init(void);

/* Phase 2: Create and start the imu_hump_task */
EXPORT ER subsystem4_start(void);

/*
 * Interface IF-02: Non-blocking atomic read of latest telemetry snapshot.
 * Safe to call from any core/task (Vehicle Controller or Wilson's S1 task).
 */
EXPORT ER subsystem4_get_telemetry(HUMP_TELEMETRY_T *p_telemetry);

/*
 * Interface IF-08: Explicit new-run reset requested by Vehicle Controller.
 * In accordance with NC3, this is the ONLY authorized mechanism to clear max_hump_mm.
 */
EXPORT ER subsystem4_reset_run(void);

/* Current mounting: front-up makes sensor Y more negative.
 * Returns vehicle pitch in tenths of a degree; front-up is positive. */
EXPORT int16_t subsystem4_calc_vehicle_pitch_x10(const GY511_ACCEL_DATA_T *accel);

#if APP_TEST
/* Standalone on-target unit test suite (make TEST=1) */
EXPORT void subsystem4_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SUBSYSTEM4_H */
