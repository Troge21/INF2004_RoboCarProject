/* Subsystem 2: motor command interface, PWM and optional encoder calibration. */
#ifndef SUBSYSTEM2_H
#define SUBSYSTEM2_H

#include "../app.h"
#include <stdint.h>

/* Standalone encoder bench, built with TEST=1 and USB/CRASH_REPORT=0.
 * Runs before the IR test and other normal subsystems. */
#define S2_ENCODER_CALIBRATION (APP_BENCH_MODE == 2)
/* 0: hand-turn only; 1: countdown, fixed-duty powered run, automatic stop.
 * Powered mode uses the existing motor task and its command watchdog. */
#define S2_ENCODER_POWERED_TEST 1
/* New bench mode: 1 tests closed-loop RPM; 0 retains the 18/20 duty trial. */
#define S2_ENCODER_PID_TEST     1
#define S2_ENCODER_TEST_RPM_X10 350
#define S2_ENCODER_TEST_LEFT_DUTY  18
#define S2_ENCODER_TEST_RIGHT_DUTY 20
#define S2_ENCODER_TEST_WAIT_MS 5000U
#define S2_ENCODER_TEST_RUN_MS  10000U
/* x1 counts per wheel revolution, stored in tenths. Latest user-confirmed
 * zero-start 10-revolution run: abs(L)=6337, abs(R)=5409, November 2026. */
#define S2_LEFT_COUNTS_PER_REV_X10  6337U
#define S2_RIGHT_COUNTS_PER_REV_X10 5409U
/* Outside tyre diameter supplied by the user; both wheels assumed equal. */
#define S2_WHEEL_DIAMETER_MM        65U
#define S2_LEFT_ENCODER_SIGN       (-1)
#define S2_RIGHT_ENCODER_SIGN      1
#if APP_TEST && S2_ENCODER_CALIBRATION
/* Periodic supervisor diagnostics; silent during powered measurements. */
EXPORT void subsystem2_test_status(void);
#endif
#if S2_ENCODER_TEST_LEFT_DUTY < 1 || S2_ENCODER_TEST_LEFT_DUTY > 100 || \
    S2_ENCODER_TEST_RIGHT_DUTY < 1 || S2_ENCODER_TEST_RIGHT_DUTY > 100
#error "Encoder test duties must be between 1 and 100"
#endif

/* Motor task owns PWM and stops on missing/stale controller commands. */
#define S2_PRI_MOTOR           14
#define S2_PERIOD_MS           20
#define S2_COMMAND_READ_WAIT_MS 10
#define S2_COMMAND_MAX_AGE_MS  100U
#define S2_SPEED_SAMPLE_MS     200U
#define S2_FEEDBACK_MAX_AGE_MS  500U
#define S2_LOOP_MAX_GAP_MS      250U
#define S2_STALL_TIMEOUT_MS    1500U
#define S2_MAX_RPM_X10          600

/* Conservative initial PID tuning; requires physical verification.
 * Gains are duty-percent/RPM, duty-percent/(RPM*s), duty-percent*s/RPM,
 * all multiplied by 1000. Derivative starts disabled for noisy encoders. */
#define S2_PID_KP_X1000          100
#define S2_PID_KI_X1000          200
#define S2_PID_KD_X1000          0
#define S2_PID_INTEGRAL_LIMIT_X100 1500
#define S2_PID_MAX_DUTY_X100     4000
#define S2_PID_SLEW_X100_PER_S   2000
/* Faster PWM reduction for the inside wheel of a strong forward turn. */
#define S2_TURN_FALL_X100_PER_S  8000
#define S2_TURN_MIN_SPLIT_RPM_X10 70
#define S2_PID_FF_RPM_X10       350
#define S2_PID_LEFT_FF_X100     1800
#define S2_PID_RIGHT_FF_X100    2000
/* Current analog line follower requests wheel RPM rather than raw PWM. */
#define S2_LINE_USE_RPM_CONTROL 1
#define S2_LINE_FORWARD_RPM_X10 350
#define S2_LINE_INNER_RPM_X10   260

