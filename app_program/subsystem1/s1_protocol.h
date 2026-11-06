/* Kernel/SDK-independent packed application protocol, explicit little endian.
 * No C bit-field layout or unaligned native loads are used on the wire.
 */
#ifndef S1_PROTOCOL_H
#define S1_PROTOCOL_H
#include <stdint.h>

#define S1_TELEMETRY_BYTES 6U
#define S1_HEARTBEAT_BYTES 5U
#define S1_LINE_VALID 4U
#define S1_CODEC_OK 0
#define S1_CODEC_INVALID (-1)
#define S1_V2_TAG 0x8000U
#define S1_DISTANCE_MASK 0x0fffU
#define S1_LINE_MASK 0x7000U

/* seq:32, distance:12 (zero=unavailable), line:3, format marker:1. */
typedef struct { uint8_t bytes[S1_TELEMETRY_BYTES]; } S1_TELEMETRY;
/* Local freshness metadata: source uptime:32, line state:3 in one byte. */
typedef struct { uint8_t bytes[5]; } S1_LINE_TELEMETRY;
_Static_assert(sizeof(S1_TELEMETRY) == 6, "packed telemetry must be six bytes");
_Static_assert(sizeof(S1_LINE_TELEMETRY) == 5, "packed line snapshot must be five bytes");

uint32_t s1_read_u32(const uint8_t *bytes);
void s1_write_u32(uint8_t *bytes, uint32_t value);
int s1_make_telemetry(S1_TELEMETRY *out, uint32_t sequence,
                      uint32_t distance_mm, unsigned valid);
int s1_validate_telemetry(const S1_TELEMETRY *sample);
uint32_t s1_sequence(const S1_TELEMETRY *sample);
unsigned s1_distance(const S1_TELEMETRY *sample);
unsigned s1_line_bits(const S1_TELEMETRY *sample);
int s1_make_line(S1_LINE_TELEMETRY *out, unsigned pattern, unsigned valid,
                 uint32_t sampled_ms);
/* No terminator is written; positive return is the exact binary byte count.
 * Errors leave out unchanged. line=NULL means unavailable IR. */
int s1_pack_telemetry(const S1_TELEMETRY *sample,
                      const S1_LINE_TELEMETRY *line, uint32_t now_ms,
                      uint32_t max_line_age_ms, uint8_t *out, unsigned capacity);
/* One format-marker byte (0x80) followed by the full 32-bit counter LE.
 * The marker prevents ambiguity with legacy ASCII heartbeats. */
int s1_pack_heartbeat(uint32_t counter, uint8_t *out, unsigned capacity);
#endif
