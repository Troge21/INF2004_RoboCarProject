/* HC-SR04: GP4 TRIG, GP5 ECHO through a 5 V -> 3.3 V divider.
 * GP12 drives the servo on PWM slice 6 at 50 Hz.
 * GPIO IRQ13 timestamps echo edges; the task sleeps while waiting.
 * Only this core-0 task owns the sensor and servo. It also publishes the
 * obstacle-avoidance wheel request through S2's command interface.
 */
#include "subsystem5.h"
#include "../gpio_irq.h"
#include "../subsystem2/subsystem2.h"
#include "../subsystem1/subsystem1.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>


#define ECHO_FALL (4U << (4 * S5_ECHO_PIN))
#define ECHO_RISE (8U << (4 * S5_ECHO_PIN))
#define ECHO_EDGES (ECHO_FALL | ECHO_RISE)
#define ECHO_TIMEOUT_MS 40
#define SAMPLE_PAUSE_MS 80 /* HC-SR04 trigger spacing remains greater than 60 ms */
#define S5_PWM_SLICE 6U
#define S5_PWM_TOP 39062U /* 50 Hz: 125 MHz / 64 / (39062 + 1) */
#define S5_PWM_REG(offset) (PWM_BASE + S5_PWM_SLICE * 0x14U + (offset))

enum { WAIT_RISE, WAIT_FALL, COMPLETE, BAD_EDGES };
static ID echo_done;
static volatile UINT echo_state;
static volatile UW rise_us, width_us;

EXPORT ER s5_echo_to_mm(UW pulse_us, UW *mm)
{
    if (mm == NULL) return E_PAR;
    *mm = 0;
    /* Datasheet formula: centimetres = microseconds / 58 (x10 for mm). */
    if (pulse_us < 116 || pulse_us > 23200) return E_PAR;
    *mm = (pulse_us * 10U + 29U) / 58U;
    return E_OK;
}

EXPORT S5_MOTION s5_choose_motion(BOOL centre_valid, UW centre_mm,
                                  BOOL left_valid, UW left_mm,
                                  BOOL right_valid, UW right_mm)
{
    if (!centre_valid) return S5_MOTION_STOP;
    if (centre_mm > S5_OBSTACLE_MM) return S5_MOTION_FORWARD;
    if (!left_valid && !right_valid) return S5_MOTION_STOP;
    if (!right_valid) return left_mm > S5_OBSTACLE_MM ? S5_MOTION_LEFT : S5_MOTION_STOP;
    if (!left_valid) return right_mm > S5_OBSTACLE_MM ? S5_MOTION_RIGHT : S5_MOTION_STOP;
    if (left_mm <= S5_OBSTACLE_MM && right_mm <= S5_OBSTACLE_MM)
        return S5_MOTION_STOP;
    return left_mm + S5_CLEARANCE_MARGIN_MM >= right_mm ?
           S5_MOTION_LEFT : S5_MOTION_RIGHT;
}

static void servo_set_angle(UINT degrees)
{
    /* 0..180 degrees maps to a conservative 1.0..2.0 ms pulse. */
    UW pulse_us = 1000U + (1000U * degrees) / 180U;
    UW compare = (pulse_us * (S5_PWM_TOP + 1U) + 10000U) / 20000U;
    (void)pwm_set_cc(S5_SERVO_PIN, compare);
}

static void command_motion(S5_MOTION motion)
{
    INT left = 0, right = 0;
    if (motion == S5_MOTION_FORWARD)
        left = right = S5_FORWARD_RPM_X10;
    else if (motion == S5_MOTION_LEFT) {
        left = S5_TURN_INNER_RPM_X10;
        right = S5_TURN_OUTER_RPM_X10;
    } else if (motion == S5_MOTION_RIGHT) {
        left = S5_TURN_OUTER_RPM_X10;
        right = S5_TURN_INNER_RPM_X10;
    }
    (void)subsystem2_set_rpm_x10(left, right);
}

static void controlled_delay(UW delay_ms, S5_MOTION motion)
{
    while (delay_ms != 0U) {
        UW step = delay_ms > 50U ? 50U : delay_ms;
        command_motion(motion); /* refresh before S2's 100 ms watchdog */
        (void)tk_dly_tsk(step);
        delay_ms -= step;
    }
}

