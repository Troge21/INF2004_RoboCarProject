/* Report current actual S1 shared-cache layouts, using the real bridge source. */
#define main s1_host_test_main
#include "test_telemetry.c"
#undef main
int main(void)
{
    printf("{\"snapshot_bytes\":%u,\"publication_time_bytes\":%u,"
           "\"scan_cache_and_metadata_bytes\":%u,\"resources_bytes\":%u,"
           "\"input_output_message_buffer_bytes\":0,\"main_packet_bytes\":%u}\n",
           (unsigned)sizeof(publication),(unsigned)sizeof(publication_ms),
           (unsigned)(sizeof(scan_data)+sizeof(scan_time)+sizeof(scan_sequence)+sizeof(scan_count)),
           (unsigned)sizeof(resource_data),(unsigned)S1_FRAME_BYTES);
    return 0;
}
