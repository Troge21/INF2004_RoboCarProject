/* Local parser/format tests; do not inject fake samples into live telemetry. */
#include "subsystem1.h"

#if APP_TEST
#include "../test_report.h"
#ifdef PICO_RP2040
#include <sys/sysdef.h>
#include <tk/syslib.h>
#endif

static INT same_text(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

EXPORT void subsystem1_test(void)
{
    static const unsigned char stop[] = {'S', 'T', 'O', 'P'};
    S1_TELEMETRY sample = {1, 451, 1};
    S1_LINE_TELEMETRY line = {1000, 6};
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
    len = s1_format_telemetry(&sample, NULL, 0, payload, sizeof(payload));
    TEST_CHECK("S1-U22", len > 0 && same_text(payload,
               "{\"sequence\":1,\"obstacle_mm\":451,\"obstacle_valid\":true,\"line_bits\":0}"),
               "valid distance JSON");
    sample.obstacle_valid = 0;
    len = s1_format_telemetry(&sample, NULL, 0, payload, sizeof(payload));
    TEST_CHECK("S1-U23", len > 0 && same_text(payload,
               "{\"sequence\":1,\"obstacle_mm\":null,\"obstacle_valid\":false,\"line_bits\":0}"),
               "invalid distance is null, not clear space");
    TEST_CHECK("S1-U24", s1_format_telemetry(&sample, NULL, 0, small, sizeof(small)) == E_PAR &&
               small[0] == '\0' && small[3] == 'x',
               "small output buffer rejected without overflow");
    sample.obstacle_valid = 1;
    sample.obstacle_mm = 4001;
    TEST_CHECK("S1-U25", s1_submit_telemetry(&sample) == E_PAR &&
               s1_submit_telemetry(NULL) == E_PAR &&
               s1_format_telemetry(&sample, NULL, 0, payload, sizeof(payload)) == E_PAR,
               "null and out-of-range sample rejected");
    sample.sequence = 0xffffffffu;
    sample.obstacle_mm = 4000;
    len = s1_format_telemetry(&sample, NULL, 0, payload, sizeof(payload));
    TEST_CHECK("S1-U26", len > 0 && same_text(payload,
               "{\"sequence\":4294967295,\"obstacle_mm\":4000,\"obstacle_valid\":true,\"line_bits\":0}"),
               "maximum sequence and distance fit payload");
    sample.obstacle_valid = 2;
    TEST_CHECK("S1-U27", s1_format_telemetry(&sample, NULL, 0, payload, sizeof(payload)) == E_PAR &&
               s1_format_telemetry(&sample, NULL, 0, NULL, sizeof(payload)) == E_PAR &&
               s1_format_telemetry(&sample, NULL, 0, payload, 0) == E_PAR,
               "invalid validity flag and output arguments rejected");
    sample.sequence = 1;
    sample.obstacle_mm = 451;
    sample.obstacle_valid = 1;
    TEST_CHECK("S1-U28", s1_format_telemetry(&sample, &line, 1100, payload, sizeof(payload)) > 0 &&
               same_text(payload, "{\"sequence\":1,\"obstacle_mm\":451,\"obstacle_valid\":true,\"line_bits\":6}"),
               "centre black and right background remain valid at 100 ms");
    line.bits = 4;
    TEST_CHECK("S1-U29", s1_format_telemetry(&sample, &line, 1000, payload, sizeof(payload)) > 0 &&
               same_text(payload, "{\"sequence\":1,\"obstacle_mm\":451,\"obstacle_valid\":true,\"line_bits\":4}"),
               "valid all-background differs from unavailable");
    line.bits = 7;
    TEST_CHECK("S1-U30", s1_format_telemetry(&sample, &line, 1101, payload, sizeof(payload)) > 0 &&
               same_text(payload, "{\"sequence\":1,\"obstacle_mm\":451,\"obstacle_valid\":true,\"line_bits\":0}"),
               "IR expires after 100 ms without losing distance");
    line.bits = 5;
    line.timestamp_ms = 0xfffffff0U;
    TEST_CHECK("S1-U31", s1_format_telemetry(&sample, &line, 16, payload, sizeof(payload)) > 0 &&
               same_text(payload, "{\"sequence\":1,\"obstacle_mm\":451,\"obstacle_valid\":true,\"line_bits\":5}"),
               "right black snapshot stays fresh across uptime wrap");
    line.timestamp_ms = 1001;
    TEST_CHECK("S1-U32", s1_format_telemetry(&sample, &line, 1000, payload, sizeof(payload)) > 0 &&
               same_text(payload, "{\"sequence\":1,\"obstacle_mm\":451,\"obstacle_valid\":true,\"line_bits\":0}"),
               "future timestamp cannot appear fresh");
    line.bits = 3;
    len = s1_format_telemetry(&sample, &line, 1001, payload, sizeof(payload));
    line.bits = 8;
    TEST_CHECK("S1-U33", len == E_PAR &&
               s1_format_telemetry(&sample, &line, 1001, payload, sizeof(payload)) == E_PAR &&
               payload[0] == '\0', "noncanonical or reserved line bits rejected");
#ifdef PICO_RP2040
    {
        UW raw_before, elapsed_us, tick;
        UINT intsts;
        /* A kernel sleep can lose clock ticks during long masked startup
         * work in another driver. Use a 1 ms CPU/SysTick busy interval,
         * with local interrupts masked, to check only the hardware timebase.
         * This is shorter than the 10 ms kernel tick; no kernel calls occur
         * while interrupts are masked. */
        DI(intsts);
        raw_before = in_w(TIMER_TIMERAWL);
        WaitUsec(1000);
        elapsed_us = (UW)(in_w(TIMER_TIMERAWL) - raw_before);
        tick = in_w(0x4005802cU);
        EI(intsts);
        tm_printf((UB *)"[CLOCK] tick=%u timer_us=%u cpu_wait_us=1000\n",
                  (unsigned int)tick, (unsigned int)elapsed_us);
        TEST_CHECK("S1-U43", (tick & 0x3ffU) == ((1U << 9) | XOSC_MHz) &&
                   (tick & (1U << 10)) != 0 &&
                   elapsed_us >= 900U && elapsed_us <= 1100U,
                   "microsecond TIMER configured and advancing at 1 MHz");
    }
#endif
    TEST_SUMMARY("subsystem1");
}
#endif
