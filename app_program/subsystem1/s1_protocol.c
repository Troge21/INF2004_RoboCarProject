#include "s1_protocol.h"

static unsigned read_word(const S1_TELEMETRY *sample)
{
    return (unsigned)sample->bytes[4] | ((unsigned)sample->bytes[5] << 8);
}
static int valid_line(unsigned bits)
{
    return bits == 0U || (bits >= S1_LINE_VALID && bits <= 7U);
}
uint32_t s1_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}
void s1_write_u32(uint8_t *bytes, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (8U * i));
}
int s1_make_telemetry(S1_TELEMETRY *out, uint32_t sequence,
                      uint32_t distance_mm, unsigned valid)
{
    unsigned word;
    if (out == 0 || valid > 1U ||
        (valid && (distance_mm < 20U || distance_mm > 4000U)))
        return S1_CODEC_INVALID;
    word = S1_V2_TAG | (valid ? (unsigned)distance_mm : 0U);
    s1_write_u32(out->bytes, sequence);
    out->bytes[4] = (uint8_t)word;
    out->bytes[5] = (uint8_t)(word >> 8);
    return S1_CODEC_OK;
}
int s1_validate_telemetry(const S1_TELEMETRY *sample)
{
    unsigned word, distance;
    if (sample == 0) return S1_CODEC_INVALID;
    word = read_word(sample);
    distance = word & S1_DISTANCE_MASK;
    if (!(word & S1_V2_TAG) ||
        (distance != 0U && (distance < 20U || distance > 4000U)) ||
        !valid_line((word & S1_LINE_MASK) >> 12)) return S1_CODEC_INVALID;
    return S1_CODEC_OK;
}
uint32_t s1_sequence(const S1_TELEMETRY *sample)
{
    return s1_read_u32(sample->bytes);
}
unsigned s1_distance(const S1_TELEMETRY *sample)
{
    return read_word(sample) & S1_DISTANCE_MASK;
}
unsigned s1_line_bits(const S1_TELEMETRY *sample)
{
    return (read_word(sample) & S1_LINE_MASK) >> 12;
}
int s1_make_line(S1_LINE_TELEMETRY *out, unsigned pattern, unsigned valid,
                 uint32_t sampled_ms)
{
    if (out == 0 || pattern > 3U || valid > 1U) return S1_CODEC_INVALID;
    s1_write_u32(out->bytes, valid ? sampled_ms : 0U);
    out->bytes[4] = valid ? (uint8_t)(S1_LINE_VALID | pattern) : 0U;
    return S1_CODEC_OK;
}
int s1_pack_telemetry(const S1_TELEMETRY *sample,
                      const S1_LINE_TELEMETRY *line, uint32_t now_ms,
                      uint32_t max_line_age_ms, uint8_t *out, unsigned capacity)
{
    unsigned bits = 0, word, i;
    if (out == 0 || capacity < S1_TELEMETRY_BYTES ||
        s1_validate_telemetry(sample) != S1_CODEC_OK) return S1_CODEC_INVALID;
    if (line != 0) {
        if (!valid_line(line->bytes[4])) return S1_CODEC_INVALID;
        if ((uint32_t)(now_ms - s1_read_u32(line->bytes)) <= max_line_age_ms)
            bits = line->bytes[4];
    }
    word = S1_V2_TAG | s1_distance(sample) | (bits << 12);
    /* Permit in-place serialization, but validate everything before writing. */
    for (i = 0; i < 4; ++i) out[i] = sample->bytes[i];
    out[4] = (uint8_t)word;
    out[5] = (uint8_t)(word >> 8);
    return S1_TELEMETRY_BYTES;
}
int s1_pack_heartbeat(uint32_t counter, uint8_t *out, unsigned capacity)
{
    if (out == 0 || capacity < S1_HEARTBEAT_BYTES) return S1_CODEC_INVALID;
    out[0] = 0x80U;
    s1_write_u32(out + 1, counter);
    return S1_HEARTBEAT_BYTES;
}

