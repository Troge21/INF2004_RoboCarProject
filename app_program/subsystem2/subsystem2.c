/* S2 owns PWM, encoder feedback and wheel-speed control. No networking.
 * GP8/9 left, GP10/11 right; encoders A/B left16/17, right0/1. */
#include "subsystem2.h"
#include "motion_control.h"
#include "odometry.h"
#include "../telemetry_sources.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

#define S2_PWM_TOP 6249U
#define S2_PWM_REG(slice, offset) (PWM_BASE + (slice) * 0x14U + (offset))
#define S2_DRIVE_ENABLED (!APP_TEST || (S2_ENCODER_CALIBRATION && S2_ENCODER_POWERED_TEST))
#define S2_PASSIVE_FEEDBACK (APP_TEST && APP_BENCH_MODE == 0)
#define S2_FEEDBACK_ENABLED (S2_DRIVE_ENABLED || S2_PASSIVE_FEEDBACK)

static ID s_command_sem, s_feedback_sem;
static BOOL s_motor_ready, s_started;
#define S2_COMMAND_TARGET_BITS 11U
#define S2_COMMAND_TARGET_MASK ((1U << S2_COMMAND_TARGET_BITS) - 1U)
#define S2_COMMAND_TARGET_BIAS (1 << (S2_COMMAND_TARGET_BITS - 1U))
#define S2_COMMAND_MODE_SHIFT (2U * S2_COMMAND_TARGET_BITS)
_Static_assert(S2_MAX_RPM_X10 < S2_COMMAND_TARGET_BIAS, "Command target out of range");
_Static_assert(S2_MODE_RPM < 3, "Command mode out of range");
/* Under s_command_sem: little-endian time:32, biased left/right:11 each,
 * mode:2. Mode zero means empty; populated modes store S2_CONTROL_MODE + 1. */
static uint8_t s_command[7];

/* One semaphore protects both motion and path at the same source tick.
 * Controls in order: targets 11+11, duties 14+14, validity/activity 3,
 * mode 2 (zero for no publication), stop reason 3, fault mask 6. */
#define S2_FEEDBACK_DUTY_BITS 14U
#define S2_FEEDBACK_DUTY_MASK ((1U << S2_FEEDBACK_DUTY_BITS) - 1U)
enum {
    S2_FEEDBACK_LEFT_DUTY = 2 * S2_COMMAND_TARGET_BITS,
    S2_FEEDBACK_RIGHT_DUTY = S2_FEEDBACK_LEFT_DUTY + S2_FEEDBACK_DUTY_BITS,
    S2_FEEDBACK_ENCODER_VALID = S2_FEEDBACK_RIGHT_DUTY + S2_FEEDBACK_DUTY_BITS,
    S2_FEEDBACK_SPEED_VALID = S2_FEEDBACK_ENCODER_VALID + 1,
    S2_FEEDBACK_COMMAND_ACTIVE = S2_FEEDBACK_SPEED_VALID + 1,
    S2_FEEDBACK_MODE = S2_FEEDBACK_COMMAND_ACTIVE + 1,
    S2_FEEDBACK_STOP_REASON = S2_FEEDBACK_MODE + 2,
    S2_FEEDBACK_FAULTS = S2_FEEDBACK_STOP_REASON + 3
};
_Static_assert(S2_PID_MAX_DUTY_X100 + 10000U <= S2_FEEDBACK_DUTY_MASK,
               "Feedback duty out of range");
_Static_assert(S2_STOP_FEEDBACK_WAIT < 8, "Feedback stop reason out of range");
_Static_assert((S2_FAULT_ENCODER | S2_FAULT_TIMING | S2_FAULT_LEFT_STALL |
                S2_FAULT_RIGHT_STALL | S2_FAULT_LEFT_DIRECTION |
                S2_FAULT_RIGHT_DIRECTION) < (UINT64_C(1) << (64 - S2_FEEDBACK_FAULTS)),
               "Feedback faults out of range");