static void echo_isr(UINT pin, UINT events, UW levels, UW now)
{
    UW pending = events << (4U * S5_ECHO_PIN);
    (void)pin;
    (void)levels;
    if (pending == 0) return;
    if (echo_state == WAIT_RISE && pending == ECHO_RISE) {
        rise_us = now;
        echo_state = WAIT_FALL;
        return;
    }
    if (echo_state == WAIT_FALL && pending == ECHO_FALL) {
        width_us = now - rise_us; /* unsigned subtraction handles timer wrap */
        echo_state = COMPLETE;
    } else {
        /* Coalesced edges or unexpected order cannot give a valid width. */
        echo_state = BAD_EDGES;
    }
    (void)app_gpio_irq_enable(S5_ECHO_PIN, FALSE);
    tk_sig_sem(echo_done, 1); /* no printing or waiting in interrupt context */
}

static const char *measure(UW *mm, UW *pulse)
{
    ER er;
    *mm = 0;
    *pulse = 0;
    if (gpio_get_val(S5_ECHO_PIN)) return "ECHO_HIGH_BEFORE_TRIGGER";
    while (tk_wai_sem(echo_done, 1, TMO_POL) == E_OK) { /* drain stale signal */ }

    (void)app_gpio_irq_enable(S5_ECHO_PIN, FALSE);
    echo_state = WAIT_RISE;
    width_us = 0;
    (void)app_gpio_irq_enable(S5_ECHO_PIN, TRUE);
    gpio_set_val(S5_TRIG_PIN, 1);
    WaitUsec(12); /* HC-SR04 requires at least 10 us */
    gpio_set_val(S5_TRIG_PIN, 0);
    er = tk_wai_sem(echo_done, 1, ECHO_TIMEOUT_MS);
    (void)app_gpio_irq_enable(S5_ECHO_PIN, FALSE);
    if (er == E_TMOUT) {
        return echo_state == WAIT_RISE ? "NO_ECHO_TIMEOUT" : "ECHO_END_TIMEOUT";
    }
    if (er < E_OK) return "KERNEL_WAIT_ERROR";
    if (echo_state != COMPLETE) return "INVALID_EDGE_SEQUENCE";
    *pulse = width_us;
    return s5_echo_to_mm(*pulse, mm) == E_OK ? NULL : "OUT_OF_RANGE";
}

