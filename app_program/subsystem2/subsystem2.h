/* Subsystem 2: motor/encoder controller; runtime interfaces still pending. */
#ifndef SUBSYSTEM2_H
#define SUBSYSTEM2_H

#include "../app.h"

/* Set to 1 for manual encoder calibration, then build with TEST=1.
 * Requires USB console and CRASH_REPORT=0. Ultrasonic task is skipped
 * in this mode because the bench handlers both own GPIO IRQ13. */
#define S2_ENCODER_CALIBRATION 0

#endif /* SUBSYSTEM2_H */
