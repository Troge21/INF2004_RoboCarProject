/* Local parser tests; no network connection or motor commands generated. */
#include "subsystem1.h"

#if APP_TEST
#include "../test_report.h"

EXPORT void subsystem1_test(void)
{
    static const unsigned char stop[] = {'S', 'T', 'O', 'P'};
    TEST_INIT();

    TEST_CHECK("S1-U14", s1_parse_command(stop, sizeof(stop)) == S1_COMMAND_STOP,
               "STOP accepted without NUL terminator");
    TEST_CHECK("S1-U15", s1_parse_command((const unsigned char *)"STATUS", 6) == S1_COMMAND_STATUS,
               "STATUS accepted");
    TEST_CHECK("S1-U16", s1_parse_command(0, 4) == S1_COMMAND_INVALID &&
               s1_parse_command(stop, 0) == S1_COMMAND_INVALID,
               "null and empty inputs rejected");
    TEST_CHECK("S1-U17", s1_parse_command((const unsigned char *)"GO", 2) == S1_COMMAND_INVALID &&
               s1_parse_command((const unsigned char *)"stop", 4) == S1_COMMAND_INVALID,
               "unknown and lowercase commands rejected");
    TEST_CHECK("S1-U18", s1_parse_command(stop, 3) == S1_COMMAND_INVALID,
               "incomplete command rejected");
    TEST_CHECK("S1-U19", s1_parse_command((const unsigned char *)"STOP NOW", 8) == S1_COMMAND_INVALID &&
               s1_parse_command((const unsigned char *)"STOP\n", 5) == S1_COMMAND_INVALID,
               "extra bytes and whitespace rejected");
    TEST_CHECK("S1-U20", s1_parse_command((const unsigned char *)"ST\0P", 4) == S1_COMMAND_INVALID &&
               s1_parse_command((const unsigned char *)"STOP", 5) == S1_COMMAND_INVALID,
               "embedded and trailing NUL rejected");
    TEST_CHECK("S1-U21", s1_parse_command(stop, 0xffffffffu) == S1_COMMAND_INVALID,
               "oversized length rejected before reading payload");
    TEST_SUMMARY("subsystem1");
}
#endif
