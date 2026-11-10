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
#include <sys/sysdef.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

/* Bench diagnostics: set to 1 to print latest readings once a second. */
#define S4_STATUS_LOG_ENABLED       0
#define S4_LOG_PERIOD_MS            (1000U)

#define s4_abs(x)                   (((x) < 0) ? -(x) : (x))

/* Module state variables */
static S4_STATE_E s_state = S4_STATE_INITIALIZING;
static int16_t s_baseline_pitch_x10 = 0;
static uint16_t s_current_hump_mm = 0U;
static uint16_t s_max_hump_mm = 0U;
static uint16_t s_vehicle_speed_mm_s = S4_DEFAULT_SPEED_MM_S;
static uint8_t s_flags = 0U;

/* Hump integration accumulation in micrometers (um) for integer precision */
static uint32_t s_accumulated_height_um = 0U;
static uint32_t s_hump_start_time_ms = 0U;
static uint32_t s_last_traversal_duration_ms = 0U;

/* RTOS Synchronization: Semaphore protecting telemetry snapshot */
static ID s_sem_telemetry;
/* Published fields only: pitch:11, baseline:11, heights:7+7, state:3,
 * flags:6, traversal:32, publication time:32, pitch sample time:32.
 * 141 logical bits round up to 18 bytes. Runtime estimator state above
 * remains private to the IMU task; readers never race its intermediate writes. */
static uint8_t s_snapshot[18];
/* Shared request bits: reset:1, speed update:1; semaphore-protected RMW. */
static uint8_t s_requests;
static uint16_t s_requested_speed;

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
 * Helper: Retrieve current kernel uptime in milliseconds.
 */
static uint32_t get_time_ms(void)
{
    SYSTIM tim;
    (void)tk_get_tim(&tim);
    return (uint32_t)tim.lo;
}

/* Only the owner calls this. No sensor I/O or wait while holding the lock.
 * If a reader owns the semaphore, retain the old snapshot and retry next loop. */
