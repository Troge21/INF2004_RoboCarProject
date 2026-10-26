/* Subsystem 1: WiFi commands/telemetry tests will be added with implementation. */
#include "subsystem1.h"

#if APP_TEST
EXPORT void subsystem1_test(void)
{
    tm_printf((UB *)"[TEST] subsystem1 SKIP: WiFi command/telemetry tests pending.\n");
}
#endif