static uint64_t imu_word(const uint8_t *bytes)
{
    return (uint64_t)s1_read_u32(bytes) | ((uint64_t)bytes[4] << 32);
}
static int valid_height(unsigned height)
{
    return height == 0U || (height >= 6U && height <= 80U);
}
static int valid_imu(uint64_t word)
{
    unsigned pitch = (unsigned)(word & 0x7ffU);
    unsigned current = (unsigned)((word >> 11) & 127U);
    unsigned max = (unsigned)((word >> 18) & 127U);
    unsigned state = (unsigned)((word >> 25) & 7U);
    unsigned flags = (unsigned)((word >> 28) & 63U);
    if ((word >> 34) != 0U || pitch > 1801U ||
        !valid_height(current) || !valid_height(max) || current > max)
        return 0;
    if (state == 7U) return word == ((uint64_t)7U << 25);
    if ((flags & 1U) && (flags & 32U)) return 0;
    if (pitch && (!(flags & 1U) || (flags & 32U) || state < 2U || state > 5U))
        return 0;
    return 1;
}
int s1_make_imu(S1_IMU_TELEMETRY *out, int pitch_x10, unsigned current_mm,
                unsigned max_mm, unsigned state, unsigned flags,
                uint32_t sampled_ms, uint32_t pitch_sampled_ms)
{
    uint64_t word;
    unsigned pitch, compact_flags, i;
    if (out == 0 || state > 7U || (flags & ~0xbfU) ||
        !valid_height(current_mm) || !valid_height(max_mm) || current_mm > max_mm ||
        ((flags & S1_IMU_PITCH_VALID) && (pitch_x10 < -900 || pitch_x10 > 900)))
        return S1_CODEC_INVALID;
    pitch = flags & S1_IMU_PITCH_VALID ? (unsigned)(pitch_x10 + 901) : 0U;
    compact_flags = (flags & 15U) | ((flags & S1_IMU_SPEED_SUPPLIED) >> 1) |
                    ((flags & S1_IMU_FAULT) >> 2);
    word = pitch | ((uint64_t)current_mm << 11) | ((uint64_t)max_mm << 18) |
           ((uint64_t)state << 25) | ((uint64_t)compact_flags << 28);
    if (!valid_imu(word)) return S1_CODEC_INVALID;
    for (i = 0; i < S1_IMU_BYTES; ++i) out->bytes[i] = (uint8_t)(word >> (8U * i));
    s1_write_u32(out->bytes + S1_IMU_BYTES, sampled_ms);
    s1_write_u32(out->bytes + S1_IMU_BYTES + 4U, pitch_sampled_ms);
    return S1_CODEC_OK;
}
int s1_pack_frame(const S1_TELEMETRY *sample, const S1_LINE_TELEMETRY *line,
                  const S1_IMU_TELEMETRY *imu, uint32_t now_ms,
                  uint32_t max_line_age_ms, uint8_t *out, unsigned capacity)
{
    uint8_t frame[S1_FRAME_BYTES];
    uint64_t word = (uint64_t)7U << 25; /* unavailable, not an all-zero reading */
    unsigned i;
    if (out == 0 || capacity < S1_FRAME_BYTES ||
        s1_pack_telemetry(sample, line, now_ms, max_line_age_ms, frame,
                          S1_TELEMETRY_BYTES) != (int)S1_TELEMETRY_BYTES)
        return S1_CODEC_INVALID;
    if (imu != 0) {
        uint64_t candidate = imu_word(imu->bytes);
        if (!valid_imu(candidate)) return S1_CODEC_INVALID;
        if ((uint32_t)(now_ms - s1_read_u32(imu->bytes + S1_IMU_BYTES)) <= S1_IMU_MAX_AGE_MS) {
            word = candidate;
            /* A queued angle can expire before the status/retained heights. */
            if ((uint32_t)(now_ms - s1_read_u32(imu->bytes + S1_IMU_BYTES + 4U)) > 100U)
                word &= ~((uint64_t)0x7ffU);
        }
    }
    for (i = 0; i < S1_IMU_BYTES; ++i) frame[S1_TELEMETRY_BYTES + i] = (uint8_t)(word >> (8U * i));
    for (i = 0; i < S1_FRAME_BYTES; ++i) out[i] = frame[i];
    return S1_FRAME_BYTES;
}
