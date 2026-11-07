/* Barcode decoding: turn measured black/white durations into a validated
 * one-character Code 39 result, including forward/reverse scan direction. */
#include "barcode_decoder.h"
#include <stddef.h>

#define BC_STAR 0x094U
static const char s_alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";
/* Nine width bits in scan order: 1=wide, 0=narrow, starting with black.
 * Reference: standard Code 39; cross-checked against ReportLab's encoder. */
static const uint16_t s_patterns[] = {
    0x034, 0x121, 0x061, 0x160, 0x031, 0x130, 0x070, 0x025, 0x124, 0x064,
    0x109, 0x049, 0x148, 0x019, 0x118, 0x058, 0x00d, 0x10c, 0x04c, 0x01c,
    0x103, 0x043, 0x142, 0x013, 0x112, 0x052, 0x007, 0x106, 0x046, 0x016,
    0x181, 0x0c1, 0x1c0, 0x091, 0x190, 0x0d0, 0x085, 0x184, 0x0c4, 0x0a8,
    0x0a2, 0x08a, 0x02a
};

char s3_barcode_character(uint8_t code)
{
    unsigned index = code & S3_BC_SYMBOL_MASK;
    if ((code & S3_BC_VALID) == 0U || index >= sizeof(s_patterns)/sizeof(s_patterns[0]))
        return '\0';
    return s_alphabet[index];
}

static unsigned reverse_nine(unsigned pattern)
{
    unsigned i, result = 0;
    for (i = 0; i < 9U; ++i) {
        result = (result << 1) | (pattern & 1U);
        pattern >>= 1;
    }
    return result;
}

/* A character has exactly six narrow and three wide elements. Validate
 * both clusters instead of blindly selecting the three largest values. */
static int classify(const uint32_t *widths, unsigned *pattern, uint32_t *narrow)
{
    uint32_t sorted[9], n = 0, w = 0;
    unsigned i, j;
    *pattern = 0;
    for (i = 0; i < 9U; ++i) {
        if (widths[i] == 0U || widths[i] > S3_BC_MAX_RUN_US) return 0;
        sorted[i] = widths[i];
        for (j = i; j > 0U && sorted[j] < sorted[j - 1U]; --j) {
            uint32_t temp = sorted[j];
            sorted[j] = sorted[j - 1U];
            sorted[j - 1U] = temp;
        }
    }
    for (i = 0; i < 6U; ++i) n += sorted[i];
    for (i = 6; i < 9U; ++i) w += sorted[i];
    n /= 6U;
    w /= 3U;
    if (n < S3_BC_MIN_NARROW_US || n > S3_BC_MAX_NARROW_US ||
        w * 10U < n * 18U || w * 10U > n * 34U) return 0;
    for (i = 0; i < 9U; ++i) {
        uint32_t mean = i < 6U ? n : w;
        if (sorted[i] * 4U < mean * 3U || sorted[i] * 4U > mean * 5U) return 0;
    }
    /* A clear split is necessary even at the tolerance boundaries. */
    if (sorted[6] * 10U < sorted[5] * 14U) return 0;
    for (i = 0; i < 9U; ++i)
        *pattern = (*pattern << 1) | (widths[i] >= sorted[6] ? 1U : 0U);
    *narrow = n;
    return 1;
}

/* Complete-frame validation: check widths, gaps, white margins, start/stop
 * guards and the data character before returning a packed result. */
