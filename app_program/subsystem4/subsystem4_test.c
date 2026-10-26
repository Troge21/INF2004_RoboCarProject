/* Previous sample-data demo tests are retired; IDs remain in test logs. */
#include "subsystem4.h"

#if APP_TEST
EXPORT void subsystem4_test(void)
{
    tm_printf((UB *)"[TEST] subsystem4 SKIP: IMU tests pending.\n");
}
#endif /* APP_TEST */
