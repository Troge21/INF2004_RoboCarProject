/* S3 owns the AO inputs. GP28 is barcode-reserved; GP27/26 track the line. */
#ifndef SUBSYSTEM3_H
#define SUBSYSTEM3_H

#include "../app.h"
#include "barcode_decoder.h"

/* Analog AO wiring/thresholds from the teammate's tested IR bench. */
#define S3_PRI_IR               17
#define S3_BARCODE_PIN          28
#define S3_IR_CENTRE_PIN        27
#define S3_IR_RIGHT_PIN         26
#define S3_BARCODE_ADC          2
#define S3_IR_CENTRE_ADC        1
#define S3_IR_RIGHT_ADC         0
#define S3_BARCODE_THRESHOLD    1390U
#define S3_IR_CENTRE_THRESHOLD  950U
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
} S3_LINE_DATA;
EXPORT ER subsystem3_get_line(S3_LINE_DATA *sample);
/* One consumer; nonblocking pop. E_TMOUT means no new barcode.
 * One-byte result format is documented in barcode_decoder.h. */
EXPORT ER subsystem3_get_barcode(UB *code);

#endif /* SUBSYSTEM3_H */
