/* Previous sample-data demo tests are retired; IDs remain in test logs. */
#include "subsystem3.h"

#if APP_TEST
EXPORT void subsystem3_test(void)
{
    tm_printf((UB *)"[TEST] subsystem3 SKIP: IR hardware checks are manual; enable S3_IR_LOG_ENABLED.\n");
}
#endif /* APP_TEST */
