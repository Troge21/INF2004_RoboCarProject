/* Subsystem 1 public interface. */
#ifndef SUBSYSTEM1_H
#define SUBSYSTEM1_H

#include "../app.h"

/* Set to 1 only for manual encoder calibration, then build with TEST=1.
 * Requires USB console and CRASH_REPORT=0 because encoders use GP0/GP1. */
#define S1_ENCODER_CALIBRATION 0

#endif /* SUBSYSTEM1_H */
