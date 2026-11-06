/* Offline C -> browser protocol evidence. Emits no network traffic. */
#include <stdio.h>
#include "../../../app_program/subsystem1/s1_protocol.h"

static int emit(uint32_t sequence, unsigned mm, unsigned bits, uint32_t age)
{
    S1_TELEMETRY sample;
    S1_LINE_TELEMETRY line;
    unsigned char bytes[6];
    unsigned i;
    if (s1_make_telemetry(&sample, sequence, mm, mm != 0) != S1_CODEC_OK ||
        s1_make_line(&line, bits & 3U, bits != 0, 1000) != S1_CODEC_OK ||
        s1_pack_telemetry(&sample, &line, 1000 + age, 100, bytes, sizeof(bytes)) != 6)
        return 1;
    printf("{\"kind\":\"telemetry\",\"sequence\":%u,\"distance\":%u,\"line\":%u,\"hex\":\"",
           (unsigned)sequence, mm, age > 100 ? 0U : bits);
    for (i = 0; i < sizeof(bytes); ++i) printf("%02x", bytes[i]);
    puts("\"}");
    return 0;
}
int main(void)
{
    const unsigned states[] = {0, 4, 5, 6, 7};
    const uint32_t counters[] = {0, 12, 0xffffffffU};
    unsigned mm, j, k;
    unsigned char heartbeat[5];
    S1_TELEMETRY rejected;
    for (mm = 0; mm <= 4000; ++mm) {
        if (mm > 0 && mm < 20) continue;
        for (j = 0; j < 5; ++j)
            if (emit((uint32_t)(mm * 8U + j), mm, states[j], 0)) return 1;
    }
    if (emit(12, 451, 6, 0) || emit(0xffffffffU, 4000, 7, 0) ||
        emit(0x12345678U, 20, 5, 100) || emit(0, 451, 7, 101)) return 1;
    for (mm = 1; mm < 20; ++mm)
        if (s1_make_telemetry(&rejected, 0, mm, 1) != S1_CODEC_INVALID) return 2;
    for (mm = 4001; mm <= 4095; ++mm)
        if (s1_make_telemetry(&rejected, 0, mm, 1) != S1_CODEC_INVALID) return 2;
    for (j = 0; j < 3; ++j) {
        if (s1_pack_heartbeat(counters[j], heartbeat, sizeof(heartbeat)) != 5) return 1;
        printf("{\"kind\":\"heartbeat\",\"counter\":%u,\"hex\":\"", (unsigned)counters[j]);
        for (k = 0; k < sizeof(heartbeat); ++k) printf("%02x", heartbeat[k]);
        puts("\"}");
    }
    return 0;
}
