/* Packed v4 only. Explicit LE bytes/bits; native structs are never transmitted. */
#ifndef S1_PROTOCOL_H
#define S1_PROTOCOL_H
#include <stdint.h>
#include "../controller.h"
#define S1_CODEC_OK 0
#define S1_CODEC_INVALID (-1)
#define S1_FRAME_BYTES 46U
#define S1_BODY_BYTES 33U
#define S1_HEADER_BYTES 13U
#define S1_HEARTBEAT_BYTES 5U
#define S1_STATUS_REQUEST_BYTES 5U
#define S1_STATUS_HEADER_BYTES 16U
#define S1_STATUS_MAX_BYTES (S1_STATUS_HEADER_BYTES + S1_FRAME_BYTES)
#define S1_STATUS_NO_SAMPLE 2047U
#define S1_STOP_REQUEST_BYTES 13U
#define S1_STOP_REPLY_BYTES 11U
#define S1_RESUME_ARM_HEADER_BYTES 20U
#define S1_RESUME_ARM_MAX_BYTES (S1_RESUME_ARM_HEADER_BYTES+S1_FRAME_BYTES)
#define S1_BARCODE_MAP_REQUEST 0xa9U
#define S1_BARCODE_MAP_REPLY 0xaaU
#define S1_BARCODE_MAP_REQUEST_BYTES 14U
#define S1_BARCODE_MAP_REPLY_BYTES 8U
#define S1_COMMAND_MAX_BYTES S1_BARCODE_MAP_REQUEST_BYTES
#define S1_SCAN_HEADER_BYTES 16U
#define S1_SCAN_CHUNK_POINTS 16U
#define S1_SCAN_PACKET_BYTES 58U
#define S1_SCAN_COUNT_MASK 0x1fU
#define S1_SCAN_WIDTH_FLAG 0x80U
#define S1_DIAGNOSTIC_BYTES 40U
#define S1_TERRAIN_BYTES 15U
#define S1_TERRAIN_BODY_BYTES 6U
#define S1_TX_CAPACITY S1_RESUME_ARM_MAX_BYTES
#define S1_SCAN_PACKED_BYTES 453U
#define S1_SCAN_MAX_AGE_MS 10000U
#define S1_EXTERNAL_FAULT_MASK 0x700U
#define S1_RESOURCE_UNKNOWN 0x7ffffU
enum {
    S1_AGE_RANGE, S1_AGE_LINE, S1_AGE_IMU, S1_AGE_PITCH, S1_AGE_MOTION,
    S1_AGE_SPEED, S1_AGE_TRAVEL, S1_AGE_BARCODE, S1_AGE_FAULTS, S1_AGE_COUNT
};
enum {
    S1_B_RANGE=0, S1_B_LINE=12, S1_B_IMU=15, S1_B_LEFT=49, S1_B_RIGHT=63,
    S1_B_COUNT_L=77, S1_B_COUNT_R=109, S1_B_NET=141, S1_B_TRAVEL=173,
    S1_B_KNOWN=205, S1_B_FAULT=216, S1_B_BARCODE=227, S1_B_EVENT=234,
    S1_B_CONTROL=250, S1_B_MODE=256, S1_B_REASON=258, S1_B_NAV=261
};
typedef struct {
    uint8_t body[S1_BODY_BYTES], timestamp[S1_AGE_COUNT][4], sequence[4];
    uint8_t terrain[S1_TERRAIN_BODY_BYTES];
} S1_SNAPSHOT;
_Static_assert(sizeof(S1_SNAPSHOT)==79, "S1 snapshot must have no native padding");
typedef struct {
    uint32_t timestamp_ms, speed_timestamp_ms;
    int64_t left_speed_x10, right_speed_x10, left_count, right_count, net_mm_x10;
    unsigned encoder_valid, speed_valid, faults, mode, stop_reason;
} S1_MOTION_INPUT;
uint32_t s1_read_u32(const uint8_t *b);
void s1_write_u32(uint8_t *b,uint32_t value);
uint32_t s1_get_bits(const uint8_t *b,unsigned bit,unsigned width);
void s1_set_bits(uint8_t *b,unsigned bit,unsigned width,uint32_t value);
void s1_snapshot_init(S1_SNAPSHOT *out,uint32_t sequence);
int s1_set_range(S1_SNAPSHOT *,uint32_t mm,unsigned valid,uint32_t timestamp);
int s1_set_line(S1_SNAPSHOT *,unsigned pattern,unsigned valid,uint32_t timestamp);
int s1_set_imu(S1_SNAPSHOT *,int pitch_x10,unsigned current,unsigned maximum,
               unsigned state,unsigned flags,uint32_t timestamp,uint32_t pitch_timestamp);
