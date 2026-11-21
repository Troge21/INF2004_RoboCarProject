/*
 * subsystem4.c
 * Subsystem 4: GY-511 IMU Terrain Monitoring, Hump Detection, Height Estimation
 *              and Run Maximum Retention.
 *
 * Implements the 6-state RTOS task according to Section 6.4 of the design package,
 * enforces Decision NC3 for valid-only run maximum retention, and provides
 * Interface IF-02 telemetry and IF-08 new-run reset handling.
 *
 * Conforms to Barr-C Coding Standard and micro T-Kernel 3.0 conventions.
 */

#include "subsystem4.h"
#include "../controller.h"
#include "../subsystem2/subsystem2.h"
#include <config.h>
#include <sys/sysdef.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#ifndef APP_S4_MOTOR_TRACE
#define APP_S4_MOTOR_TRACE 0
#endif

#if (APP_TEST || APP_S4_MOTOR_TRACE) && TM_CONSOLE_USB_CDC
#include "../subsystem3/subsystem3.h"
IMPORT UW tm_usb_state(void);
IMPORT UW tm_usb_pending(void);
#if APP_S4_MOTOR_TRACE && !APP_TEST
#define S4_TRACE_PREFIX "[S4 MOTOR TRACE] "
#else
#define S4_TRACE_PREFIX "[S4 TRACE] "
#endif
#endif

#define S4_SAMPLE_MAX_AGE_MS        (100U)
#define S4_CAL_MAX_AXIS_CHANGE_MG   (35)
#define S4_CAL_MIN_GRAVITY_MG       (800U)
#define S4_CAL_MAX_GRAVITY_MG       (1200U)
#define S4_PICKUP_CONFIRM_MS        (160U)
#define S4_PICKUP_MAX_SAMPLE_GAP_MS (2U * S4_SAMPLE_PERIOD_MS)
/* cos(55 degrees)^2 in Q8, for a comparison without a square root. */
#define S4_PICKUP_COS_SQ_Q8         (84U)
/* The natural-lift trace rose along the calibrated vertical before tilting. */
#define S4_LIFT_RISE_MS             (60U)
#define S4_LIFT_RISE_DIVISOR        (10U)
#define S4_LIFT_WINDOW_MS           (900U)
#define S4_LIFT_COS_SQ_Q8           (220U) /* cos(22 degrees)^2 */
#define S4_ROLL_ENTER_X10          30
#define S4_ROLL_SMOOTH_MM_X10      150U
#define S4_LEVEL_FILTER_MS         160U
#define S4_HUMP_FLAT_MS            120U
#define S4_HUMP_MIN_TRAVEL_X10     250U
/* Confirm sustained right tilt before allowing a slower, noisier exit. */
#define S4_RIGHT_HUMP_TILT_TRAVEL_X10 300U
#define S4_RIGHT_HUMP_MAX_MS          4500U
#define S4_RIGHT_HUMP_EXIT_ROLL_X10   20
#define S4_RIGHT_HUMP_EXIT_PITCH_X10  30
#define S4_ENCODER_MAX_AGE_MS      100U
#define S4_HUMP_MAX_STEP_X10       120
#define S4_HUMP_MAX_TURN_DEG_S     35
#define S4_TURN_EVENT_DEG_S        18
#define S4_STATIONARY_MM_S_X10     50
#define S4_ACCEL_EVENT_MM_S2       75
#define S4_IMPACT_HOLD_MS          1000U

enum {
    S4_LIFT_IDLE, S4_LIFT_RISING, S4_LIFT_ARMED
};

typedef struct {
    uint32_t lift_since_ms;
    uint32_t tilt_since_ms;
    uint8_t lift_phase;
    uint8_t tilt_active;
} S4_PICKUP_PROGRESS;

typedef struct {
    int64_t left_mm_x10, right_mm_x10;
    uint32_t started_ms, flat_ms, travel_x10, right_tilt_travel_x10;
    int32_t roll_x1000;
    int16_t pitch_x10, peak_roll_x10;
    int32_t level_pitch_x1000, level_roll_x1000;
    BOOL position_valid, level_seen, pitch_descended, settling;
} S4_TERRAIN_PROGRESS;

typedef struct {
    uint32_t speed_time, impact_time;
    int32_t speed_x10;
    BOOL have_speed, impact;
    uint8_t wheel_class, reported;
} S4_MOTION_PROGRESS;

#define s4_abs(x)                   (((x) < 0) ? -(x) : (x))

/* Module state variables */
static S4_STATE_E s_state = S4_STATE_INITIALIZING;
static int16_t s_baseline_pitch_x10 = 0;
static uint16_t s_current_hump_mm = 0U;
static uint16_t s_max_hump_mm = 0U;
static uint8_t s_flags = 0U;

/* Hump integration accumulation in micrometers (um) for integer precision */
static uint32_t s_accumulated_height_um = 0U;
static uint32_t s_last_traversal_duration_ms = 0U;
static S4_TERRAIN_PROGRESS s_terrain;
static int16_t s_roll_x10;
static uint16_t s_turn_code, s_hump_event;
static uint8_t s_motion_class, s_hump_kind;

/* RTOS Synchronization: Semaphore protecting telemetry snapshot */
static ID s_sem_telemetry;
/* Published fields only: pitch:11, baseline:11, heights:7+7, state:3,
 * flags:6, traversal:32, publication time:32, pitch sample time:32.
 * 141 logical bits round up to 18 bytes. Six following bytes hold roll:11,
 * encoder turn rate:9, motion class:4, hump kind:2 and event sequence:16.
 * Runtime estimator state above
 * remains private to the IMU task; readers never race its intermediate writes. */
