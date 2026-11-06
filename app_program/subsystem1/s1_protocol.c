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
