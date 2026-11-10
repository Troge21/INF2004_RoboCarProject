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
#define S1_FRAME_BYTES 11U
#define S1_IMU_BYTES 5U
#define S1_IMU_MAX_AGE_MS 1000U
#define S1_IMU_PITCH_VALID 0x10U
#define S1_IMU_SPEED_SUPPLIED 0x20U
#define S1_IMU_FAULT 0x80U
#define S1_STATUS_REQUEST_BYTES 5U
#define S1_STATUS_HEADER_BYTES 11U
#define S1_STATUS_MAX_BYTES (S1_STATUS_HEADER_BYTES + S1_FRAME_BYTES)
#define S1_STATUS_NO_SAMPLE 2047U
/* STATUS is the only permitted request. Byte 0 is fixed 0xa0; the remaining
 * four bytes hold a nonzero uint32 correlation ID, little endian. */
int s1_parse_status_request(const uint8_t *bytes, unsigned length, uint32_t *id);
/* Reply: 0xa1, ID:32, uptime:32, age:11 + TEST-build:1 + reserved:4.
 * Age 2047 means no fresh sample and reply length 11. Otherwise age 0..2000
 * requires an appended current 11-byte frame, making the reply 22 bytes.
 * Invalid arguments leave out unchanged. */
int s1_pack_status_reply(uint32_t id, uint32_t uptime_ms, unsigned sample_age_ms,
                         unsigned test_build, const uint8_t *frame,
                         uint8_t *out, unsigned capacity);

/* seq:32, distance:12 (zero=unavailable), line:3, format marker:1. */
typedef struct { uint8_t bytes[S1_TELEMETRY_BYTES]; } S1_TELEMETRY;
/* Local freshness metadata: source uptime:32, line state:3 in one byte. */
typedef struct { uint8_t bytes[5]; } S1_LINE_TELEMETRY;
/* Five field bytes + source publication uptime:32 + pitch sample uptime:32. */
typedef struct { uint8_t bytes[13]; } S1_IMU_TELEMETRY;
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
/* V3 appends an IMU block to the unchanged six-byte V2 prefix. Fields:
 * pitch:11 (0 unavailable, 1..1801 = -900..900 tenths of a degree),
 * last accepted height:7, run max:7, S4 state:3 (7 unavailable), flags:6.
 * Flags carry sensor-ok/active/new-event/max-updated/speed-supplied/fault.
 * The top six bits of the fifth byte are reserved zero. */
int s1_make_imu(S1_IMU_TELEMETRY *out, int pitch_x10, unsigned current_mm,
                unsigned max_mm, unsigned state, unsigned flags,
                uint32_t sampled_ms, uint32_t pitch_sampled_ms);
int s1_pack_frame(const S1_TELEMETRY *sample, const S1_LINE_TELEMETRY *line,
                  const S1_IMU_TELEMETRY *imu, uint32_t now_ms,
                  uint32_t max_line_age_ms, uint8_t *out, unsigned capacity);
#endif