static uint8_t s_snapshot[24];
/* Shared explicit new-run reset request. */
static uint8_t s_requests;

static void snapshot_put(unsigned bit, unsigned width, uint32_t value)
{
    unsigned i;
    for (i = 0; i < width; ++i) {
        unsigned at = bit + i;
        uint8_t mask = (uint8_t)(1U << (at % 8U));
        s_snapshot[at / 8U] = (uint8_t)((s_snapshot[at / 8U] & (uint8_t)~mask) |
            (((value >> i) & 1U) ? mask : 0U));
    }
}
static uint32_t snapshot_get(unsigned bit, unsigned width)
{
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < width; ++i)
        value |= (uint32_t)((s_snapshot[(bit + i) / 8U] >> ((bit + i) % 8U)) & 1U) << i;
    return value;
}

/*
 * Source clock shared with S1 and S2 snapshots.
 */
static uint32_t get_time_ms(void)
{
    SYSTIM tim;
    (void)tk_get_tim(&tim);
    return (uint32_t)tim.lo;
}

static uint32_t get_operating_ms(void)
{
    SYSTIM tim;
    (void)tk_get_otm(&tim);
    return (uint32_t)tim.lo;
}

/* Only the owner calls this. No sensor I/O or wait while holding the lock.
 * If a reader owns the semaphore, retain the old snapshot and retry next loop. */
static void cancel_hump(void)
{
    s_terrain = (S4_TERRAIN_PROGRESS){0};
    s_accumulated_height_um = 0U;
    s_flags &= (uint8_t)~S4_FLAG_HUMP_ACTIVE;
    if (s_state >= S4_STATE_HUMP_ASCENT && s_state <= S4_STATE_HUMP_VALIDATE)
        s_state = S4_STATE_MONITORING;
}

static void publish_snapshot(int16_t pitch, uint32_t pitch_time, uint32_t now,
                              BOOL have_pitch)
{
    unsigned compact_flags;
    BOOL pitch_valid;
    if (tk_wai_sem(s_sem_telemetry, 1, TMO_POL) != E_OK) return;
    if (s_requests & 1U) {
        s_current_hump_mm = s_max_hump_mm = 0U;
        s_last_traversal_duration_ms = 0U;
        s_hump_kind = S4_HUMP_NONE;
        s_flags &= (uint8_t)~(S4_FLAG_MAX_UPDATED | S4_FLAG_NEW_HUMP_EVENT | S4_FLAG_ENCODER_HEIGHT);
        cancel_hump();
        s_requests &= (uint8_t)~1U;
    }
    if (s_state == S4_STATE_FAULT) {
        s_flags |= S4_FLAG_FAULT;
        s_flags &= (uint8_t)~(S4_FLAG_SENSOR_OK | S4_FLAG_HUMP_ACTIVE);
    }
    pitch_valid = have_pitch && (uint32_t)(now - pitch_time) <= S4_SAMPLE_MAX_AGE_MS &&
        s_state >= S4_STATE_MONITORING && s_state <= S4_STATE_HUMP_VALIDATE &&
        (s_flags & S4_FLAG_SENSOR_OK) && !(s_flags & S4_FLAG_FAULT) &&
        pitch >= -900 && pitch <= 900;
    const BOOL terrain_valid = pitch_valid && s_roll_x10 >= -900 && s_roll_x10 <= 900;
    compact_flags = (s_flags & 15U) | ((s_flags & S4_FLAG_ENCODER_HEIGHT) >> 1) |
                    ((s_flags & S4_FLAG_FAULT) >> 2);
    snapshot_put(0, 11, pitch_valid ? (uint32_t)((int)pitch + 901) : 0U);
    snapshot_put(11, 11, (uint32_t)((int)s_baseline_pitch_x10 + 900));
    snapshot_put(22, 7, s_current_hump_mm);
    snapshot_put(29, 7, s_max_hump_mm);
    snapshot_put(36, 3, (uint32_t)s_state);
    snapshot_put(39, 6, compact_flags);
    snapshot_put(45, 32, s_last_traversal_duration_ms);
    snapshot_put(77, 32, now);
    snapshot_put(109, 32, pitch_time);
    snapshot_put(144, 11, terrain_valid ? (uint32_t)(s_roll_x10 + 901) : 0U);
    snapshot_put(155, 9, terrain_valid ? s_turn_code : 0U);
    snapshot_put(164, 4, terrain_valid ? s_motion_class : S4_MOTION_UNAVAILABLE);
    snapshot_put(168, 2, s_hump_kind);
    snapshot_put(170, 16, s_hump_event);
    (void)tk_sig_sem(s_sem_telemetry, 1);
}

/* -------------------------------------------------------------------------
 * RTOS Task: imu_hump_task
 * ------------------------------------------------------------------------- */
EXPORT int16_t subsystem4_calc_vehicle_pitch_x10(const GY511_ACCEL_DATA_T *accel)
{
    GY511_ACCEL_DATA_T mapped;

    if (accel == NULL) {
        return 0;
    }
    /* Use Y as the longitudinal axis, X/Z for the denominator.
     * Negate the resulting angle so front-up is positive. */
    mapped.x_mg = accel->y_mg;
    mapped.y_mg = accel->x_mg;
    mapped.z_mg = accel->z_mg;
    return (int16_t)-drv_gy511_calc_pitch_x10(&mapped);
}