static void publish_snapshot(int16_t pitch, uint32_t pitch_time, uint32_t now,
                              BOOL have_pitch)
{
    unsigned compact_flags;
    BOOL pitch_valid;
    if (tk_wai_sem(s_sem_telemetry, 1, TMO_POL) != E_OK) return;
    if (s_requests & 1U) {
        s_current_hump_mm = s_max_hump_mm = 0U;
        s_accumulated_height_um = 0U;
        s_flags &= (uint8_t)~(S4_FLAG_MAX_UPDATED | S4_FLAG_NEW_HUMP_EVENT);
        s_requests &= (uint8_t)~1U;
    }
    if (s_requests & 2U) {
        s_vehicle_speed_mm_s = s_requested_speed;
        s_flags |= S4_FLAG_SPEED_SUPPLIED;
        s_requests &= (uint8_t)~2U;
    }
    if (s_state == S4_STATE_FAULT) {
        s_flags |= S4_FLAG_FAULT;
        s_flags &= (uint8_t)~(S4_FLAG_SENSOR_OK | S4_FLAG_HUMP_ACTIVE);
    }
    pitch_valid = have_pitch && (uint32_t)(now - pitch_time) <= 100U &&
        s_state >= S4_STATE_MONITORING && s_state <= S4_STATE_HUMP_VALIDATE &&
        (s_flags & S4_FLAG_SENSOR_OK) && !(s_flags & S4_FLAG_FAULT) &&
        pitch >= -900 && pitch <= 900;
    compact_flags = (s_flags & 15U) | ((s_flags & S4_FLAG_SPEED_SUPPLIED) >> 1) |
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

static void imu_hump_task(INT stacd, void *exinf)
{
    GY511_ACCEL_DATA_T accel_data = {0};
    int16_t pitch_x10 = 0;
    uint32_t pitch_sample_ms = 0U;
    BOOL have_pitch = FALSE;
    int16_t delta_pitch_x10;
    uint32_t current_time_ms;
    uint32_t log_elapsed_ms = S4_LOG_PERIOD_MS;
    uint16_t calib_sample_counter = 0U;
    int32_t calib_sum_x10 = 0;
    uint32_t height_mm;
    ER er;

    (void)stacd;
    (void)exinf;

    tm_printf((UB*)"[S4] IMU Hump Task started (priority=%u, period=%u ms, I2C1 GP2/GP3)\n",
              S4_PRI_IMU, S4_SAMPLE_PERIOD_MS);

    while (1) {
        current_time_ms = get_time_ms();
        publish_snapshot(pitch_x10, pitch_sample_ms, current_time_ms, have_pitch);

        switch (s_state) {

        /* -----------------------------------------------------------------
         * STATE 0: INITIALIZING
         * ----------------------------------------------------------------- */
        case S4_STATE_INITIALIZING:
            er = drv_gy511_init();
            if (er == E_OK) {
                s_flags |= S4_FLAG_SENSOR_OK;
                s_flags &= ~S4_FLAG_FAULT;
                calib_sample_counter = 0U;
                calib_sum_x10 = 0;
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
            er = drv_gy511_read_accel(&accel_data);
            if (er == E_OK) {
                pitch_x10 = subsystem4_calc_vehicle_pitch_x10(&accel_data);
                pitch_sample_ms = get_time_ms();
                have_pitch = TRUE;
                calib_sum_x10 += (int32_t)pitch_x10;
                calib_sample_counter++;

                if (calib_sample_counter >= S4_CALIBRATION_SAMPLES) {
                    s_baseline_pitch_x10 = (int16_t)(calib_sum_x10 / (int32_t)S4_CALIBRATION_SAMPLES);
                    s_state = S4_STATE_MONITORING;
                    tm_printf((UB*)"[S4] Calibration complete. Baseline pitch: %d.%d deg\n",
                              s_baseline_pitch_x10 / 10,
                              (int)s4_abs(s_baseline_pitch_x10 % 10));
                }
            } else {
                s_state = S4_STATE_FAULT;
            }
            break;

        /* -----------------------------------------------------------------
         * STATE 2: MONITORING (Normal Level Operation)
         * ----------------------------------------------------------------- */
        case S4_STATE_MONITORING:
            er = drv_gy511_read_accel(&accel_data);
            if (er != E_OK) {
                s_state = S4_STATE_FAULT;
                break;
            }

            pitch_x10 = subsystem4_calc_vehicle_pitch_x10(&accel_data);
            pitch_sample_ms = get_time_ms();
            have_pitch = TRUE;
            delta_pitch_x10 = (int16_t)(pitch_x10 - s_baseline_pitch_x10);

            /* Check for upward slope / hump ascent */
            if (delta_pitch_x10 >= S4_TILT_ENTER_THRESH_X10) {
                s_hump_start_time_ms = current_time_ms;
                s_accumulated_height_um = 0U;
                s_flags |= S4_FLAG_HUMP_ACTIVE;
                s_flags &= ~S4_FLAG_NEW_HUMP_EVENT;
                s_flags &= ~S4_FLAG_MAX_UPDATED;
                s_state = S4_STATE_HUMP_ASCENT;

                tm_printf((UB*)"[S4] HUMP DETECTED! Ascent pitch: %d.%d deg (start=%u ms)\n",
                          delta_pitch_x10 / 10, (int)s4_abs(delta_pitch_x10 % 10), current_time_ms);
            }
            break;

        /* -----------------------------------------------------------------
         * STATE 3: HUMP ASCENT (Front Climbing Upwards)
         * ----------------------------------------------------------------- */
        case S4_STATE_HUMP_ASCENT:
            er = drv_gy511_read_accel(&accel_data);
            if (er != E_OK) {
                s_state = S4_STATE_FAULT;
                break;
            }

            pitch_x10 = subsystem4_calc_vehicle_pitch_x10(&accel_data);
            pitch_sample_ms = get_time_ms();
            have_pitch = TRUE;
            delta_pitch_x10 = (int16_t)(pitch_x10 - s_baseline_pitch_x10);

            /*
             * Fixed-point vertical height integration:
             * dh_um = speed (mm/s) * sin(theta) * delta_t (seconds) * 1000
             * For small angles, sin(theta) ~ theta_rad = theta_x10 / 573.0
             * dh_um = (speed * theta_x10 * 20 * 1000) / (573 * 1000) = (speed * theta_x10 * 100) / 2865
             */
            if (delta_pitch_x10 > 0) {
                uint32_t dh_um = ((uint32_t)s_vehicle_speed_mm_s * (uint32_t)delta_pitch_x10 * 100U) / 2865U;
                s_accumulated_height_um += dh_um;
            }

            /* Transition to descent when vehicle tilts downward past threshold */
            if (delta_pitch_x10 <= S4_TILT_EXIT_THRESH_X10) {
                s_state = S4_STATE_HUMP_DESCENT;
            }

            /* Timeout guard */
            if ((current_time_ms - s_hump_start_time_ms) > S4_MAX_TRAVERSAL_MS) {
                tm_printf((UB*)"[S4] WARNING: Hump traversal timeout (>%u ms). Resetting to MONITOR.\n",
                          S4_MAX_TRAVERSAL_MS);
                s_flags &= ~S4_FLAG_HUMP_ACTIVE;
                s_state = S4_STATE_MONITORING;
            }
            break;

        /* -----------------------------------------------------------------
         * STATE 4: HUMP DESCENT (Rear Wheels Climbing, Front Going Down)
         * ----------------------------------------------------------------- */
        case S4_STATE_HUMP_DESCENT:
            er = drv_gy511_read_accel(&accel_data);
            if (er != E_OK) {
                s_state = S4_STATE_FAULT;
                break;
            }

            pitch_x10 = subsystem4_calc_vehicle_pitch_x10(&accel_data);
            pitch_sample_ms = get_time_ms();
            have_pitch = TRUE;
            delta_pitch_x10 = (int16_t)(pitch_x10 - s_baseline_pitch_x10);

            /* When chassis settles back to level, transition to validate */
            if (s4_abs(delta_pitch_x10) <= S4_TILT_FLAT_THRESH_X10) {
                s_last_traversal_duration_ms = current_time_ms - s_hump_start_time_ms;
                s_state = S4_STATE_HUMP_VALIDATE;
            }

            /* Timeout guard */
            if ((current_time_ms - s_hump_start_time_ms) > S4_MAX_TRAVERSAL_MS) {
                s_flags &= ~S4_FLAG_HUMP_ACTIVE;
                s_state = S4_STATE_MONITORING;
            }
            break;

        /* -----------------------------------------------------------------
         * STATE 5: HUMP VALIDATE & NC3 MAXIMUM RETENTION
         * ----------------------------------------------------------------- */
        case S4_STATE_HUMP_VALIDATE:
            s_flags &= ~S4_FLAG_HUMP_ACTIVE;
            height_mm = (s_accumulated_height_um + 500U) / 1000U;

            /* Validate physical plausibility */
            if ((s_last_traversal_duration_ms >= S4_MIN_TRAVERSAL_MS) &&
                (s_last_traversal_duration_ms <= S4_MAX_TRAVERSAL_MS) &&
                (height_mm >= S4_MIN_VALID_HUMP_MM) &&
                (height_mm <= S4_MAX_VALID_HUMP_MM)) {

                s_current_hump_mm = (uint16_t)height_mm;
                s_flags |= S4_FLAG_NEW_HUMP_EVENT;

                /*
                 * DECISION NC3 ENFORCEMENT:
                 * Only valid, strictly higher hump estimates update the run maximum.
                 * Temporary stops and sensor recovery preserve it.
                 */
                if (s_current_hump_mm > s_max_hump_mm) {
                    s_max_hump_mm = s_current_hump_mm;
                    s_flags |= S4_FLAG_MAX_UPDATED;
                }

                tm_printf((UB*)"[S4] VALID HUMP: Height=%u mm (duration=%u ms) | Run Max=%u mm\n",
                          s_current_hump_mm, s_last_traversal_duration_ms, s_max_hump_mm);
            } else {
                tm_printf((UB*)"[S4] REJECTED HUMP: noise/invalid (height=%u mm, duration=%u ms)\n",
                          (UINT)height_mm, s_last_traversal_duration_ms);
            }

            s_state = S4_STATE_MONITORING;
            break;

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
                calib_sample_counter = 0U;
                calib_sum_x10 = 0;
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

        /* Periodic diagnostic heartbeat */
        if (S4_STATUS_LOG_ENABLED && log_elapsed_ms >= S4_LOG_PERIOD_MS) {
            /* Last successful sample; retained if a later read fails.
             * Previously Pitch displayed the fixed calibration baseline. */
            tm_printf((UB*)"[S4 Status] State=%d | Pitch=%s%d.%d deg | Baseline=%s%d.%d deg | Accel=%d,%d,%d mg | CurrentHump=%u mm | RunMax=%u mm | Flags=0x%02X\n",
                      s_state,
                      pitch_x10 < 0 ? "-" : "",
                      (int)s4_abs(pitch_x10) / 10,
                      (int)s4_abs(pitch_x10 % 10),
                      s_baseline_pitch_x10 < 0 ? "-" : "",
                      (int)s4_abs(s_baseline_pitch_x10) / 10,
                      (int)s4_abs(s_baseline_pitch_x10 % 10),
                      (int)accel_data.x_mg,
                      (int)accel_data.y_mg,
                      (int)accel_data.z_mg,
                      s_current_hump_mm,
                      s_max_hump_mm,
                      s_flags);
            log_elapsed_ms = 0U;
        }

        tk_dly_tsk(S4_SAMPLE_PERIOD_MS);
        if (S4_STATUS_LOG_ENABLED) log_elapsed_ms += S4_SAMPLE_PERIOD_MS;
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
        if (pitch && (uint32_t)(get_time_ms() - snapshot_get(109, 32)) <= 100U)
            p_telemetry->flags |= S4_FLAG_PITCH_VALID;
        p_telemetry->traversal_time_ms = snapshot_get(45, 32);
        p_telemetry->timestamp_ms = snapshot_get(77, 32);
        p_telemetry->pitch_timestamp_ms = snapshot_get(109, 32);
        /* A reset is visible immediately; the owner applies it next loop. */
        if (s_requests & 1U) {
            p_telemetry->current_hump_mm = p_telemetry->max_hump_mm = 0U;
            p_telemetry->flags &= (uint8_t)~(S4_FLAG_MAX_UPDATED | S4_FLAG_NEW_HUMP_EVENT);
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

EXPORT void subsystem4_set_speed_mm_s(uint16_t speed_mm_s)
{
    if (s_sem_telemetry <= 0 || tk_wai_sem(s_sem_telemetry, 1, TMO_FEVR) != E_OK) return;
    s_requested_speed = speed_mm_s;
    s_requests |= 2U;
    (void)tk_sig_sem(s_sem_telemetry, 1);
}
