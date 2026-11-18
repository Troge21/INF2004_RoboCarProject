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
static BOOL s_motor_ready, s_feedback_ready, s_started;
static S2_COMMAND s_command;
static S2_MOTION_DATA s_feedback;
/* Protected by the existing feedback semaphore: path mm:32 + source time:32. */
static uint8_t s_travel[8];

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
    if (s_command_sem <= 0) return E_OBJ;
    er = tk_wai_sem(s_command_sem, 1, TMO_POL);
    if (er != E_OK) return er;
    if (!s_motor_ready) {
        (void)tk_sig_sem(s_command_sem, 1);
        return E_OBJ;
    }
    (void)tk_get_tim(&now);
    s_command.mode = left == 0 && right == 0 ? S2_MODE_STOP : mode;
    s_command.left = left;
    s_command.right = right;
    s_command.timestamp_ms = (UW)now.lo;
    s_command.valid = TRUE;
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
    if (left < 0 || left > S2_MAX_RPM_X10 || right < 0 || right > S2_MAX_RPM_X10) return E_PAR;
    return submit_command(S2_MODE_RPM, left, right);
}

EXPORT ER subsystem2_get_motion(S2_MOTION_DATA *sample)
{
    ER er;
    if (sample == NULL) return E_PAR;
    if (s_feedback_sem <= 0) return E_OBJ;
    er = tk_wai_sem(s_feedback_sem, 1, TMO_POL);
    if (er != E_OK) return er;
    if (s_feedback_ready) *sample = s_feedback;
    er = s_feedback_ready ? E_OK : E_OBJ;
    (void)tk_sig_sem(s_feedback_sem, 1);
    return er;
}

int telemetry_get_travel(TELEMETRY_TRAVEL *out)
{
    ER er;
    if (out == NULL) return E_PAR;
    if (s_feedback_sem <= 0) return E_OBJ;
    er = tk_wai_sem(s_feedback_sem, 1, TMO_POL);
    if (er != E_OK) return er;
    er = s_feedback_ready && s2_read32(s_travel) != UINT32_MAX ? E_OK : E_OBJ;
    if (er == E_OK) {
        out->distance_mm = s2_read32(s_travel);
        out->timestamp_ms = s2_read32(s_travel + 4);
    }
    (void)tk_sig_sem(s_feedback_sem, 1);
    return er;
}

#if S2_FEEDBACK_ENABLED
#if S2_DRIVE_ENABLED
static void motor_duty(UINT a, UINT b, INT duty_x100, BOOL reversed)
{
    UW compare = ((S2_PWM_TOP + 1U) * (UW)duty_x100) / 10000U;
    (void)pwm_set_cc(reversed ? a : b, 0);
    (void)pwm_set_cc(reversed ? b : a, compare);
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
        BOOL available = tk_wai_sem(s_command_sem, 1, TMO_POL) == E_OK;
        if (available) {
            command = s_command;
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
            s_feedback = control.data;
            s2_write32(s_travel, s2_odometer_mm(&odometer));
            s2_write32(s_travel + 4, (UW)now.lo);
            s_feedback_ready = TRUE;
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
    s_motor_ready = s_feedback_ready = s_started = FALSE;
    s_command.valid = FALSE;
    s2_write32(s_travel, UINT32_MAX);
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