static BOOL motion_during_calibration(void)
{
    S2_MOTION_DATA motion;
    uint32_t source_ms;
    if (subsystem2_get_motion(&motion) != E_OK) return FALSE;
    source_ms = get_time_ms();
    if (source_ms - motion.timestamp_ms > S2_FEEDBACK_MAX_AGE_MS) return FALSE;

    return motion.command_active || motion.duty_left_x100 || motion.duty_right_x100 ||
        (motion.speed_valid &&
         (uint32_t)(source_ms - motion.speed_timestamp_ms) <= S2_FEEDBACK_MAX_AGE_MS &&
         (motion.actual_left_rpm_x10 || motion.actual_right_rpm_x10));
}

static uint32_t norm_squared(const GY511_ACCEL_DATA_T *accel)
{
    int32_t x = accel->x_mg, y = accel->y_mg, z = accel->z_mg;
    /* Three signed 12-bit axes as decoded by the driver. */
    return (uint32_t)(x * x + y * y + z * z);
}

static BOOL gravity_sample(uint32_t norm_sq)
{
    return norm_sq >= S4_CAL_MIN_GRAVITY_MG * S4_CAL_MIN_GRAVITY_MG &&
           norm_sq <= S4_CAL_MAX_GRAVITY_MG * S4_CAL_MAX_GRAVITY_MG;
}

/* Sine of an angle in tenths of a degree, scaled by 10000. The cubic
 * approximation is used only for calibrated angles within 45 degrees. */
static int32_t slope_q10000(int angle_x10)
{
    const int32_t radians = angle_x10 * 17453 / 1000;
    return radians - (int32_t)((int64_t)radians * radians * radians / 600000000LL);
}

static uint32_t side_height_mm(int16_t roll_x10)
{
    return (S4_WHEEL_CENTRES_MM * (uint32_t)s4_abs(slope_q10000(roll_x10)) + 5000U) / 10000U;
}

static BOOL right_hump_confirmed(void)
{
    return s_terrain.peak_roll_x10 < 0 &&
        s_terrain.right_tilt_travel_x10 >= S4_RIGHT_HUMP_TILT_TRAVEL_X10;
}

static void complete_hump(uint32_t elapsed_ms)
{
    const uint32_t pitch_mm = (s_accumulated_height_um + 500U) / 1000U;
    const uint32_t roll_mm = side_height_mm(s_terrain.peak_roll_x10);
    const BOOL side = roll_mm >= S4_MIN_VALID_HUMP_MM &&
        (s_terrain.peak_roll_x10 > 0 || right_hump_confirmed()) &&
        (!s_terrain.pitch_descended || roll_mm >= pitch_mm);
    const uint32_t height = side ? roll_mm : pitch_mm;
    if (elapsed_ms < S4_MIN_TRAVERSAL_MS || s_terrain.travel_x10 < S4_HUMP_MIN_TRAVEL_X10 ||
        (!side && !s_terrain.pitch_descended) ||
        height < S4_MIN_VALID_HUMP_MM || height > S4_MAX_VALID_HUMP_MM) return;

    s_current_hump_mm = (uint16_t)height;
    s_last_traversal_duration_ms = elapsed_ms;
    s_hump_kind = side ? (s_terrain.peak_roll_x10 > 0 ? S4_HUMP_LEFT : S4_HUMP_RIGHT) :
        S4_HUMP_FULL_WIDTH;
    ++s_hump_event;
    s_flags |= S4_FLAG_NEW_HUMP_EVENT | S4_FLAG_ENCODER_HEIGHT;
    if (s_current_hump_mm > s_max_hump_mm) {
        s_max_hump_mm = s_current_hump_mm;
        s_flags |= S4_FLAG_MAX_UPDATED;
    }
    tm_printf((UB*)"[S4] VALID HUMP: %s, height=%u mm, duration=%u ms, run max=%u mm\n",
              side ? (s_hump_kind == S4_HUMP_LEFT ? "left wheel" : "right wheel") : "full width",
              (UINT)height, (UINT)elapsed_ms, (UINT)s_max_hump_mm);
}

