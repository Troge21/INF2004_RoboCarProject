/* Subsystem 3: digital IR sensing. */
#ifndef SUBSYSTEM3_H
#define SUBSYSTEM3_H

#include "../app.h"

/* Stationary IR test: digital DO pins already claimed in TEAM.md. */
#define S3_PRI_IR               17
#define S3_IR_LEFT_PIN          26
#define S3_IR_CENTRE_PIN        6
#define S3_IR_RIGHT_PIN         16
#define S3_IR_PERIOD_MS         100
#define S3_IR_LOG_ENABLED       0       /* set to 1 to restore IR console output */
#define S3_IR_BLACK_LEVEL       1       /* confirmed by stationary testing */

#endif /* SUBSYSTEM3_H */
