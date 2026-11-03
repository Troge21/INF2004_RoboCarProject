/* S3: LEFT barcode input plus ONE optional, separately wired line input. */
#ifndef SUBSYSTEM3_H
#define SUBSYSTEM3_H

#include "../app.h"
#include "barcode_decoder.h"

/* Digital DO pins already claimed in TEAM.md. */
#define S3_PRI_IR               17
#define S3_BARCODE_PIN          26
/* Two sensors total. GP26 is barcode; the remaining line pin is unknown.
 * Set to 6 or 16 ONLY after the actual wiring is confirmed. -1 means no
 * line GPIO is configured/read and line snapshots remain invalid.
 * The guard permits build-time overrides for software validation. */
#ifndef S3_LINE_PIN
#define S3_LINE_PIN             (-1)
#endif
#if S3_LINE_PIN != -1 && S3_LINE_PIN != 6 && S3_LINE_PIN != 16
#error "S3_LINE_PIN must be -1 (unconfirmed), 6 or 16"
#endif
#if APP_LINE_FOLLOW_ENABLED
#error "Legacy three-sensor steering cannot consume the single-line-sensor format"
#endif
#define S3_IR_PERIOD_MS         20
#define S3_IR_LOG_ENABLED       1       /* set to 0 to mute IR console output */
#define S3_IR_BLACK_LEVEL       1       /* confirmed by stationary testing */
#define S3_BARCODE_BLACK_LEVEL  1       /* verify after final mounting */

/* A single sensor status: pattern=1 black, pattern=0 white, ONLY if valid.
 * An unconfigured sensor has valid=FALSE; get_line returns E_TMOUT.
 * This is not a lateral-position estimate or the old three-sensor pattern.
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