static void terrain_update(const S2_MOTION_DATA *motion, int16_t pitch_x10,
                           uint32_t now_ms, uint32_t sample_dt_ms)
{
    if (!motion || sample_dt_ms > S4_PICKUP_MAX_SAMPLE_GAP_MS ||
        s4_abs(pitch_x10) > 450 || s4_abs(s_roll_x10) > 450 ||
        (s_turn_code && s4_abs((int)s_turn_code - 256) > S4_HUMP_MAX_TURN_DEG_S)) {
        cancel_hump();
        return;
    }

    int64_t left = 0, right = 0;
    if (s_terrain.position_valid) {
        left = motion->distance_left_mm_x10 - s_terrain.left_mm_x10;
        right = motion->distance_right_mm_x10 - s_terrain.right_mm_x10;
        /* Average the signed angles over time so motor vibration does not
         * restart level confirmation on every brief deviation. This filter
         * continues to settle even after encoder positions stop changing. */
        s_terrain.level_pitch_x1000 += ((int32_t)pitch_x10 * 100 -
            s_terrain.level_pitch_x1000) * (int32_t)sample_dt_ms / (int32_t)S4_LEVEL_FILTER_MS;
        s_terrain.level_roll_x1000 += ((int32_t)s_roll_x10 * 100 -
            s_terrain.level_roll_x1000) * (int32_t)sample_dt_ms / (int32_t)S4_LEVEL_FILTER_MS;
    } else {
        s_terrain.level_pitch_x1000 = (int32_t)pitch_x10 * 100;
        s_terrain.level_roll_x1000 = (int32_t)s_roll_x10 * 100;
    }
    const BOOL level = s4_abs(s_terrain.level_pitch_x1000) <= S4_TILT_FLAT_THRESH_X10 * 100 &&
        s4_abs(s_terrain.level_roll_x1000) <= S4_TILT_FLAT_THRESH_X10 * 100;
    s_terrain.left_mm_x10 = motion->distance_left_mm_x10;
    s_terrain.right_mm_x10 = motion->distance_right_mm_x10;
    s_terrain.position_valid = TRUE;
    if (left < 0 || right < 0 || left > S4_HUMP_MAX_STEP_X10 || right > S4_HUMP_MAX_STEP_X10) {
        cancel_hump();
        return;
    }
    const uint32_t step_x10 = (uint32_t)(left + right) / 2U;
    if (!s_terrain.level_seen) {
        s_terrain.level_seen = level;
        s_terrain.pitch_x10 = pitch_x10;
        return;
    }

    /* Filter side tilt over travelled distance. Holding the car at an angle
     * or repeating the same encoder position cannot create a higher peak. */
    s_terrain.roll_x1000 += ((int32_t)s_roll_x10 * 100 - s_terrain.roll_x1000) *
        (int32_t)step_x10 / (int32_t)S4_ROLL_SMOOTH_MM_X10;
    const int16_t roll_x10 = (int16_t)(s_terrain.roll_x1000 / 100);
    const int16_t previous_pitch = s_terrain.pitch_x10;
    s_terrain.pitch_x10 = pitch_x10;

    if (!(s_flags & S4_FLAG_HUMP_ACTIVE)) {
        if (!step_x10 || (pitch_x10 < S4_TILT_ENTER_THRESH_X10 &&
                         s4_abs(roll_x10) < S4_ROLL_ENTER_X10)) return;
        s_terrain.started_ms = now_ms;
        s_flags = (uint8_t)((s_flags | S4_FLAG_HUMP_ACTIVE) &
            ~(S4_FLAG_NEW_HUMP_EVENT | S4_FLAG_MAX_UPDATED));
        s_state = S4_STATE_HUMP_ASCENT;
    }
    const uint32_t duration = now_ms - s_terrain.started_ms;
    if (roll_x10 <= -S4_ROLL_ENTER_X10)
        s_terrain.right_tilt_travel_x10 += step_x10;
    if (s4_abs(roll_x10) > s4_abs(s_terrain.peak_roll_x10))
        s_terrain.peak_roll_x10 = roll_x10;
    const BOOL right_confirmed = right_hump_confirmed();
    if (duration > (right_confirmed ? S4_RIGHT_HUMP_MAX_MS : S4_MAX_TRAVERSAL_MS)) {
        cancel_hump();
        return;
    }
    s_terrain.travel_x10 += step_x10;
    if (!s_terrain.pitch_descended) {
        const int32_t slope = slope_q10000((previous_pitch + pitch_x10) / 2);
        if (slope > 0)
            s_accumulated_height_um += step_x10 * (uint32_t)slope / 100U;
        s_terrain.pitch_descended = pitch_x10 <= S4_TILT_EXIT_THRESH_X10;
    }
    /* The distance-smoothed roll supplies the peak. Settle against the live
     * level check so the filter need not advance after the wheels stop. */
    const BOOL exit_level = level || (right_confirmed &&
        roll_x10 - s_terrain.peak_roll_x10 >= S4_TILT_FLAT_THRESH_X10 &&
        s4_abs(s_terrain.level_roll_x1000) <= S4_RIGHT_HUMP_EXIT_ROLL_X10 * 100 &&
        s4_abs(s_terrain.level_pitch_x1000) <= S4_RIGHT_HUMP_EXIT_PITCH_X10 * 100);
    const BOOL side_descending = s4_abs(s_terrain.peak_roll_x10) >= S4_ROLL_ENTER_X10 &&
        (exit_level || s4_abs(s_terrain.peak_roll_x10) - s4_abs(roll_x10) >= S4_TILT_FLAT_THRESH_X10);
    const BOOL descending = s_terrain.pitch_descended || side_descending;
    s_state = descending ? S4_STATE_HUMP_DESCENT : S4_STATE_HUMP_ASCENT;
    if (!descending || !exit_level) {
        s_terrain.settling = FALSE;
    } else if (!s_terrain.settling) {
        s_terrain.settling = TRUE;
        s_terrain.flat_ms = now_ms;
    } else if (now_ms - s_terrain.flat_ms >= S4_HUMP_FLAT_MS) {
        complete_hump(duration);
        cancel_hump();
    }
}

