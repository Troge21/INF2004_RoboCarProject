/* Subsystem 3: digital IR sensing. */
#ifndef SUBSYSTEM3_H
#define SUBSYSTEM3_H

#include "../app.h"

/* Digital DO pins already claimed in TEAM.md. */
#define S3_PRI_IR               17
#define S3_IR_LEFT_PIN          26
#define S3_IR_CENTRE_PIN        6
#define S3_IR_RIGHT_PIN         16
#define S3_IR_PERIOD_MS         20
#define S3_IR_LOG_ENABLED       1       /* set to 0 to mute IR console output */
#define S3_IR_BLACK_LEVEL       1       /* confirmed by stationary testing */

/* L/C/R bits = 4/2/1. Timestamp is kernel milliseconds (low word). */
typedef struct {
    UINT pattern;
    UW timestamp_ms;
    BOOL valid;
} S3_LINE_DATA;
EXPORT ER subsystem3_get_line(S3_LINE_DATA *sample);

#endif /* SUBSYSTEM3_H */
