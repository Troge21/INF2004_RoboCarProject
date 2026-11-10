/* Local parser/format tests; do not inject fake samples into live telemetry. */
#include "subsystem1.h"

#if APP_TEST
#include "../test_report.h"
#ifdef PICO_RP2040
#include <sys/sysdef.h>
#include <tk/syslib.h>
#endif

static INT same_bytes(const UB *a, const UB *b, UINT count)
{
    UINT i;
    for (i = 0; i < count; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

EXPORT void subsystem1_test(void)
{
    static const unsigned char stop[] = {'S', 'T', 'O', 'P'};
    S1_TELEMETRY sample = {{0}}, bad;
    S1_LINE_TELEMETRY line = {{0}};
    UB payload[S1_PAYLOAD_CAPACITY];
    UB small[4] = {0xa5, 0xa5, 0xa5, 0xa5};
    const UB plain[6] = {1, 0, 0, 0, 0xc3, 0x81};
    const UB missing[6] = {1, 0, 0, 0, 0, 0x80};
    const UB maximum[6] = {255, 255, 255, 255, 0xa0, 0x8f};
    const UB left[6] = {1, 0, 0, 0, 0xc3, 0xe1};
    const UB white[6] = {1, 0, 0, 0, 0xc3, 0xc1};
    const UB right[6] = {1, 0, 0, 0, 0xc3, 0xd1};
    const UB hb_zero[5] = {0x80, 0, 0, 0, 0};
    const UB hb_max[5] = {0x80, 255, 255, 255, 255};
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
    (void)s1_make_telemetry(&sample, 1, 451, 1);
    len = s1_encode_telemetry(&sample, NULL, 0, payload, sizeof(payload));
    TEST_CHECK("S1-U22", len == 6 && same_bytes(payload, plain, 6),
               "distance has exact six-byte binary golden vector");
    (void)s1_make_telemetry(&sample, 1, 451, 0);
    TEST_CHECK("S1-U23", s1_encode_telemetry(&sample, NULL, 0, payload, sizeof(payload)) == 6 &&
               same_bytes(payload, missing, 6),
               "invalid distance uses zero sentinel, never clear-space distance");
    TEST_CHECK("S1-U24", s1_encode_telemetry(&sample, NULL, 0, small, sizeof(small)) == E_PAR &&
               small[0] == 0xa5 && small[3] == 0xa5,
               "small binary buffer rejected without writing");
    bad = sample; bad.bytes[4] = 0xa1; bad.bytes[5] = 0x8f;
    TEST_CHECK("S1-U25", s1_make_telemetry(&sample, 1, 4001, 1) == S1_CODEC_INVALID &&
               s1_submit_telemetry(&bad) == E_PAR && s1_submit_telemetry(NULL) == E_PAR,
               "null and out-of-range samples rejected before queue access");
    (void)s1_make_telemetry(&sample, 0xffffffffU, 4000, 1);
    TEST_CHECK("S1-U26", s1_encode_telemetry(&sample, NULL, 0, payload, sizeof(payload)) == 6 &&
               same_bytes(payload, maximum, 6), "maximum sequence and distance preserved");
    TEST_CHECK("S1-U27", s1_make_telemetry(&sample, 1, 451, 2) == S1_CODEC_INVALID &&
               s1_encode_telemetry(&sample, NULL, 0, NULL, 6) == E_PAR &&
               s1_encode_telemetry(&sample, NULL, 0, payload, 0) == E_PAR,
               "invalid validity and output arguments rejected");
    (void)s1_make_telemetry(&sample, 1, 451, 1);
    (void)s1_make_line(&line, 2, 1, 1000);
    TEST_CHECK("S1-U28", s1_encode_telemetry(&sample, &line, 1100, payload, sizeof(payload)) == 6 &&
               same_bytes(payload, left, 6), "left line black remains valid at age 100 ms");
    (void)s1_make_line(&line, 0, 1, 1000);
    TEST_CHECK("S1-U29", s1_encode_telemetry(&sample, &line, 1000, payload, sizeof(payload)) == 6 &&
               same_bytes(payload, white, 6), "valid background differs from unavailable");
    (void)s1_make_line(&line, 3, 1, 1000);
    TEST_CHECK("S1-U30", s1_encode_telemetry(&sample, &line, 1101, payload, sizeof(payload)) == 6 &&
               same_bytes(payload, plain, 6), "queued IR expires without losing distance");
    (void)s1_make_line(&line, 1, 1, 0xfffffff0U);
    TEST_CHECK("S1-U31", s1_encode_telemetry(&sample, &line, 16, payload, sizeof(payload)) == 6 &&
               same_bytes(payload, right, 6), "freshness survives uptime wrap");
    (void)s1_make_line(&line, 1, 1, 1001);
    TEST_CHECK("S1-U32", s1_encode_telemetry(&sample, &line, 1000, payload, sizeof(payload)) == 6 &&
               same_bytes(payload, plain, 6), "future timestamp cannot appear fresh");
    payload[0] = 0xa5; line.bytes[4] = 3;
    len = s1_encode_telemetry(&sample, &line, 1001, payload, sizeof(payload));
    line.bytes[4] = 8;
    TEST_CHECK("S1-U33", len == E_PAR &&
               s1_encode_telemetry(&sample, &line, 1001, payload, sizeof(payload)) == E_PAR &&
               payload[0] == 0xa5, "noncanonical line bits rejected without writing");
    TEST_CHECK("S1-U44", sizeof(S1_TELEMETRY) == 6 && sizeof(S1_LINE_TELEMETRY) == 5,
               "application snapshots use packed byte layouts");
    len = s1_pack_heartbeat(0, payload, sizeof(payload));
    TEST_CHECK("S1-U45", len == 5 && same_bytes(payload, hb_zero, 5) &&
               s1_pack_heartbeat(0xffffffffU, payload, sizeof(payload)) == 5 &&
               same_bytes(payload, hb_max, 5), "binary heartbeat preserves zero and full counter");
    TEST_CHECK("S1-U46", s1_pack_heartbeat(1, small, sizeof(small)) == S1_CODEC_INVALID &&
               small[0] == 0xa5 && small[3] == 0xa5 &&
               s1_pack_heartbeat(1, NULL, 5) == S1_CODEC_INVALID,
               "heartbeat output bounds preserved");
    TEST_CHECK("S1-U47", s1_make_telemetry(&sample, 1, 4096U + 451U, 1) == S1_CODEC_INVALID &&
               s1_make_line(&line, 4, 1, 1000) == S1_CODEC_INVALID,
               "raw values rejected before masking could turn them into valid data");
    {
        S1_IMU_TELEMETRY imu;
        const UB frame[11] = {1,0,0,0,0xc3,0xe1,0x1b,0x64,0x88,0x14,0};
        (void)s1_make_telemetry(&sample, 1, 451, 1);
        (void)s1_make_line(&line, 2, 1, 1000);
        len = s1_make_imu(&imu, 150, 12, 34, 2, 0x11, 1000, 1000);
        TEST_CHECK("S1-U48", len == 0 && sizeof(imu) == 13 &&
            s1_pack_frame(&sample, &line, &imu, 1000, 100, payload, sizeof(payload)) == 11 &&
            same_bytes(payload, frame, 11), "V3 matches independent 11-byte pitch/height golden vector");
        (void)s1_make_imu(&imu, -900, 0, 0, 2, 0x11, 1000, 1000);
        (void)s1_pack_frame(&sample, NULL, &imu, 1000, 100, payload, sizeof(payload));
        len = payload[6] == 1 && (payload[7] & 7U) == 0;
        (void)s1_make_imu(&imu, 900, 80, 80, 2, 0x31, 1000, 1000);
        (void)s1_pack_frame(&sample, NULL, &imu, 1000, 100, payload, sizeof(payload));
        TEST_CHECK("S1-U49", len && (s1_read_u32(payload + 6) & 2047U) == 1801U,
            "full signed pitch range and maximum accepted height fit without truncation");
        TEST_CHECK("S1-U50", s1_make_imu(&imu, 901, 0, 0, 2, 0x11, 0, 0) < 0 &&
            s1_make_imu(&imu, -901, 0, 0, 2, 0x11, 0, 0) < 0 &&
            s1_make_imu(&imu, 0, 81, 81, 2, 1, 0, 0) < 0 &&
            s1_make_imu(&imu, 0, 0, 0, 8, 1, 0, 0) < 0,
            "IMU raw range errors rejected before packing");
        (void)s1_make_imu(&imu, 150, 12, 34, 2, 0x11, 1000, 1000);
        (void)s1_pack_frame(&sample, NULL, &imu, 1100, 100, payload, sizeof(payload));
        len = (s1_read_u32(payload + 6) & 2047U) == 1051U;
        (void)s1_pack_frame(&sample, NULL, &imu, 1101, 100, payload, sizeof(payload));
        len = len && (s1_read_u32(payload + 6) & 2047U) == 0U &&
            ((s1_read_u32(payload + 6) >> 18) & 127U) == 34U;
        (void)s1_pack_frame(&sample, NULL, &imu, 2001, 100, payload, sizeof(payload));
        TEST_CHECK("S1-U51", len && s1_read_u32(payload + 6) == (7U << 25) && payload[10] == 0,
            "queued pitch expires independently, then the whole stale IMU block expires");
        (void)s1_make_imu(&imu, -150, 0, 0, 2, 0x11, 0xfffffff0U, 0xfffffff0U);
        (void)s1_pack_frame(&sample, NULL, &imu, 16, 100, payload, sizeof(payload));
        len = (s1_read_u32(payload + 6) & 2047U) == 751U;
        (void)s1_make_imu(&imu, 0, 0, 0, 2, 0x11, 17, 17);
        (void)s1_pack_frame(&sample, NULL, &imu, 16, 100, payload, sizeof(payload));
        TEST_CHECK("S1-U52", len && s1_read_u32(payload + 6) == (7U << 25),
            "IMU freshness handles uptime wrap and rejects future source time");
        imu.bytes[4] |= 0x80U; payload[0] = 0xa5;
        TEST_CHECK("S1-U53", s1_pack_frame(&sample, NULL, &imu, 17, 100, payload, sizeof(payload)) < 0 &&
            payload[0] == 0xa5, "reserved IMU bits rejected without changing output");
        TEST_CHECK("S1-U54", s1_pack_frame(&sample, NULL, NULL, 0, 100, small, sizeof(small)) < 0 &&
            small[0] == 0xa5 && s1_pack_frame(&sample, NULL, NULL, 0, 100, NULL, 11) < 0,
            "V3 buffer boundaries preserved");
        TEST_CHECK("S1-U55", s1_make_imu(&imu, 0, 20, 12, 2, 1, 0, 0) < 0 &&
            s1_make_imu(&imu, 0, 0, 0, 6, 0x91, 0, 0) < 0 &&
            s1_make_imu(&imu, 0, 0, 0, 7, 1, 0, 0) < 0 &&
            s1_make_imu(&imu, 0, 0, 0, 1, 0x11, 0, 0) < 0,
            "contradictory validity, unavailable fields and height ordering rejected");
    }
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