static void update_wheel_motion(S4_MOTION_PROGRESS *progress,
                                const S2_MOTION_DATA *motion, uint32_t source_ms)
{
    s_turn_code = 0U;
    if (!motion || !motion->speed_valid ||
        source_ms - motion->speed_timestamp_ms > S2_FEEDBACK_MAX_AGE_MS ||
        motion->actual_left_rpm_x10 < -2000 || motion->actual_left_rpm_x10 > 2000 ||
        motion->actual_right_rpm_x10 < -2000 || motion->actual_right_rpm_x10 > 2000) {
        progress->have_speed = FALSE;
        progress->wheel_class = S4_MOTION_UNAVAILABLE;
        return;
    }
    const int32_t difference = motion->actual_right_rpm_x10 - motion->actual_left_rpm_x10;
    /* Convert RPM difference to degrees/s. Wheel circumference is pi*D;
     * yaw conversion cancels pi. Positive is a left turn. */
    const int32_t yaw = difference * S2_WHEEL_DIAMETER_MM * 3 / (10 * (int32_t)S4_TURN_TRACK_MM);
    if (yaw >= -255 && yaw <= 255) s_turn_code = (uint16_t)(yaw + 256);

    if (progress->have_speed && progress->speed_time == motion->speed_timestamp_ms) return;
    const int32_t speed = motion->speed_centre_mm_s_x10;
    const uint32_t elapsed = motion->speed_timestamp_ms - progress->speed_time;
    int32_t accel = 0;
    if (progress->have_speed && elapsed && elapsed <= S2_FEEDBACK_MAX_AGE_MS)
        accel = (int32_t)((int64_t)(s4_abs(speed) - s4_abs(progress->speed_x10)) * 100 / elapsed);
    progress->speed_x10 = speed;
    progress->speed_time = motion->speed_timestamp_ms;
    progress->have_speed = TRUE;
    if (yaw >= S4_TURN_EVENT_DEG_S) progress->wheel_class = S4_MOTION_LEFT;
    else if (yaw <= -S4_TURN_EVENT_DEG_S) progress->wheel_class = S4_MOTION_RIGHT;
    else if (accel >= S4_ACCEL_EVENT_MM_S2) progress->wheel_class = S4_MOTION_ACCELERATING;
    else if (accel <= -S4_ACCEL_EVENT_MM_S2 && s4_abs(speed) >= S4_STATIONARY_MM_S_X10)
        progress->wheel_class = S4_MOTION_SLOWING;
    else if (s4_abs(speed) < S4_STATIONARY_MM_S_X10) progress->wheel_class = S4_MOTION_STATIONARY;
    else progress->wheel_class = speed > 0 ? S4_MOTION_FORWARD : S4_MOTION_REVERSE;
}

static void report_motion(S4_MOTION_PROGRESS *progress, BOOL pickup, uint32_t now_ms,
                          uint32_t raw_norm, uint32_t reference_norm)
{
    static const char *const names[] = {
        "Unavailable", "Stationary", "Forward", "Reverse", "Accelerating", "Slowing",
        "Turning left", "Turning right", "Climbing hump", "Descending hump", "Pickup", "Sudden impact"
    };
    /* A short peak above 1.5 times calibrated gravity is an impact cue.
     * Only the separately confirmed pickup drives the motor STOP latch. */
    if (!pickup && gravity_sample(reference_norm) && raw_norm * 4U >= reference_norm * 9U) {
        progress->impact_time = now_ms;
        progress->impact = TRUE;
    }
    if (progress->impact && now_ms - progress->impact_time >= S4_IMPACT_HOLD_MS)
        progress->impact = FALSE;
    s_motion_class = pickup ? S4_MOTION_PICKUP : progress->impact ? S4_MOTION_IMPACT :
        s_state == S4_STATE_HUMP_ASCENT ? S4_MOTION_CLIMBING :
        s_state == S4_STATE_HUMP_DESCENT ? S4_MOTION_DESCENDING : progress->wheel_class;
    if (s_motion_class != progress->reported) {
        progress->reported = s_motion_class;
        tm_printf((UB*)"[S4] Motion: %s\n", names[s_motion_class]);
    }
}

/* Keep the deliberate tilt trigger. A smaller tilt needs a recent rise
 * along the calibrated vertical, sustained above one tenth of resting g. */
static BOOL pickup_update(S4_PICKUP_PROGRESS *progress,
                          const GY511_ACCEL_DATA_T *reference,
                          const GY511_ACCEL_DATA_T *accel, uint32_t now_ms)
{
    const uint32_t reference_sq = norm_squared(reference);
    const uint32_t accel_sq = norm_squared(accel);
    const int32_t dot = (int32_t)reference->x_mg * accel->x_mg +
        (int32_t)reference->y_mg * accel->y_mg + (int32_t)reference->z_mg * accel->z_mg;
    const BOOL gravity_ok = gravity_sample(accel_sq);
    if (!gravity_sample(reference_sq)) return FALSE;

    if (progress->lift_phase == S4_LIFT_ARMED) {
        if (now_ms - progress->lift_since_ms > S4_LIFT_WINDOW_MS)
            progress->lift_phase = S4_LIFT_IDLE;
    } else if (!gravity_ok || dot < (int32_t)(reference_sq + reference_sq / S4_LIFT_RISE_DIVISOR)) {
        progress->lift_phase = S4_LIFT_IDLE;
    } else if (progress->lift_phase == S4_LIFT_IDLE) {
        progress->lift_phase = S4_LIFT_RISING;
        progress->lift_since_ms = now_ms;
    } else if (now_ms - progress->lift_since_ms >= S4_LIFT_RISE_MS) {
        progress->lift_phase = S4_LIFT_ARMED;
        progress->lift_since_ms = now_ms;
    }

    const unsigned cos_sq = progress->lift_phase == S4_LIFT_ARMED ?
        S4_LIFT_COS_SQ_Q8 : S4_PICKUP_COS_SQ_Q8;
    if (!gravity_ok || (dot > 0 && (uint64_t)(uint32_t)dot * (uint32_t)dot * 256U >
        (uint64_t)reference_sq * accel_sq * cos_sq)) {
        progress->tilt_active = FALSE;
        return FALSE;
    }
    if (!progress->tilt_active) {
        progress->tilt_active = TRUE;
        progress->tilt_since_ms = now_ms;
        return FALSE;
    }
    return now_ms - progress->tilt_since_ms >= S4_PICKUP_CONFIRM_MS;
}