int s3_barcode_decode(const uint32_t widths[S3_BC_RUNS],
                      uint32_t leading_white_us, uint32_t trailing_white_us,
                      uint8_t *code)
{
    unsigned patterns[3], i, reversed;
    uint32_t narrow[3], smallest, largest;
    if (code == NULL) return S3_BC_INVALID;
    *code = 0;
    if (widths == NULL) return S3_BC_INVALID;
    for (i = 0; i < 3U; ++i)
        if (!classify(widths + i * 10U, &patterns[i], &narrow[i])) return S3_BC_INVALID;
    smallest = largest = narrow[0];
    for (i = 1; i < 3U; ++i) {
        if (narrow[i] < smallest) smallest = narrow[i];
        if (narrow[i] > largest) largest = narrow[i];
    }
    /* Reject severe acceleration/distortion; speed may vary between scans. */
    if (largest * 10U > smallest * 14U) return S3_BC_INVALID;
    for (i = 0; i < 2U; ++i) {
        uint32_t gap = widths[9U + i * 10U];
        if (gap * 2U < narrow[i] || gap > narrow[i] * 18U / 10U)
            return S3_BC_INVALID;
    }
    if (leading_white_us < narrow[0] * 10U || trailing_white_us < narrow[2] * 10U)
        return S3_BC_INVALID;
    if (patterns[0] == BC_STAR && patterns[2] == BC_STAR) reversed = 0;
    else if (reverse_nine(patterns[0]) == BC_STAR &&
             reverse_nine(patterns[2]) == BC_STAR) reversed = S3_BC_REVERSED;
    else return S3_BC_INVALID;
    if (reversed) patterns[1] = reverse_nine(patterns[1]);
    for (i = 0; i < sizeof(s_patterns)/sizeof(s_patterns[0]); ++i) {
        if (patterns[1] == s_patterns[i]) {
            *code = (uint8_t)(S3_BC_VALID | reversed | i);
            return S3_BC_OK;
        }
    }
    return S3_BC_INVALID;
}

/* Streaming decoder: reset, poll and edge() collect a frame over successive
 * calls, enforce timeouts, and wait for trailing white before reporting. */
void s3_barcode_reset(S3_BARCODE_DECODER *decoder, int black, uint32_t now_us)
{
    if (decoder == NULL) return;
    decoder->count = 0;
    decoder->black = black != 0;
    decoder->capturing = 0;
    decoder->pending = 0;
    decoder->pending_code = 0;
    decoder->last_edge_us = now_us;
    decoder->frame_start_us = now_us;
    decoder->leading_white_us = 0;
    decoder->quiet_needed_us = 0;
}

int s3_barcode_poll(S3_BARCODE_DECODER *decoder, uint32_t now_us, uint8_t *code)
{
    uint32_t elapsed;
    if (decoder == NULL || code == NULL) return S3_BC_INVALID;
    *code = 0;
    elapsed = now_us - decoder->last_edge_us;
    if (decoder->pending && elapsed >= decoder->quiet_needed_us) {
        *code = decoder->pending_code;
        decoder->pending = 0;
        return S3_BC_OK;
    }
    if (decoder->capturing && (elapsed > S3_BC_MAX_RUN_US ||
        now_us - decoder->frame_start_us > S3_BC_FRAME_TIMEOUT_US)) {
        decoder->capturing = 0;
        decoder->count = 0;
        return S3_BC_TIMEOUT;
    }
    /* Cap a long idle white interval, so a 71-minute timer wrap cannot
     * make an already established quiet zone appear too short. */
    if (!decoder->capturing && !decoder->pending && !decoder->black &&
        elapsed > S3_BC_QUIET_CAP_US)
        decoder->last_edge_us = now_us - S3_BC_QUIET_CAP_US;
    return S3_BC_NONE;
}

int s3_barcode_edge(S3_BARCODE_DECODER *decoder, int black, uint32_t now_us,
                    uint8_t *code)
{
    uint32_t duration;
    int result;
    if (decoder == NULL || code == NULL) return S3_BC_INVALID;
    *code = 0;
    if ((black != 0) == decoder->black) return S3_BC_NONE;
    result = s3_barcode_poll(decoder, now_us, code);
    duration = now_us - decoder->last_edge_us;
    if (decoder->pending) {
        /* More ink before the required trailing white: not this frame. */
        decoder->pending = 0;
        result = S3_BC_INVALID;
    } else if (decoder->capturing) {
        decoder->widths[decoder->count++] = duration;
        if (decoder->count == S3_BC_RUNS) {
            unsigned pattern;
            uint32_t narrow;
            decoder->capturing = 0;
            result = s3_barcode_decode(decoder->widths, decoder->leading_white_us,
                                       UINT32_MAX, &decoder->pending_code);
            if (result == S3_BC_OK && classify(decoder->widths + 20U, &pattern, &narrow)) {
                decoder->quiet_needed_us = narrow * 10U;
                decoder->pending = 1;
                result = S3_BC_NONE; /* wait for the real trailing white */
            }
        }
    } else if (black && duration >= S3_BC_MIN_NARROW_US * 10U) {
        decoder->count = 0;
        decoder->capturing = 1;
        decoder->frame_start_us = now_us;
        decoder->leading_white_us = duration;
    }
    decoder->black = black != 0;
    decoder->last_edge_us = now_us;
    return result;
}