int s1_set_motion(S1_SNAPSHOT *,const S1_MOTION_INPUT *);
int s1_set_travel(S1_SNAPSHOT *,uint64_t mm,uint32_t timestamp);
int s1_set_barcode(S1_SNAPSHOT *,unsigned code,unsigned navigation,uint16_t sequence,uint32_t timestamp);
int s1_set_faults(S1_SNAPSHOT *,unsigned known,unsigned active,uint32_t timestamp);
int s1_pack_frame(const S1_SNAPSHOT *,uint32_t source_now,uint32_t boot_now,
                   uint32_t session,unsigned control,uint8_t *out,unsigned capacity);
int s1_validate_frame(const uint8_t *,unsigned length);
int s1_pack_heartbeat(uint32_t counter,uint8_t *out,unsigned capacity);
int s1_parse_status_request(const uint8_t *,unsigned length,uint32_t *id);
int s1_pack_status_reply(uint32_t id,uint32_t uptime,unsigned age,unsigned control,
                          uint32_t session,const uint8_t *frame,uint8_t *out,unsigned capacity);
int s1_valid_control_word(unsigned word);
int s1_parse_stop_request(const uint8_t *,unsigned length,uint32_t *id,
                           uint32_t *session,uint32_t *deadline);
int s1_pack_stop_reply(uint32_t id,uint32_t session,unsigned result,unsigned control,
                        uint8_t *out,unsigned capacity);
/* a5 arm / a7 commit requests reuse id:32/session:32/deadline:32.
 * Arm returns a server-generated one-use ticket; commit echoes that ticket. */
int s1_parse_resume_request(const uint8_t *,unsigned,uint8_t marker,uint32_t *,uint32_t *,uint32_t *);
int s1_pack_resume_arm_reply(uint32_t id,uint32_t uptime,unsigned age,unsigned control,
                           uint32_t session,uint32_t ticket,const uint8_t *frame,uint8_t *out,unsigned capacity);
int s1_pack_resume_reply(uint32_t ticket,uint32_t session,unsigned result,unsigned control,
                         uint8_t *out,unsigned capacity);
/* scan is 181 * (angle:8,distance:12) packed bits, count 1..181.
 * A nonzero width adds a 10-bit value in a two-byte trailer on chunk zero. */
int s1_pack_scan(uint32_t session,uint32_t sequence,uint32_t source_boot_ms,
                  const uint8_t *scan,unsigned total,unsigned start,unsigned width_mm,
                  uint8_t *out,unsigned capacity);
/* Six 19-bit resource values; UINT32_MAX/unknown encode sentinel 0x7ffff.
 * Counters: MQTT publish failures, command rejections, MQTT connections,
 * S1 snapshot contention. These retain all 32 bits and wrap naturally. */
int s1_pack_diagnostics(uint32_t session,uint32_t uptime,const uint32_t values[6],
                         const uint32_t counters[4],uint8_t *out,unsigned capacity);
int s1_pack_terrain(uint32_t session, uint32_t uptime, const uint8_t terrain[S1_TERRAIN_BODY_BYTES],
                    uint8_t *out, unsigned capacity);
#endif
