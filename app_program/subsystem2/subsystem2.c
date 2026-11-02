/* S2 owns motor PWM and consumes fresh controller commands.
 * GP8/9: left A/B, GP10/11: right A/B. No encoder feedback yet. */
#include "subsystem2.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

#define S2_PWM_TOP 6249U
#define S2_PWM_REG(slice, offset) (PWM_BASE + (slice) * 0x14U + (offset))

/* Shared latest-command snapshot, protected for calls from either core. */
static ID s_command_sem;
static BOOL s_motor_ready;
static INT s_left_duty, s_right_duty;
static UW s_command_ms;
static BOOL s_command_valid;

EXPORT ER subsystem2_set_duty(INT left, INT right)
{
    SYSTIM now;
    ER er;
    if (left < 0 || left > 100 || right < 0 || right > 100) return E_PAR;
    if (!s_motor_ready) return E_OBJ;
    er = tk_wai_sem(s_command_sem, 1, TMO_POL);
    if (er != E_OK) return er;
    (void)tk_get_tim(&now);
    s_left_duty = left;
    s_right_duty = right;
    s_command_ms = (UW)now.lo;
    s_command_valid = TRUE;
    (void)tk_sig_sem(s_command_sem, 1);
    return E_OK;
}

#if !APP_TEST
/* Only this task writes PWM, avoiding cross-core compare-register races.
 * B remains low for forward, A remains low for reversed wiring.
 * Zero duty gives low/low (brake on the recorded Robo Pico setup). */
static void motor_duty(UINT a, UINT b, INT duty, BOOL reversed, UINT scale)
{
    /* Scale before division so low duties retain fractional-percent trim. */
    UW compare = ((S2_PWM_TOP + 1U) * (UW)duty * scale) / 10000U;
    (void)pwm_set_cc(reversed ? a : b, 0);
    (void)pwm_set_cc(reversed ? b : a, compare);
}

static void motor_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    for (;;) {
        SYSTIM now;
        INT left = 0, right = 0;
        if (tk_wai_sem(s_command_sem, 1, TMO_POL) == E_OK) {
            (void)tk_get_tim(&now);
            if (s_command_valid &&
                (UW)((UW)now.lo - s_command_ms) <= S2_COMMAND_MAX_AGE_MS) {
                left = s_left_duty;
                right = s_right_duty;
            }
            (void)tk_sig_sem(s_command_sem, 1);
        }
        motor_duty(8, 9, left, S2_LEFT_REVERSED, S2_LEFT_POWER_SCALE);
        motor_duty(10, 11, right, S2_RIGHT_REVERSED, S2_RIGHT_POWER_SCALE);
        (void)tk_dly_tsk(S2_PERIOD_MS);
    }
}
#endif

EXPORT ER subsystem2_init(void)
{
    UINT pin;
    T_CSEM sem = {0};
    s_motor_ready = FALSE;
    s_command_valid = FALSE;
    sem.sematr = TA_TFIFO | TA_FIRST;
    sem.isemcnt = 1;
    sem.maxsem = 1;
    s_command_sem = tk_cre_sem(&sem);
    if (s_command_sem < E_OK) return s_command_sem;
    /* Preload low before enabling outputs. Calibration can later select coast. */
    for (pin = 8; pin <= 11; ++pin) {
        (void)gpio_set_val(pin, 0);
        (void)gpio_set_pin(pin, GPIO_MODE_OUT);
    }
#if !APP_TEST
    {
        UINT slice;
        UINT timeout = 10000U;
        /* Release shared PWM reset; never reset another subsystem's slice. */
        clr_w(RESETS_RESET, RESETS_RESET_PWM);
        while ((in_w(RESETS_RESET_DONE) & RESETS_RESET_PWM) == 0U) {
            if (--timeout == 0U) return E_IO;
            WaitUsec(1);
        }
        /* GP8/9 share slice 4, GP10/11 share slice 5.
         * Divider 1, TOP 6249: 20 kHz at the default 125 MHz clock.
         * Servo GP12 uses slice 6 and is untouched. */
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
        s_motor_ready = TRUE;
    }
#endif
    return E_OK;
}

EXPORT ER subsystem2_start(void)
{
#if !APP_TEST
    ID tid = app_start_task((FP)motor_task, S2_PRI_MOTOR, TP_PRC1, "motor_task");
    return tid < E_OK ? tid : E_OK;
#else
    return E_OK;
#endif
}
