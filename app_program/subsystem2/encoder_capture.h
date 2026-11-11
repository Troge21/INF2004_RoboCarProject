#ifndef S2_ENCODER_CAPTURE_H
#define S2_ENCODER_CAPTURE_H
#include "subsystem2.h"
typedef struct { UW left, right; } S2_ENCODER_COUNTS;
/* Private driver interface: core 0 only. Values wrap modulo 2^32 and are
 * already normalized so vehicle-forward movement increases both counts. */
ER s2_encoder_start(void);
ER s2_encoder_read(S2_ENCODER_COUNTS *counts);
#endif
