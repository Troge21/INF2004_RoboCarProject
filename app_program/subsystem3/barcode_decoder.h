/* Hardware-independent decoder for one-character, standard Code 39 labels.
 * No checksum or Full ASCII extension. Input: timestamped black/white edges.
 */
#ifndef BARCODE_DECODER_H
#define BARCODE_DECODER_H

#include <stdint.h>

#define S3_BC_RUNS             29U
#define S3_BC_MIN_NARROW_US    2000U
#define S3_BC_MAX_NARROW_US    100000U
#define S3_BC_MAX_RUN_US       600000U
#define S3_BC_FRAME_TIMEOUT_US 8000000U
#define S3_BC_QUIET_CAP_US     1000000U
#define S3_BC_ANALOG_SAMPLE_US  2000U
#define S3_BC_VALID            0x80U
#define S3_BC_REVERSED         0x40U
#define S3_BC_SYMBOL_MASK      0x3fU
#define S3_BC_FIRST_LETTER_INDEX 10U /* Alphabet begins with digits 0–9. */

enum { S3_BC_NONE, S3_BC_OK, S3_BC_INVALID, S3_BC_TIMEOUT };

/* One-byte result: valid bit, reverse-scan bit, six-bit alphabet index.
 * An invalid byte is NEVER a navigation command. */
char s3_barcode_character(uint8_t code);
int s3_barcode_decode(const uint32_t widths[S3_BC_RUNS],
                      uint32_t leading_white_us, uint32_t trailing_white_us,
                      uint8_t *code);
/* Analog fallback: complete frame, optionally with one short false pulse.
 * Returns the trailing white interval still required before reporting. */
int s3_barcode_decode_analog(const uint32_t *widths, unsigned count,
                            uint32_t leading_white_us, uint32_t *quiet_us,
                            uint8_t *code);

typedef struct {
    uint32_t widths[S3_BC_RUNS];
    uint32_t last_edge_us;
    uint32_t frame_start_us;
    uint32_t leading_white_us;
    uint32_t quiet_needed_us; /* Capture restart bound or pending trailing quiet. */
    uint8_t count;
    uint8_t black;
    uint8_t capturing;
    uint8_t pending;
    uint8_t pending_code;
} S3_BARCODE_DECODER;

/* All calls for a decoder belong to ONE task. Timestamps are uint32 us;
 * unsigned subtraction permits a timer wrap during a normal scan.
 * Init/resync discards an in-progress label and requires fresh white space. */
void s3_barcode_reset(S3_BARCODE_DECODER *decoder, int black, uint32_t now_us);
int s3_barcode_edge(S3_BARCODE_DECODER *decoder, int black, uint32_t now_us,
                    uint8_t *code);
int s3_barcode_poll(S3_BARCODE_DECODER *decoder, uint32_t now_us, uint8_t *code);

#endif
