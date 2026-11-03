/* S3: centre/right line inputs; repurposed LEFT barcode input. */
#ifndef SUBSYSTEM3_H
#define SUBSYSTEM3_H

#include "../app.h"
#include "barcode_decoder.h"

/* Digital DO pins already claimed in TEAM.md. */
#define S3_PRI_IR               17
#define S3_BARCODE_PIN          26
#define S3_IR_CENTRE_PIN        6
#define S3_IR_RIGHT_PIN         16
#define S3_IR_PERIOD_MS         20
#define S3_IR_LOG_ENABLED       1       /* set to 0 to mute IR console output */
#define S3_IR_BLACK_LEVEL       1       /* confirmed by stationary testing */
#define S3_BARCODE_BLACK_LEVEL  1       /* verify after final mounting */

/* C/R bits = 2/1. Bit 2 is always zero; barcode never affects steering.
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
