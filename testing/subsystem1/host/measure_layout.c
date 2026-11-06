/* Measure actual application layouts from the host harness's real C sources.
 * All scalar aliases in that harness match the 32-bit RP2040 widths.
 * The packed revision also verifies these layouts with ARM static assertions.
 */
#define main s1_host_test_main
#include "test_telemetry.c"
#undef main
#ifndef S1_MBF_STORAGE
/* Also usable with the original aligned records in the baseline checkout. */
#define S1_MBF_STORAGE(bytes) (4U * (sizeof(INT) + \
    (((bytes) + sizeof(INT) - 1U) / sizeof(INT)) * sizeof(INT)))
#endif
int main(void)
{
    unsigned example_bytes;
#ifdef S1_TELEMETRY_BYTES
    example_bytes = S1_TELEMETRY_BYTES;
#else
    S1_TELEMETRY sample = {12, 451, 1};
    S1_LINE_TELEMETRY line = {1000, 6};
    char payload[S1_PAYLOAD_CAPACITY];
    example_bytes = (unsigned)s1_format_telemetry(&sample, &line, 1000,
                                                 payload, sizeof(payload));
#endif
    printf("{\"sample_bytes\":%u,\"line_snapshot_bytes\":%u,"
           "\"input_record_bytes\":%u,\"output_record_bytes\":%u,"
           "\"input_buffer_bytes\":%u,\"output_buffer_bytes\":%u,"
           "\"example_telemetry_bytes\":%u}\n",
           (unsigned)sizeof(S1_TELEMETRY), (unsigned)sizeof(S1_LINE_TELEMETRY),
           (unsigned)sizeof(S1_INPUT), (unsigned)sizeof(S1_OUTPUT),
           (unsigned)S1_MBF_STORAGE(sizeof(S1_INPUT)),
           (unsigned)S1_MBF_STORAGE(sizeof(S1_OUTPUT)), example_bytes);
    return 0;
}
