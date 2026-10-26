/* Optional manual calibration: set S1_ENCODER_CALIBRATION=1 in subsystem1.h,
 * then make TEST=1 -j8 in build_make.
 * Motors never run automatically. Raise wheels and turn them by hand.
 * Left encoder GP7/GP28; right GP0/GP1. Use 3.3 V signals/common ground.
 * Counts use the same x1 (A rising edge) mode as the powered bench test.
 * This test owns IO_BANK0 IRQ13 and GP0/1/7/8/9/10/11/28 exclusively.
 */
#include "subsystem1.h"

#if APP_TEST && S1_ENCODER_CALIBRATION
#include <sys/sysdef.h>
#include <tk/syslib.h>

#if TM_CONSOLE_UART
#error "Encoder GP0/GP1 conflicts with UART: use CONSOLE=usb_cdc CRASH_REPORT=0"
#endif

#define ENC_IRQ 13
#define ENC_REG(offset) (IO_BANK0_BASE + (offset))
#define ENC_EVENT(pin) (8U << (4 * (pin))) /* A pins GP0 and GP7 share bank 0 */
#define ENC_EVENTS (ENC_EVENT(7) | ENC_EVENT(0))

static const UINT encoder_a[] = {7, 0};
static const UINT encoder_b[] = {28, 1};
static volatile INT counts[2];

/* Runs on core 0. B's level at each A rising edge determines direction.
 * Never print or wait in this interrupt handler. */
static void encoder_isr(UINT intno)
{
    UW pending = in_w(ENC_REG(0x120)) & ENC_EVENTS; /* PROC0_INTS0 */
    UW levels = in_w(GPIO_IN);
    UINT i;
    (void)intno;
    out_w(ENC_REG(0xf0), pending); /* INTR0: write-one-to-clear */
    for (i = 0; i < 2; ++i) {
        if (pending & ENC_EVENT(encoder_a[i])) {
            counts[i] += (levels & (1U << encoder_b[i])) ? 1 : -1;
        }
    }
}

static ER calibration_init(void)
{
    T_DINT dint = {0};
    UINT pin, i;
    ER er;

    /* SIO outputs bypass PWM entirely. Low/low brakes during setup;
     * high/high selects Robo Pico's coast mode for hand rotation. */
    for (pin = 8; pin <= 11; ++pin) {
        gpio_set_val(pin, 0);
        gpio_set_pin(pin, GPIO_MODE_OUT);
    }
    out_w(GPIO_OUT_SET, 0xfU << 8); /* both inputs high on both motors */

    DisableInt(ENC_IRQ);
    clr_w(ENC_REG(0x100), ENC_EVENTS); /* PROC0_INTE0 */
    for (i = 0; i < 2; ++i) {
        UINT pins[] = {encoder_a[i], encoder_b[i]};
        UINT j;
        for (j = 0; j < 2; ++j) {
            gpio_set_pin(pins[j], GPIO_MODE_IN);
            clr_w(GPIO(pins[j]), GPIO_PDE);
            set_w(GPIO(pins[j]), GPIO_PUE); /* open-collector pull-up */
        }
        counts[i] = 0;
    }
    dint.intatr = TA_HLNG;
    dint.inthdr = (FP)encoder_isr;
    er = tk_def_int(ENC_IRQ, &dint);
    if (er < E_OK) return er;
    out_w(ENC_REG(0xf0), ENC_EVENTS);
    set_w(ENC_REG(0x100), ENC_EVENTS);
    ClearInt(ENC_IRQ);
    EnableInt(ENC_IRQ, 3);
    return E_OK;
}

static void calibration_task(INT stacd, void *exinf)
{
    ER er = calibration_init();
    (void)stacd;
    (void)exinf;
    if (er < E_OK) {
        CHECK(er, "encoder calibration setup");
        tk_exd_tsk();
        return;
    }
    tm_printf((UB *)"[CAL] READY: motors OFF/coasting. Turn one wheel by hand.\n");
    tm_printf((UB *)"[CAL] Record START, turn exactly 10 revolutions, record END.\n");
    tm_printf((UB *)"[CAL] Counts/revolution = abs(END - START) / 10. Reset restarts at zero.\n");
    for (;;) {
        tm_printf((UB *)"[CAL] LIVE motors OFF | LEFT=%d RIGHT=%d (x1 counts)\n",
                  counts[0], counts[1]);
        tk_dly_tsk(1000);
    }
}

EXPORT void subsystem1_test(void)
{
    /* Core-0 routing matches the IRQ enable registers above. */
    ID tid = app_start_task((FP)calibration_task, 13, TP_PRC1,
                            "encoder_calibration");
    CHECK(tid, "start encoder calibration");
}
#elif APP_TEST
EXPORT void subsystem1_test(void)
{
    tm_printf((UB *)"[TEST] subsystem1: encoder calibration disabled (S1_ENCODER_CALIBRATION=0).\n");
}
#endif /* APP_TEST && S1_ENCODER_CALIBRATION */