typedef struct {
    int64_t count_left, count_right;
    int64_t distance_left_mm_x10, distance_right_mm_x10;
    uint64_t controls;
    UW sequence, timestamp_ms, speed_timestamp_ms, speed_period_ms;
    INT actual_left_rpm_x10, actual_right_rpm_x10, speed_centre_mm_s_x10;
    uint32_t path_mm;
} S2_FEEDBACK_RECORD;
static S2_FEEDBACK_RECORD s_feedback;

static uint32_t s2_read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void s2_write32(uint8_t *p, uint32_t n)
{
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8U * i));
}

static ER submit_command(S2_CONTROL_MODE mode, INT left, INT right)
{
    SYSTIM now;
    ER er;
    uint32_t word;
    if (s_command_sem <= 0) return E_OBJ;
    er = tk_wai_sem(s_command_sem, 1, TMO_POL);
    if (er != E_OK) return er;
    if (!s_motor_ready) {
        (void)tk_sig_sem(s_command_sem, 1);
        return E_OBJ;
    }
    (void)tk_get_tim(&now);
    if (left == 0 && right == 0) mode = S2_MODE_STOP;
    word = (uint32_t)(left + S2_COMMAND_TARGET_BIAS) |
           (uint32_t)(right + S2_COMMAND_TARGET_BIAS) << S2_COMMAND_TARGET_BITS |
           ((uint32_t)mode + 1U) << S2_COMMAND_MODE_SHIFT;
    s2_write32(s_command, (UW)now.lo);
    s_command[4] = (uint8_t)word;
    s_command[5] = (uint8_t)(word >> 8);
    s_command[6] = (uint8_t)(word >> 16);
    (void)tk_sig_sem(s_command_sem, 1);
    return E_OK;
}

EXPORT ER subsystem2_set_duty(INT left, INT right)
{
    if (left < 0 || left > 100 || right < 0 || right > 100) return E_PAR;
    return submit_command(S2_MODE_DUTY, left, right);
}

EXPORT ER subsystem2_set_rpm_x10(INT left, INT right)
{
    if (left < 0 || right < 0) return E_PAR;
    return subsystem2_set_signed_rpm_x10(left, right);
}

EXPORT ER subsystem2_set_signed_rpm_x10(INT left, INT right)
{
    if (left < -S2_MAX_RPM_X10 || left > S2_MAX_RPM_X10 ||
        right < -S2_MAX_RPM_X10 || right > S2_MAX_RPM_X10) return E_PAR;
    return submit_command(S2_MODE_RPM, left, right);
}

