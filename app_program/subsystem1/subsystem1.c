/* Subsystem 1 lifecycle hooks. The LED example has been retired.
 * WiFi command/telemetry implementation remains to be added (TEAM.md).
 * Motor/encoder calibration is an optional test, not a runtime task here.
 */
#include "subsystem1.h"

EXPORT ER subsystem1_init(void)
{
    return E_OK;
}

EXPORT ER subsystem1_start(void)
{
    return E_OK;
}