#if (APP_TEST || APP_S4_MOTOR_TRACE) && TM_CONSOLE_USB_CDC
static void trace_sample(uint32_t sequence, uint32_t uptime_ms,
                         const GY511_ACCEL_DATA_T *raw, const GY511_ACCEL_DATA_T *reference,
                         const S4_PICKUP_PROGRESS *pickup, BOOL pickup_latched)
{
    S2_MOTION_DATA motion;
    S3_LINE_DATA line;
    /* Leave space in the 4096-byte USB ring for other console messages. */
    if (tm_usb_state() != 3U || tm_usb_pending() > 2048U) return;
    const BOOL got_motion = subsystem2_get_motion(&motion) == E_OK;
    const BOOL got_line = subsystem3_get_line(&line) == E_OK;
    const uint32_t source_ms = get_time_ms();
    const BOOL motion_valid = got_motion && source_ms - motion.timestamp_ms <= S2_FEEDBACK_MAX_AGE_MS;
    const BOOL encoder_valid = motion_valid && motion.encoder_valid &&
        motion.count_left >= INT32_MIN && motion.count_left <= INT32_MAX &&
        motion.count_right >= INT32_MIN && motion.count_right <= INT32_MAX;
    const BOOL speed_valid = motion_valid && motion.speed_valid &&
        source_ms - motion.speed_timestamp_ms <= S2_FEEDBACK_MAX_AGE_MS;
    const BOOL line_valid = got_line && line.valid && source_ms - line.timestamp_ms <= APP_IR_MAX_AGE_MS;
    tm_printf((UB*)S4_TRACE_PREFIX "%u,%u,%d,%d,%d,%u,%d,%d,%u"
              ",%d,%d,%d,%u,%u,%u,%u,%d,%d,%u,%d,%d,%u,%u,%u\n",
              (UINT)sequence, (UINT)uptime_ms, raw->x_mg, raw->y_mg, raw->z_mg,
              (UINT)encoder_valid, encoder_valid ? (INT)motion.count_left : 0,
              encoder_valid ? (INT)motion.count_right : 0, (UINT)pickup_latched,
              reference->x_mg, reference->y_mg, reference->z_mg,
              (UINT)pickup->lift_phase, (UINT)pickup->tilt_active,
              (UINT)motion_valid, (UINT)(motion_valid && motion.command_active),
              motion_valid ? motion.duty_left_x100 : 0, motion_valid ? motion.duty_right_x100 : 0,
              (UINT)speed_valid, speed_valid ? motion.actual_left_rpm_x10 : 0,
              speed_valid ? motion.actual_right_rpm_x10 : 0,
              (UINT)line_valid, line_valid ? (UINT)line.centre_adc : 0U,
              line_valid ? (UINT)line.right_adc : 0U);
}
#endif