static ER read_motion(S2_MOTION_DATA *sample, INT wait_ms)
{
    ER er;
    uint64_t controls;
    if (sample == NULL) return E_PAR;
    if (s_feedback_sem <= 0) return E_OBJ;
    er = tk_wai_sem(s_feedback_sem, 1, wait_ms);
    if (er != E_OK) return er;
    controls = s_feedback.controls;
    if (!controls) {
        (void)tk_sig_sem(s_feedback_sem, 1);
        return E_OBJ;
    }
    sample->count_left = s_feedback.count_left;
    sample->count_right = s_feedback.count_right;
    sample->distance_left_mm_x10 = s_feedback.distance_left_mm_x10;
    sample->distance_right_mm_x10 = s_feedback.distance_right_mm_x10;
    sample->sequence = s_feedback.sequence;
    sample->timestamp_ms = s_feedback.timestamp_ms;
    sample->speed_timestamp_ms = s_feedback.speed_timestamp_ms;
    sample->speed_period_ms = s_feedback.speed_period_ms;
    sample->actual_left_rpm_x10 = s_feedback.actual_left_rpm_x10;
    sample->actual_right_rpm_x10 = s_feedback.actual_right_rpm_x10;
    sample->speed_centre_mm_s_x10 = s_feedback.speed_centre_mm_s_x10;
    (void)tk_sig_sem(s_feedback_sem, 1);

    sample->target_left_rpm_x10 = (INT)(controls & S2_COMMAND_TARGET_MASK) - S2_COMMAND_TARGET_BIAS;
    sample->target_right_rpm_x10 =
        (INT)((controls >> S2_COMMAND_TARGET_BITS) & S2_COMMAND_TARGET_MASK) - S2_COMMAND_TARGET_BIAS;
    sample->duty_left_x100 =
        (INT)((controls >> S2_FEEDBACK_LEFT_DUTY) & S2_FEEDBACK_DUTY_MASK) - S2_PID_MAX_DUTY_X100;
    sample->duty_right_x100 =
        (INT)((controls >> S2_FEEDBACK_RIGHT_DUTY) & S2_FEEDBACK_DUTY_MASK) - S2_PID_MAX_DUTY_X100;
    sample->encoder_valid = (controls >> S2_FEEDBACK_ENCODER_VALID) & 1U;
    sample->speed_valid = (controls >> S2_FEEDBACK_SPEED_VALID) & 1U;
    sample->command_active = (controls >> S2_FEEDBACK_COMMAND_ACTIVE) & 1U;
    sample->mode = (S2_CONTROL_MODE)(((controls >> S2_FEEDBACK_MODE) & 3U) - 1U);
    sample->stop_reason = (S2_STOP_REASON)((controls >> S2_FEEDBACK_STOP_REASON) & 7U);
    sample->faults = (UW)(controls >> S2_FEEDBACK_FAULTS);
    sample->error_left_rpm_x10 = sample->target_left_rpm_x10 != 0 ?
        sample->target_left_rpm_x10 - sample->actual_left_rpm_x10 : 0;
    sample->error_right_rpm_x10 = sample->target_right_rpm_x10 != 0 ?
        sample->target_right_rpm_x10 - sample->actual_right_rpm_x10 : 0;
    sample->distance_centre_mm_x10 = (sample->distance_left_mm_x10 + sample->distance_right_mm_x10) / 2;
    return E_OK;
}

EXPORT ER subsystem2_get_motion(S2_MOTION_DATA *sample)
{
    return read_motion(sample, TMO_POL);
}

EXPORT ER subsystem2_get_motion_for_control(S2_MOTION_DATA *sample)
{
    return read_motion(sample, S2_CONTROL_FEEDBACK_WAIT_MS);
}

int telemetry_get_travel(TELEMETRY_TRAVEL *out)
{
    ER er;
    if (out == NULL) return E_PAR;
    if (s_feedback_sem <= 0) return E_OBJ;
    er = tk_wai_sem(s_feedback_sem, 1, TMO_POL);
    if (er != E_OK) return er;
    er = s_feedback.controls && s_feedback.path_mm != UINT32_MAX ? E_OK : E_OBJ;
    if (er == E_OK) {
        out->distance_mm = s_feedback.path_mm;
        out->timestamp_ms = s_feedback.timestamp_ms;
    }
    (void)tk_sig_sem(s_feedback_sem, 1);
    return er;
}

#if S2_FEEDBACK_ENABLED
static void publish_feedback(const S2_MOTION_DATA *sample, uint32_t path_mm)
{
    s_feedback.count_left = sample->count_left;
    s_feedback.count_right = sample->count_right;
    s_feedback.distance_left_mm_x10 = sample->distance_left_mm_x10;
    s_feedback.distance_right_mm_x10 = sample->distance_right_mm_x10;
    s_feedback.sequence = sample->sequence;
    s_feedback.timestamp_ms = sample->timestamp_ms;
    s_feedback.speed_timestamp_ms = sample->speed_timestamp_ms;
    s_feedback.speed_period_ms = sample->speed_period_ms;
    s_feedback.actual_left_rpm_x10 = sample->actual_left_rpm_x10;
    s_feedback.actual_right_rpm_x10 = sample->actual_right_rpm_x10;
    s_feedback.speed_centre_mm_s_x10 = sample->speed_centre_mm_s_x10;
    s_feedback.path_mm = path_mm;
    s_feedback.controls = (uint64_t)(sample->target_left_rpm_x10 + S2_COMMAND_TARGET_BIAS) |
        (uint64_t)(sample->target_right_rpm_x10 + S2_COMMAND_TARGET_BIAS) << S2_COMMAND_TARGET_BITS |
        (uint64_t)(sample->duty_left_x100 + S2_PID_MAX_DUTY_X100) << S2_FEEDBACK_LEFT_DUTY |
        (uint64_t)(sample->duty_right_x100 + S2_PID_MAX_DUTY_X100) << S2_FEEDBACK_RIGHT_DUTY |
        (uint64_t)(sample->encoder_valid != 0) << S2_FEEDBACK_ENCODER_VALID |
        (uint64_t)(sample->speed_valid != 0) << S2_FEEDBACK_SPEED_VALID |
        (uint64_t)(sample->command_active != 0) << S2_FEEDBACK_COMMAND_ACTIVE |
        ((uint64_t)sample->mode + 1U) << S2_FEEDBACK_MODE |
        (uint64_t)sample->stop_reason << S2_FEEDBACK_STOP_REASON |
        (uint64_t)sample->faults << S2_FEEDBACK_FAULTS;
}