static void ultrasonic_task(INT stacd, void *exinf)
{
    ER er;
    (void)stacd;
    (void)exinf;
    /* GPIO interrupt routing and NVIC setup are both on physical core 0. */
    er = app_gpio_irq_register(S5_ECHO_PIN, APP_GPIO_RISE | APP_GPIO_FALL, echo_isr);
    if (er < E_OK) {
        CHECK(er, "ultrasonic IRQ setup");
        tk_exd_tsk();
        return;
    }
    if (S5_ULTRA_LOG_ENABLED)
        tm_printf((UB *)"[ULTRA] HC-SR04 GP4/GP5; servo GP12; avoidance enabled\n");
    tk_dly_tsk(100); /* let sensor power settle */
    servo_set_angle(S5_SERVO_CENTRE_DEG);
    controlled_delay(S5_SERVO_SETTLE_MS, S5_MOTION_STOP);
    UW sequence = 0;
    S5_MOTION current_motion = S5_MOTION_STOP;
    for (;;) {
        UW centre_mm, left_mm, right_mm, pulse;
        const char *centre_error, *left_error = "NOT_SCANNED", *right_error = "NOT_SCANNED";
        S5_MOTION motion;

        servo_set_angle(S5_SERVO_CENTRE_DEG);
        /* When the path was clear, keep driving while the next centre sample
         * settles. Scanning and invalid readings still stop the wheels. */
        controlled_delay(S5_SERVO_SETTLE_MS, current_motion);
        centre_error = measure(&centre_mm, &pulse);
        if (centre_error == NULL && centre_mm <= S5_OBSTACLE_MM) {
            controlled_delay(SAMPLE_PAUSE_MS, S5_MOTION_STOP);
            servo_set_angle(S5_SERVO_LEFT_DEG);
            controlled_delay(S5_SERVO_SETTLE_MS, S5_MOTION_STOP);
            left_error = measure(&left_mm, &pulse);
            controlled_delay(SAMPLE_PAUSE_MS, S5_MOTION_STOP);
            servo_set_angle(S5_SERVO_RIGHT_DEG);
            controlled_delay(S5_SERVO_SETTLE_MS, S5_MOTION_STOP);
            right_error = measure(&right_mm, &pulse);
            servo_set_angle(S5_SERVO_CENTRE_DEG);
        } else {
            left_mm = right_mm = 0U;
        }
        motion = s5_choose_motion(centre_error == NULL, centre_mm,
                                  left_error == NULL, left_mm,
                                  right_error == NULL, right_mm);
        S1_TELEMETRY sample = {0};
        er = s1_make_telemetry(&sample, sequence++, centre_mm,
                               centre_error == NULL);
        /* Temporary bench route; final measurements go via the controller.
         * Never delay sensing for WiFi, a disconnected broker or full queue. */
        if (er == E_OK) er = s1_submit_telemetry(&sample);
        if (er != E_OK && er != E_TMOUT) CHECK(er, "S5 -> S1 telemetry");
        if (S5_ULTRA_LOG_ENABLED) {
            if (centre_error == NULL) {
                tm_printf((UB *)"[ULTRA] centre=%u left=%u right=%u motion=%d\n",
                          centre_mm, left_mm, right_mm, motion);
            } else {
                tm_printf((UB *)"[ULTRA] INVALID %s echo=%u us (not clear space)\n",
                          centre_error, pulse);
            }
        }
        if (motion == S5_MOTION_LEFT || motion == S5_MOTION_RIGHT)
            controlled_delay(S5_TURN_MS, motion);
        else
            controlled_delay(SAMPLE_PAUSE_MS, motion);
        current_motion = motion == S5_MOTION_FORWARD ? motion : S5_MOTION_STOP;
    }
}

EXPORT ER subsystem5_init(void)
{
#if APP_TEST && S2_ENCODER_CALIBRATION
    return E_OK;
#else
    T_CSEM csem = {0};
    ER er;
    gpio_set_val(S5_TRIG_PIN, 0);
    er = gpio_set_pin(S5_TRIG_PIN, GPIO_MODE_OUT);
    if (er < E_OK) return er;
    er = gpio_set_pin(S5_ECHO_PIN, GPIO_MODE_IN);
    if (er < E_OK) return er;
    clr_w(GPIO(S5_ECHO_PIN), GPIO_PUE);
    set_w(GPIO(S5_ECHO_PIN), GPIO_PDE); /* idle low when unplugged */
    /* Configure only servo slice 6; S2 owns slices 4 and 5. */
    out_w(S5_PWM_REG(PWM_CHx_CSR), 0U);
    out_w(S5_PWM_REG(PWM_CHx_DIV), 0x400U); /* integer divider 64 */
    out_w(S5_PWM_REG(PWM_CHx_CTR), 0U);
    out_w(S5_PWM_REG(PWM_CHx_CC), 0U);
    out_w(S5_PWM_REG(PWM_CHx_TOP), S5_PWM_TOP);
    er = pwm_set_pin(S5_SERVO_PIN);
    if (er < E_OK) return er;
    er = pwm_set_enabled(S5_SERVO_PIN, TRUE);
    if (er < E_OK) return er;
    csem.sematr = TA_TFIFO;
    csem.maxsem = 1;
    echo_done = tk_cre_sem(&csem);
    return echo_done < E_OK ? echo_done : E_OK;
#endif
}

EXPORT ER subsystem5_start(void)
{
#if APP_TEST && S2_ENCODER_CALIBRATION
    if (S5_ULTRA_LOG_ENABLED)
        tm_printf((UB *)"[ULTRA] SKIP: subsystem2 encoder calibration owns GPIO IRQ13.\n");
    return E_OK;
#else
    ID tid;
    if (echo_done <= 0) return E_OBJ;
    tid = app_start_task((FP)ultrasonic_task, S5_PRI_MAIN, TP_PRC1,
                         "ultrasonic_task");
    return tid < E_OK ? tid : E_OK;
#endif
}