static void imu_hump_task(INT stacd, void *exinf)
{
    const uint32_t delay_budget_ms = S4_SAMPLE_PERIOD_MS > CNF_TIMER_PERIOD ?
        S4_SAMPLE_PERIOD_MS - CNF_TIMER_PERIOD : 0U;
    GY511_SAMPLE_T sample = {0};
    GY511_ACCEL_DATA_T calib_reference = {0};
    S2_MOTION_DATA wheel = {0};
    S4_MOTION_PROGRESS motion = {0};
    int16_t pitch_x10 = 0;
    int16_t baseline_roll_x10 = 0;
    uint32_t pitch_sample_ms = 0U;
    uint32_t last_sample_ms = 0U;
    BOOL have_pitch = FALSE;
    uint32_t current_time_ms;
    uint32_t sample_dt_ms = 0U;
    S4_PICKUP_PROGRESS pickup = {0};
    BOOL pickup_latched = FALSE;
#if (APP_TEST || APP_S4_MOTOR_TRACE) && TM_CONSOLE_USB_CDC
    uint32_t trace_sequence = 0U;
#endif
    uint16_t calib_sample_counter = 0U;
    int32_t calib_sum_x = 0, calib_sum_y = 0, calib_sum_z = 0;
    ER er;

    (void)stacd;
    (void)exinf;

    tm_printf((UB*)"[S4] IMU Hump Task started (priority=%u, period=%u ms, I2C1 GP2/GP3)\n",
              S4_PRI_IMU, S4_SAMPLE_PERIOD_MS);

    while (1) {
        const uint32_t loop_start_ms = get_operating_ms();
        BOOL advance = TRUE;
        current_time_ms = loop_start_ms;
        publish_snapshot(pitch_x10, pitch_sample_ms, get_time_ms(), have_pitch);

        if (s_state >= S4_STATE_CALIBRATING && s_state <= S4_STATE_HUMP_DESCENT) {
            er = drv_gy511_read_accel(&sample);
            current_time_ms = get_operating_ms();
            if (er == E_OK) {
                sample_dt_ms = have_pitch ? current_time_ms - last_sample_ms : 0U;
                last_sample_ms = current_time_ms;
                pitch_sample_ms = get_time_ms();
                pitch_x10 = subsystem4_calc_vehicle_pitch_x10(&sample.filtered);
                have_pitch = TRUE;

                if (!pickup_latched && s_state >= S4_STATE_MONITORING) {
                    if (sample_dt_ms > S4_PICKUP_MAX_SAMPLE_GAP_MS) pickup = (S4_PICKUP_PROGRESS){0};
                    if (pickup_update(&pickup, &calib_reference, &sample.filtered, current_time_ms)) {
                        app_controller_report_pickup();
                        pickup_latched = TRUE;
                        cancel_hump();
                        tm_printf((UB*)"[S4] PICKUP confirmed. Controller STOP latched.\n");
                    }
                }
#if (APP_TEST || APP_S4_MOTOR_TRACE) && TM_CONSOLE_USB_CDC
                if (s_state >= S4_STATE_MONITORING)
                    trace_sample(++trace_sequence, current_time_ms, &sample.raw, &calib_reference,
                                 &pickup, pickup_latched);
#endif
            } else {
                pickup = (S4_PICKUP_PROGRESS){0};
                if (er != E_BUSY || current_time_ms - last_sample_ms > S4_PICKUP_MAX_SAMPLE_GAP_MS)
                    cancel_hump();
                if (er != E_BUSY || current_time_ms - last_sample_ms >= S4_SAMPLE_MAX_AGE_MS) {
                    have_pitch = FALSE;
                    s_state = S4_STATE_FAULT;
                }
                advance = FALSE;
            }
        }

        if (advance) switch (s_state) {

        /* -----------------------------------------------------------------
         * STATE 0: INITIALIZING
         * ----------------------------------------------------------------- */
        case S4_STATE_INITIALIZING:
            er = drv_gy511_init();
            if (er == E_OK) {
                last_sample_ms = get_operating_ms();
                have_pitch = FALSE;
                s_flags |= S4_FLAG_SENSOR_OK;
                s_flags &= ~S4_FLAG_FAULT;
                calib_sample_counter = 0U;
                s_state = S4_STATE_CALIBRATING;
                tm_printf((UB*)"[S4] GY-511 sensor detected. Entering stationary calibration...\n");
            } else {
                s_flags |= S4_FLAG_FAULT;
                s_flags &= ~S4_FLAG_SENSOR_OK;
                s_state = S4_STATE_FAULT;
                tm_printf((UB*)"[S4] ERROR: GY-511 initialization failed (%d). Retrying...\n", er);
            }
            break;

        /* -----------------------------------------------------------------
         * STATE 1: CALIBRATING (Stationary Zero Reference)
         * ----------------------------------------------------------------- */
        case S4_STATE_CALIBRATING:
            if (!gravity_sample(norm_squared(&sample.raw)) || motion_during_calibration()) {
                calib_sample_counter = 0U;
                break;
            }
            if (calib_sample_counter == 0U || sample_dt_ms > S4_SAMPLE_MAX_AGE_MS ||
                s4_abs(sample.raw.x_mg - calib_reference.x_mg) > S4_CAL_MAX_AXIS_CHANGE_MG ||
                s4_abs(sample.raw.y_mg - calib_reference.y_mg) > S4_CAL_MAX_AXIS_CHANGE_MG ||
                s4_abs(sample.raw.z_mg - calib_reference.z_mg) > S4_CAL_MAX_AXIS_CHANGE_MG) {
                calib_reference = sample.raw;
                calib_sample_counter = 0U;
                calib_sum_x = calib_sum_y = calib_sum_z = 0;
            }
            calib_sum_x += sample.raw.x_mg;
            calib_sum_y += sample.raw.y_mg;
            calib_sum_z += sample.raw.z_mg;
            calib_sample_counter++;
            if (calib_sample_counter >= S4_CALIBRATION_SAMPLES) {
                /* The first sample anchors the stillness check. Use the
                 * whole still window as the resting gravity reference. */
                calib_reference = (GY511_ACCEL_DATA_T){
                    (int16_t)(calib_sum_x / (int32_t)S4_CALIBRATION_SAMPLES),
                    (int16_t)(calib_sum_y / (int32_t)S4_CALIBRATION_SAMPLES),
                    (int16_t)(calib_sum_z / (int32_t)S4_CALIBRATION_SAMPLES)
                };
                s_baseline_pitch_x10 = subsystem4_calc_vehicle_pitch_x10(&calib_reference);
                baseline_roll_x10 = drv_gy511_calc_pitch_x10(&calib_reference);
                s_roll_x10 = 0;
                s_turn_code = 0;
                s_motion_class = S4_MOTION_UNAVAILABLE;
                motion = (S4_MOTION_PROGRESS){0};
                s_state = S4_STATE_MONITORING;
                tm_printf((UB*)"[S4] Calibration complete. Baseline pitch x10=%d\n",
                          s_baseline_pitch_x10);
            }
            break;

        case S4_STATE_MONITORING:
        case S4_STATE_HUMP_ASCENT:
        case S4_STATE_HUMP_DESCENT:
        case S4_STATE_HUMP_VALIDATE: {
            const S2_MOTION_DATA *fresh = NULL;
            /* A busy mailbox leaves the prior snapshot intact. Its source
             * timestamp still limits how long the estimator may use it. */
            (void)subsystem2_get_motion(&wheel);
            const uint32_t source_ms = get_time_ms();
            if (wheel.encoder_valid && !wheel.faults &&
                source_ms - wheel.timestamp_ms <= S4_ENCODER_MAX_AGE_MS)
                fresh = &wheel;
            update_wheel_motion(&motion, fresh, source_ms);
            s_roll_x10 = drv_gy511_calc_pitch_x10(&sample.filtered) - baseline_roll_x10;
            if (!pickup_latched)
                terrain_update(gravity_sample(norm_squared(&sample.filtered)) ? fresh : NULL,
                    (int16_t)(pitch_x10 - s_baseline_pitch_x10), current_time_ms, sample_dt_ms);
            report_motion(&motion, pickup_latched, current_time_ms,
                          norm_squared(&sample.raw), norm_squared(&calib_reference));
            break;
        }

        /* -----------------------------------------------------------------
         * STATE 6: FAULT / RECOVERY
         * ----------------------------------------------------------------- */
        case S4_STATE_FAULT:
            s_flags |= S4_FLAG_FAULT;
            s_flags &= ~S4_FLAG_SENSOR_OK;
            s_flags &= ~S4_FLAG_HUMP_ACTIVE;

            /* Attempt sensor recovery */
            tk_dly_tsk(500); /* Sleep during retry backoff; do not busy-wait. */
            er = drv_gy511_init();
            if (er == E_OK) {
                tm_printf((UB*)"[S4] SENSOR RECOVERED. Re-calibrating...\n");
                last_sample_ms = get_operating_ms();
                have_pitch = FALSE;
                calib_sample_counter = 0U;
                s_flags &= ~S4_FLAG_FAULT;
                s_flags |= S4_FLAG_SENSOR_OK;
                s_state = S4_STATE_CALIBRATING;
            }
            break;

        default:
            s_state = S4_STATE_INITIALIZING;
            break;
        }

        publish_snapshot(pitch_x10, pitch_sample_ms, get_time_ms(), have_pitch);

        current_time_ms = get_operating_ms() - loop_start_ms;
        /* The kernel adds one tick to a positive relative delay. Yield
         * after an overrun so this task never samples in a tight loop. */
        (void)tk_dly_tsk(current_time_ms < delay_budget_ms ?
            delay_budget_ms - current_time_ms : 1U);
    }
}

