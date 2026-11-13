/* Software conversion checks only; hardware checks are in TEST_LOG.md. */
#include "subsystem5.h"
#include "../test_report.h"

#if APP_TEST
EXPORT void subsystem5_test(void)
{
    UW mm;
    TEST_INIT();
    /* S5-U01 remains the retired template test; do not reuse its ID. */
    TEST_CHECK("S5-U03", s5_echo_to_mm(580, &mm) == E_OK && mm == 100 &&
               s5_echo_to_mm(5800, &mm) == E_OK && mm == 1000,
               "echo widths convert to 100 and 1000 mm");
    TEST_CHECK("S5-U04", s5_echo_to_mm(116, &mm) == E_OK && mm == 20 &&
               s5_echo_to_mm(23200, &mm) == E_OK && mm == 4000,
               "specified 20 to 4000 mm range accepted");
    TEST_CHECK("S5-U05", s5_echo_to_mm(0, &mm) == E_PAR && mm == 0 &&
               s5_echo_to_mm(115, &mm) == E_PAR && mm == 0 &&
               s5_echo_to_mm(23201, &mm) == E_PAR && mm == 0,
               "invalid echo widths rejected and distance cleared");
    TEST_CHECK("S5-U06",
               s5_choose_motion(TRUE,251,FALSE,0,FALSE,0)==S5_MOTION_FORWARD &&
               s5_choose_motion(TRUE,100,TRUE,500,TRUE,300)==S5_MOTION_LEFT &&
               s5_choose_motion(TRUE,100,TRUE,300,TRUE,500)==S5_MOTION_RIGHT,
               "clear centre permits line following; blocked centre chooses a clear side");
    TEST_CHECK("S5-U07",
               s5_choose_motion(FALSE,0,TRUE,500,TRUE,500)==S5_MOTION_STOP &&
               s5_choose_motion(TRUE,100,FALSE,0,FALSE,0)==S5_MOTION_STOP,
               "unknown centre or missing escape echoes cannot permit movement");
    TEST_CHECK("S5-U08",
               s5_choose_motion(TRUE,100,TRUE,240,TRUE,260)==S5_MOTION_RIGHT &&
               s5_choose_motion(TRUE,100,TRUE,250,TRUE,251)==S5_MOTION_RIGHT,
               "tie margin never prefers a blocked side over a clear side");
    TEST_SUMMARY("subsystem5 conversion (hardware not checked)");
}
#endif
