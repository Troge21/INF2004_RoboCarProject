/* HC-SR04: GP4 TRIG, GP5 ECHO through a 5 V -> 3.3 V divider.
 * GPIO IRQ13 timestamps echo edges; the task sleeps while waiting.
 * Only this core-0 task owns the sensor. No servo or controller commands yet.
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
#define SAMPLE_PAUSE_MS 500 /* comfortably longer than 60 ms trigger spacing */

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
        tm_printf((UB *)"[ULTRA] HC-SR04 GP4=TRIG GP5=ECHO (divider required); servo OFF\n");
    tk_dly_tsk(100); /* let sensor power settle */
    UW sequence = 0;
    for (;;) {
        UW mm, pulse;
        const char *error = measure(&mm, &pulse);
        S1_TELEMETRY sample = {0};
        er = s1_make_telemetry(&sample, sequence++, mm, error == NULL);
        /* Temporary bench route; final measurements go via the controller.
         * Never delay sensing for WiFi, a disconnected broker or full queue. */
        if (er == E_OK) er = s1_submit_telemetry(&sample);
        if (er != E_OK && er != E_TMOUT) CHECK(er, "S5 -> S1 telemetry");
        if (S5_ULTRA_LOG_ENABLED) {
            if (error == NULL) {
                tm_printf((UB *)"[ULTRA] distance=%u mm echo=%u us\n", mm, pulse);
            } else {
                tm_printf((UB *)"[ULTRA] INVALID %s echo=%u us (not clear space)\n",
                          error, pulse);
            }
        }
        tk_dly_tsk(SAMPLE_PAUSE_MS);
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