/* -------------------------------------------------------------------------
 * Public Interface Functions
 * ------------------------------------------------------------------------- */

EXPORT ER subsystem4_init(void)
{
    T_CSEM csem = {0};

    /* Create binary semaphore protecting telemetry data */
    csem.sematr = TA_TFIFO | TA_FIRST;
    csem.isemcnt = 1;
    csem.maxsem = 1;

    s_sem_telemetry = tk_cre_sem(&csem);
    if (s_sem_telemetry < E_OK) {
        CHECK(s_sem_telemetry, "S4 tk_cre_sem");
        return s_sem_telemetry;
    }

    publish_snapshot(0, 0, get_time_ms(), FALSE);

    return E_OK;
}

EXPORT ER subsystem4_start(void)
{
    ID tid;

    /* Start imu_hump_task on core 0 with priority S4_PRI_IMU */
    tid = app_start_task((FP)imu_hump_task, S4_PRI_IMU, TP_PRC1, "imu_hump_task");
    return (tid < E_OK) ? tid : E_OK;
}

EXPORT ER subsystem4_get_telemetry(HUMP_TELEMETRY_T *p_telemetry)
{
    ER er;

    if (p_telemetry == NULL) {
        return E_PAR;
    }

    if (s_sem_telemetry <= 0) return E_OBJ;
    /* Nonblocking coherent read; only this published buffer is shared. */
    er = tk_wai_sem(s_sem_telemetry, 1, TMO_POL);
    if (er != E_OK) {
        return er;
    }

    {
        unsigned pitch = snapshot_get(0, 11);
        unsigned flags = snapshot_get(39, 6);
        p_telemetry->pitch_deg_x10 = pitch ? (int16_t)((int)pitch - 901) : 0;
        p_telemetry->baseline_pitch_x10 = (int16_t)((int)snapshot_get(11, 11) - 900);
        p_telemetry->current_hump_mm = (uint16_t)snapshot_get(22, 7);
        p_telemetry->max_hump_mm = (uint16_t)snapshot_get(29, 7);
        p_telemetry->state = (uint8_t)snapshot_get(36, 3);
        p_telemetry->flags = (uint8_t)((flags & 15U) | ((flags & 16U) << 1) | ((flags & 32U) << 2));
        if (pitch && (uint32_t)(get_time_ms() - snapshot_get(109, 32)) <= S4_SAMPLE_MAX_AGE_MS)
            p_telemetry->flags |= S4_FLAG_PITCH_VALID;
        p_telemetry->traversal_time_ms = snapshot_get(45, 32);
        p_telemetry->timestamp_ms = snapshot_get(77, 32);
        p_telemetry->pitch_timestamp_ms = snapshot_get(109, 32);
        for (unsigned i = 0; i < sizeof(p_telemetry->terrain); ++i)
            p_telemetry->terrain[i] = s_snapshot[18 + i];
        if (!(p_telemetry->flags & S4_FLAG_PITCH_VALID)) {
            p_telemetry->terrain[0] = p_telemetry->terrain[1] = p_telemetry->terrain[2] = 0;
        }
        /* A reset is visible immediately; the owner applies it next loop. */
        if (s_requests & 1U) {
            p_telemetry->current_hump_mm = p_telemetry->max_hump_mm = 0U;
            p_telemetry->traversal_time_ms = 0U;
            p_telemetry->terrain[3] &= (uint8_t)~3U;
            p_telemetry->flags &= (uint8_t)~(S4_FLAG_MAX_UPDATED | S4_FLAG_NEW_HUMP_EVENT |
                                            S4_FLAG_ENCODER_HEIGHT | S4_FLAG_HUMP_ACTIVE);
            if (p_telemetry->state >= S4_STATE_HUMP_ASCENT &&
                p_telemetry->state <= S4_STATE_HUMP_VALIDATE)
                p_telemetry->state = S4_STATE_MONITORING;
        }
    }

    (void)tk_sig_sem(s_sem_telemetry, 1);
    return E_OK;
}

EXPORT ER subsystem4_reset_run(void)
{
    /*
     * INTERFACE IF-08: Explicit new-run reset.
     * Complies with Decision NC3: clears the recorded maximum for a new trial.
     */
    ER er;
    if (s_sem_telemetry <= 0) return E_OBJ;
    er = tk_wai_sem(s_sem_telemetry, 1, TMO_FEVR);
    if (er != E_OK) return er;
    s_requests |= 1U;

    (void)tk_sig_sem(s_sem_telemetry, 1);

    tm_printf((UB*)"[S4] RUN RESET: Max hump height cleared to 0 mm for new run.\n");
    return E_OK;
}