static S2_COMMAND read_command(void)
{
    uint32_t word = (uint32_t)s_command[4] | (uint32_t)s_command[5] << 8 |
                    (uint32_t)s_command[6] << 16;
    unsigned mode = word >> S2_COMMAND_MODE_SHIFT;
    if (mode == 0U) return (S2_COMMAND){0};
    return (S2_COMMAND){
        .mode = (S2_CONTROL_MODE)(mode - 1U),
        .left = (INT)(word & S2_COMMAND_TARGET_MASK) - S2_COMMAND_TARGET_BIAS,
        .right = (INT)((word >> S2_COMMAND_TARGET_BITS) & S2_COMMAND_TARGET_MASK) -
                 S2_COMMAND_TARGET_BIAS,
        .timestamp_ms = s2_read32(s_command),
        .valid = TRUE
    };
}

#if S2_DRIVE_ENABLED
static void motor_duty(UINT a, UINT b, INT duty_x100, BOOL reversed)
{
    const BOOL drive_b = (reversed != 0) != (duty_x100 < 0);
    const UW magnitude = (UW)(duty_x100 < 0 ? -duty_x100 : duty_x100);
    const UW compare = ((S2_PWM_TOP + 1U) * magnitude) / 10000U;
    (void)pwm_set_cc(drive_b ? a : b, 0);
    (void)pwm_set_cc(drive_b ? b : a, compare);
}
#endif

static void motion_loop(void)
{
    S2_CONTROL_STATE control;
    S2_ODOMETER odometer;
    S2_ENCODER_COUNTS counts = {0, 0};
    SYSTIM now;
    ER encoder_er = s2_encoder_start();
    if (encoder_er == E_OK) encoder_er = s2_encoder_read(&counts);
    s2_odometer_init(&odometer);
    if (encoder_er != E_OK) s2_odometer_invalidate(&odometer);
    (void)tk_get_tim(&now);
    s2_control_init(&control, (UW)now.lo, counts);
#if S2_DRIVE_ENABLED
    if (tk_wai_sem(s_command_sem, 1, TMO_FEVR) == E_OK) {
        s_motor_ready = TRUE;
        (void)tk_sig_sem(s_command_sem, 1);
    }
#endif
    for (;;) {
        S2_COMMAND command = {0};
        /* Let a writer finish instead of braking for a brief lock overlap.
         * A timeout still cuts drive and the original command lease applies. */
        const INT command_wait = S2_DRIVE_ENABLED ? S2_COMMAND_READ_WAIT_MS : TMO_POL;
        BOOL available = tk_wai_sem(s_command_sem, 1, command_wait) == E_OK;
        if (available) {
            command = read_command();
            (void)tk_sig_sem(s_command_sem, 1);
        }
        (void)tk_get_tim(&now);
        if (encoder_er == E_OK) encoder_er = s2_encoder_read(&counts);
        if (encoder_er == E_OK)
            s2_odometer_step(&odometer,
                s2_count_delta(counts.left, control.previous.left),
                s2_count_delta(counts.right, control.previous.right),
                S2_LEFT_COUNTS_PER_REV_X10, S2_RIGHT_COUNTS_PER_REV_X10,
                S2_WHEEL_DIAMETER_MM);
        else s2_odometer_invalidate(&odometer);
        s2_control_step(&control, (UW)now.lo, counts, &command, available, encoder_er == E_OK);
#if S2_DRIVE_ENABLED
        motor_duty(8, 9, control.data.duty_left_x100, S2_LEFT_REVERSED);
        motor_duty(10, 11, control.data.duty_right_x100, S2_RIGHT_REVERSED);
#endif
        if (tk_wai_sem(s_feedback_sem, 1, TMO_POL) == E_OK) {
            publish_feedback(&control.data, s2_odometer_mm(&odometer));
            (void)tk_sig_sem(s_feedback_sem, 1);
        }
        (void)tk_dly_tsk(S2_PERIOD_MS);
    }
}
#if S2_DRIVE_ENABLED
static void motor_task(INT stacd, void *exinf)
#else
static void encoder_feedback_task(INT stacd, void *exinf)
#endif
{
    (void)stacd; (void)exinf;
    motion_loop();
}
#endif

