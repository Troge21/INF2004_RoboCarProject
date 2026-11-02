/* Local parser/format tests; do not inject fake samples into live telemetry. */
#include "subsystem1.h"

#if APP_TEST
#include "../test_report.h"

static INT same_text(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

EXPORT void subsystem1_test(void)
{
    static const unsigned char stop[] = {'S', 'T', 'O', 'P'};
    S1_TELEMETRY sample = {1, 451, 1};
    char payload[S1_PAYLOAD_CAPACITY];
    char small[4] = {'x', 'x', 'x', 'x'};
    INT len;
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
    len = s1_format_telemetry(&sample, payload, sizeof(payload));
    TEST_CHECK("S1-U22", len > 0 && same_text(payload,
               "{\"sequence\":1,\"obstacle_mm\":451,\"obstacle_valid\":true}"),
               "valid distance JSON");
    sample.obstacle_valid = 0;
    len = s1_format_telemetry(&sample, payload, sizeof(payload));
    TEST_CHECK("S1-U23", len > 0 && same_text(payload,
               "{\"sequence\":1,\"obstacle_mm\":null,\"obstacle_valid\":false}"),
               "invalid distance is null, not clear space");
    TEST_CHECK("S1-U24", s1_format_telemetry(&sample, small, sizeof(small)) == E_PAR &&
               small[0] == '\0' && small[3] == 'x',
               "small output buffer rejected without overflow");
    sample.obstacle_valid = 1;
    sample.obstacle_mm = 4001;
    TEST_CHECK("S1-U25", s1_submit_telemetry(&sample) == E_PAR &&
               s1_submit_telemetry(NULL) == E_PAR &&
               s1_format_telemetry(&sample, payload, sizeof(payload)) == E_PAR,
               "null and out-of-range sample rejected");
    sample.sequence = 0xffffffffu;
    sample.obstacle_mm = 4000;
    len = s1_format_telemetry(&sample, payload, sizeof(payload));
    TEST_CHECK("S1-U26", len > 0 && same_text(payload,
               "{\"sequence\":4294967295,\"obstacle_mm\":4000,\"obstacle_valid\":true}"),
               "maximum sequence and distance fit payload");
    sample.obstacle_valid = 2;
    TEST_CHECK("S1-U27", s1_format_telemetry(&sample, payload, sizeof(payload)) == E_PAR &&
               s1_format_telemetry(&sample, NULL, sizeof(payload)) == E_PAR &&
               s1_format_telemetry(&sample, payload, 0) == E_PAR,
               "invalid validity flag and output arguments rejected");
    TEST_SUMMARY("subsystem1");
}
#endif
