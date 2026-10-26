/* Previous sample-data demo tests are retired; IDs remain in test logs. */
#include "subsystem2.h"

#if APP_TEST
EXPORT void subsystem2_test(void)
{
    tm_printf((UB *)"[TEST] subsystem2 SKIP: motor controller tests pending.\n");
}
#endif /* APP_TEST */
