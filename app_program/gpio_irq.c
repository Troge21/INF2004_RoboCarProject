#include "gpio_irq.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

#define GPIO_IRQ 13U
#define GPIO_REG(base, pin) (IO_BANK0_BASE + (base) + 4U * ((pin) / 8U))
#define GPIO_BITS(pin, events) ((events) << (4U * ((pin) % 8U)))
static APP_GPIO_CALLBACK callbacks[30];
static UINT event_masks[30];
static BOOL installed;

static void gpio_dispatch(UINT intno)
{
    UINT bank, bit;
    UW levels = in_w(GPIO_IN), now = in_w(TIMER_TIMERAWL);
    (void)intno;
    for (bank = 0; bank < 4U; ++bank) {
        UW pending = in_w(IO_BANK0_BASE + 0x120U + 4U * bank);
        if (pending == 0U) continue;
        for (bit = 0; bit < 8U && bank * 8U + bit < 30U; ++bit) {
            UINT pin = bank * 8U + bit;
            UINT events = (pending >> (4U * bit)) & event_masks[pin];
            APP_GPIO_CALLBACK callback = callbacks[pin];
            if (events != 0U && callback != NULL) {
                out_w(GPIO_REG(0xf0U, pin), GPIO_BITS(pin, events));
                callback(pin, events, levels, now);
            }
        }
    }
}

static BOOL on_core_zero(void)
{
#if TK_SUPPORT_SMP
    return tk_get_prc() == 1;
#else
    return TRUE;
#endif
}

ER app_gpio_irq_register(UINT pin, UINT events, APP_GPIO_CALLBACK callback)
{
    T_DINT dint = {0};
    ER er;
    UINT intsts;
    if (pin >= 30U || callback == NULL || events == 0U || (events & ~12U)) return E_PAR;
    if (!on_core_zero()) return E_CTX;
    if (callbacks[pin] != NULL) return E_OBJ;
    if (!installed) {
        dint.intatr = TA_HLNG;
        dint.inthdr = (FP)gpio_dispatch;
        er = tk_def_int(GPIO_IRQ, &dint);
        if (er != E_OK) return er;
        installed = TRUE;
        ClearInt(GPIO_IRQ);
        EnableInt(GPIO_IRQ, 3);
    }
    DI(intsts);
    clr_w(GPIO_REG(0x100U, pin), GPIO_BITS(pin, events));
    out_w(GPIO_REG(0xf0U, pin), GPIO_BITS(pin, events));
    event_masks[pin] = events;
    callbacks[pin] = callback;
    EI(intsts);
    return E_OK;
}

ER app_gpio_irq_enable(UINT pin, BOOL enabled)
{
    UINT intsts;
    if (pin >= 30U) return E_PAR;
    if (!on_core_zero()) return E_CTX;
    if (callbacks[pin] == NULL) return E_OBJ;
    DI(intsts);
    if (enabled) {
        out_w(GPIO_REG(0xf0U, pin), GPIO_BITS(pin, event_masks[pin]));
        set_w(GPIO_REG(0x100U, pin), GPIO_BITS(pin, event_masks[pin]));
    } else {
        clr_w(GPIO_REG(0x100U, pin), GPIO_BITS(pin, event_masks[pin]));
    }
    EI(intsts);
    return E_OK;
}

ER app_gpio_irq_unregister(UINT pin)
{
    ER er = app_gpio_irq_enable(pin, FALSE);
    UINT intsts;
    if (er != E_OK) return er;
    DI(intsts);
    out_w(GPIO_REG(0xf0U, pin), GPIO_BITS(pin, event_masks[pin]));
    callbacks[pin] = NULL;
    event_masks[pin] = 0;
    EI(intsts);
    return E_OK;
}
