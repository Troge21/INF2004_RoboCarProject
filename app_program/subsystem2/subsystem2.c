/* S2 owns PWM, encoder feedback and wheel-speed control. No networking.
 * GP8/9 left, GP10/11 right; encoders A/B left16/17, right0/1. */
#include "subsystem2.h"
#include "motion_control.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

#define S2_PWM_TOP 6249U
#define S2_PWM_REG(slice, offset) (PWM_BASE + (slice) * 0x14U + (offset))
#define S2_DRIVE_ENABLED (!APP_TEST || (S2_ENCODER_CALIBRATION && S2_ENCODER_POWERED_TEST))

static ID s_command_sem, s_feedback_sem;
static BOOL s_motor_ready, s_feedback_ready, s_started;
static S2_COMMAND s_command;
static S2_MOTION_DATA s_feedback;

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

#if S2_DRIVE_ENABLED
static void motor_duty(UINT a, UINT b, INT duty_x100, BOOL reversed)
{
    UW compare = ((S2_PWM_TOP + 1U) * (UW)duty_x100) / 10000U;
    (void)pwm_set_cc(reversed ? a : b, 0);
    (void)pwm_set_cc(reversed ? b : a, compare);
}

static void motor_task(INT stacd, void *exinf)
{
    S2_CONTROL_STATE control;
    S2_ENCODER_COUNTS counts = {0, 0};
    SYSTIM now;
    ER encoder_er = s2_encoder_start();
    (void)stacd; (void)exinf;
    if (encoder_er == E_OK) encoder_er = s2_encoder_read(&counts);
    (void)tk_get_tim(&now);
    s2_control_init(&control, (UW)now.lo, counts);
    if (tk_wai_sem(s_command_sem, 1, TMO_FEVR) == E_OK) {
        s_motor_ready = TRUE;
        (void)tk_sig_sem(s_command_sem, 1);
    }
    for (;;) {
        S2_COMMAND command = {0};
        BOOL available = tk_wai_sem(s_command_sem, 1, TMO_POL) == E_OK;
        if (available) {
            command = s_command;
            (void)tk_sig_sem(s_command_sem, 1);
        }
        (void)tk_get_tim(&now);
        if (encoder_er == E_OK) encoder_er = s2_encoder_read(&counts);
        s2_control_step(&control, (UW)now.lo, counts, &command, available, encoder_er == E_OK);
        motor_duty(8, 9, control.data.duty_left_x100, S2_LEFT_REVERSED);
        motor_duty(10, 11, control.data.duty_right_x100, S2_RIGHT_REVERSED);
        if (tk_wai_sem(s_feedback_sem, 1, TMO_POL) == E_OK) {
            s_feedback = control.data;
            s_feedback_ready = TRUE;
            (void)tk_sig_sem(s_feedback_sem, 1);
        }
        (void)tk_dly_tsk(S2_PERIOD_MS);
    }
}
#endif

EXPORT ER subsystem2_init(void)
{
    UINT pin;
    T_CSEM sem = {0};
    if (s_command_sem > 0) return E_OBJ;
    s_motor_ready = s_feedback_ready = s_started = FALSE;
    s_command.valid = FALSE;
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
#if S2_DRIVE_ENABLED
    ID tid;
    if (s_command_sem <= 0 || s_started) return E_OBJ;
    tid = app_start_task((FP)motor_task, S2_PRI_MOTOR, TP_PRC1, "motor_task");
    if (tid < E_OK) return tid;
    s_started = TRUE;
#endif
    return E_OK;
}