EXPORT ER subsystem2_init(void)
{
    UINT pin;
    T_CSEM sem = {0};
    if (s_command_sem > 0) return E_OBJ;
    s_motor_ready = s_started = FALSE;
    s_command[6] = 0; /* Clear the mode to mark the mailbox empty. */
    s_feedback.controls = 0;
    sem.sematr = TA_TFIFO | TA_FIRST;
    sem.isemcnt = sem.maxsem = 1;
    /* Preload low before selecting SIO or enabling any PWM slice. */
    for (pin = 8; pin <= 11; ++pin) {
        (void)gpio_set_val(pin, 0);
        (void)gpio_set_pin(pin, GPIO_MODE_OUT);
    }
    s_command_sem = tk_cre_sem(&sem);
    if (s_command_sem < E_OK) return s_command_sem;
    s_feedback_sem = tk_cre_sem(&sem);
    if (s_feedback_sem < E_OK) {
        ER er = s_feedback_sem;
        (void)tk_del_sem(s_command_sem);
        s_command_sem = 0;
        return er;
    }
#if S2_DRIVE_ENABLED
    {
        UINT slice, timeout = 10000U;
        clr_w(RESETS_RESET, RESETS_RESET_PWM);
        while ((in_w(RESETS_RESET_DONE) & RESETS_RESET_PWM) == 0U) {
            if (--timeout == 0U) {
                (void)tk_del_sem(s_command_sem);
                (void)tk_del_sem(s_feedback_sem);
                s_command_sem = s_feedback_sem = 0;
                return E_IO;
            }
            WaitUsec(1);
        }
        /* 20 kHz at 125 MHz. Servo slice 6 is untouched. */
        for (slice = 4; slice <= 5; ++slice) {
            out_w(S2_PWM_REG(slice, PWM_CHx_CSR), 0);
            out_w(S2_PWM_REG(slice, PWM_CHx_DIV), 0x10U);
            out_w(S2_PWM_REG(slice, PWM_CHx_CTR), 0);
            out_w(S2_PWM_REG(slice, PWM_CHx_CC), 0);
            out_w(S2_PWM_REG(slice, PWM_CHx_TOP), S2_PWM_TOP);
        }
        for (pin = 8; pin <= 11; ++pin) (void)pwm_set_pin(pin);
        (void)pwm_set_enabled(8, TRUE);
        (void)pwm_set_enabled(10, TRUE);
    }
#endif
    return E_OK;
}

EXPORT ER subsystem2_start(void)
{
#if S2_FEEDBACK_ENABLED
    ID tid;
    if (s_command_sem <= 0 || s_started) return E_OBJ;
#if S2_DRIVE_ENABLED
    tid = app_start_task((FP)motor_task, S2_PRI_MOTOR, TP_PRC1, "motor_task");
#else
    tid = app_start_task((FP)encoder_feedback_task, S2_PRI_MOTOR, TP_PRC1, "encoder_feedback");
#endif
    if (tid < E_OK) return tid;
    s_started = TRUE;
#endif
    return E_OK;
}
