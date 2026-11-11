#include "encoder_capture.h"
#include "../gpio_irq.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

#if TM_CONSOLE_UART
#error "Wheel encoders require GP0/GP1: use CONSOLE=usb_cdc CRASH_REPORT=0"
#endif
static volatile UW wheel_counts[2];
static BOOL started;
static const UINT a_pins[2] = {16U, 0U};
static const UINT b_pins[2] = {17U, 1U};
static const INT signs[2] = {S2_LEFT_ENCODER_SIGN, S2_RIGHT_ENCODER_SIGN};

static void encoder_edge(UINT pin, UINT events, UW levels, UW now_us)
{
    UINT wheel = pin == 16U ? 0U : 1U;
    INT step = (levels & (1U << b_pins[wheel])) ? signs[wheel] : -signs[wheel];
    (void)events;
    (void)now_us;
    if (step > 0) ++wheel_counts[wheel];
    else --wheel_counts[wheel];
}

ER s2_encoder_start(void)
{
    UINT wheel, channel;
    ER er;
#if TK_SUPPORT_SMP
    if (tk_get_prc() != 1) return E_CTX;
#endif
    if (started) return E_OBJ;
    for (wheel = 0; wheel < 2U; ++wheel) {
        UINT pins[2] = {a_pins[wheel], b_pins[wheel]};
        for (channel = 0; channel < 2U; ++channel) {
            er = gpio_set_pin(pins[channel], GPIO_MODE_IN);
            if (er != E_OK) return er;
            clr_w(GPIO(pins[channel]), GPIO_PDE);
            set_w(GPIO(pins[channel]), GPIO_PUE);
        }
        wheel_counts[wheel] = 0;
    }
    er = app_gpio_irq_register(a_pins[0], APP_GPIO_RISE, encoder_edge);
    if (er != E_OK) return er;
    er = app_gpio_irq_register(a_pins[1], APP_GPIO_RISE, encoder_edge);
    if (er != E_OK) {
        (void)app_gpio_irq_unregister(a_pins[0]);
        return er;
    }
    (void)app_gpio_irq_enable(a_pins[0], TRUE);
    (void)app_gpio_irq_enable(a_pins[1], TRUE);
    started = TRUE;
    return E_OK;
}

ER s2_encoder_read(S2_ENCODER_COUNTS *counts)
{
    UINT intsts;
    if (counts == NULL) return E_PAR;
#if TK_SUPPORT_SMP
    if (tk_get_prc() != 1) return E_CTX;
#endif
    if (!started) return E_OBJ;
    DI(intsts);
    counts->left = wheel_counts[0];
    counts->right = wheel_counts[1];
    EI(intsts);
    return E_OK;
}
