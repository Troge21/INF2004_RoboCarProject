#ifndef APP_GPIO_IRQ_H
#define APP_GPIO_IRQ_H
#include "app.h"

/* Core-0 GPIO IRQ13 dispatcher. Callbacks must not print, block or wait.
 * Event bits are per-pin: falling=4, rising=8. Registration starts masked. */
#define APP_GPIO_FALL 4U
#define APP_GPIO_RISE 8U
typedef void (*APP_GPIO_CALLBACK)(UINT pin, UINT events, UW levels, UW now_us);
ER app_gpio_irq_register(UINT pin, UINT events, APP_GPIO_CALLBACK callback);
ER app_gpio_irq_enable(UINT pin, BOOL enabled);
ER app_gpio_irq_unregister(UINT pin);
#endif