/* Wheel power balance: 100 = unchanged, 90 = 90% of requested duty.
 * Reduce the faster wheel's scale. This is manual trim, not speed feedback. */
#define S2_LEFT_POWER_SCALE    100
#define S2_RIGHT_POWER_SCALE   100
#if S2_LEFT_POWER_SCALE < 0 || S2_LEFT_POWER_SCALE > 100 || \
    S2_RIGHT_POWER_SCALE < 0 || S2_RIGHT_POWER_SCALE > 100
#error "Motor power scales must be between 0 and 100"
#endif
#define S2_LEFT_REVERSED       0
#define S2_RIGHT_REVERSED      0

/* Forward-only duty percentages 0..100. Nonblocking; retry on E_TMOUT.
 * Commands expire after S2_COMMAND_MAX_AGE_MS; only the motor task writes PWM.
 * Returns E_OBJ before initialization or in unpowered APP_TEST builds. */
EXPORT ER subsystem2_set_duty(INT left, INT right);

typedef enum { S2_MODE_STOP, S2_MODE_DUTY, S2_MODE_RPM } S2_CONTROL_MODE;
typedef enum {
    S2_STOP_REQUESTED, S2_STOP_NONE, S2_STOP_COMMAND_TIMEOUT,
    S2_STOP_COMMAND_BUSY, S2_STOP_FAULT, S2_STOP_FEEDBACK_WAIT
} S2_STOP_REASON;
#define S2_FAULT_ENCODER       1U
#define S2_FAULT_TIMING        2U
#define S2_FAULT_LEFT_STALL    4U
#define S2_FAULT_RIGHT_STALL   8U
#define S2_FAULT_LEFT_DIRECTION 16U
#define S2_FAULT_RIGHT_DIRECTION 32U

/* Coherent snapshot, readable from either core; no Wi-Fi dependencies.
 * Counts/distances are signed net travel since S2 startup, forward positive.
 * x10 fields divide by 10; signed duty x100 fields divide by 100 for percent. */
typedef struct {
    UW sequence, timestamp_ms, speed_timestamp_ms, speed_period_ms;
    BOOL encoder_valid, speed_valid, command_active;
    S2_CONTROL_MODE mode;
    S2_STOP_REASON stop_reason;
    UW faults;
    INT target_left_rpm_x10, target_right_rpm_x10;
    INT actual_left_rpm_x10, actual_right_rpm_x10;
    INT error_left_rpm_x10, error_right_rpm_x10;
    INT duty_left_x100, duty_right_x100;
    int64_t count_left, count_right;
    int64_t distance_left_mm_x10, distance_right_mm_x10, distance_centre_mm_x10;
    INT speed_centre_mm_s_x10;
} S2_MOTION_DATA;

/* Forward wheel targets 0..S2_MAX_RPM_X10 (tenths of RPM).
 * Refresh at least every S2_COMMAND_MAX_AGE_MS; zero stops immediately.
 * Faults latch until reboot. Existing set_duty remains an open-loop API. */
EXPORT ER subsystem2_set_rpm_x10(INT left, INT right);
/* Signed RPM targets in tenths. Negative requests vehicle reverse. A wheel
 * changing direction must coast to a measured stop first. Same watchdog. */
EXPORT ER subsystem2_set_signed_rpm_x10(INT left, INT right);
/* Integrated TEST=1 runs passive encoder feedback with wheel PWM disabled.
 * Commands still return E_OBJ; measurements include hand-turned wheels.
 * E_OBJ until first snapshot; E_TMOUT on contention: retry later.
 * Consumers check timestamp age and speed_valid before showing live values. */
EXPORT ER subsystem2_get_motion(S2_MOTION_DATA *sample);

#endif /* SUBSYSTEM2_H */
