/* S3 owns the AO inputs. GP28 is barcode-reserved; GP27/26 track the line. */
#ifndef SUBSYSTEM3_H
#define SUBSYSTEM3_H

#include "../app.h"
#include "barcode_decoder.h"

#ifndef APP_S3_BARCODE_DETECT
#define APP_S3_BARCODE_DETECT 0
#endif

/* Analog AO wiring/thresholds from the teammate's tested IR bench. */
#define S3_PRI_IR               17
#define S3_BARCODE_PIN          28
#define S3_IR_CENTRE_PIN        27
#define S3_IR_RIGHT_PIN         26
#define S3_BARCODE_ADC          2
#define S3_IR_CENTRE_ADC        1
#define S3_IR_RIGHT_ADC         0
/* Midpoint for GP28's A4 barcode test at normal ride height. */
#define S3_BARCODE_THRESHOLD    172U
/* Midpoint of the nearest GP27 bench samples (white 158, black 752). */
#define S3_IR_CENTRE_THRESHOLD  455U
#define S3_IR_RIGHT_THRESHOLD   1420U
#define S3_IR_SAMPLES           2U
#define S3_IR_PERIOD_MS         20
#define S3_IR_LOG_ENABLED       0
/* Legacy decoder/capture software remains for its owner; the digital GPIO
 * capture is not started for AO wiring. GP28 barcode decoding is unavailable. */
#define S3_BARCODE_BLACK_LEVEL  1

/* Centre/right bits = 2/1; left barcode input never affects this pattern.
 * Timestamp is kernel milliseconds (low word). */
typedef struct {
    UINT pattern;
    UW timestamp_ms;
    BOOL valid;
    uint16_t centre_adc, right_adc; /* Raw AO values from the same valid sample. */
} S3_LINE_DATA;
EXPORT ER subsystem3_get_line(S3_LINE_DATA *sample);
/* One consumer; nonblocking pop. E_TMOUT means no new barcode.
 * One-byte result format is documented in barcode_decoder.h. */
EXPORT ER subsystem3_get_barcode(UB *code);

#if APP_S3_BARCODE_DETECT
#define S3_BARCODE_SIGNAL_VALID 0x1000U
#define S3_BARCODE_SIGNAL_BLACK 0x2000U
#define S3_BARCODE_SIGNAL_RUNS (S3_BC_RUNS + 1U)
#define S3_BARCODE_SIGNAL_RUN_US 2000U
typedef struct {
    UW timestamp_ms;
    uint16_t sample_word; /* ADC:12, valid:1, black:1 */
    uint16_t edges, rejected, gaps;
    /* Most recent confirmed runs, including the white before a full label.
     * Lengths use 2 ms units, with 255 meaning 510 ms or more. */
    uint8_t runs[S3_BARCODE_SIGNAL_RUNS];
    uint8_t next_run, run_count;
} S3_BARCODE_SIGNAL;
ER s3_get_barcode_signal(S3_BARCODE_SIGNAL *out);
#endif

#endif /* SUBSYSTEM3_H */
