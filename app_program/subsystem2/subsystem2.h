/* Subsystem 2: motor command interface, PWM and optional encoder calibration. */
#ifndef SUBSYSTEM2_H
#define SUBSYSTEM2_H

#include "../app.h"

/* Set to 1 for manual encoder calibration, then build with TEST=1.
 * Requires USB console and CRASH_REPORT=0. Ultrasonic task is skipped
 * in this mode because the bench handlers both own GPIO IRQ13. */
#define S2_ENCODER_CALIBRATION 0

/* Motor task owns PWM and stops on missing/stale controller commands. */
#define S2_PRI_MOTOR           14
#define S2_PERIOD_MS           20
#define S2_COMMAND_MAX_AGE_MS  100U

/* Wheel power balance: 100 = unchanged, 90 = 90% of requested duty.
 * Reduce the faster wheel's scale. This is manual trim, not speed feedback. */
#define S2_LEFT_POWER_SCALE    100
#define S2_RIGHT_POWER_SCALE   100
#if S2_LEFT_POWER_SCALE < 0 || S2_LEFT_POWER_SCALE > 100 || \
    S2_RIGHT_POWER_SCALE < 0 || S2_RIGHT_POWER_SCALE > 100
#error "Motor power scales must be between 0 and 100"
#endif
#define S2_LEFT_REVERSED       0
#define S2_RIGHT_REVERSED      0

/* Forward-only duty percentages 0..100. Nonblocking; retry on E_TMOUT.
 * Commands expire after S2_COMMAND_MAX_AGE_MS; only the motor task writes PWM.
 * Returns E_OBJ before initialization or in APP_TEST builds. */
EXPORT ER subsystem2_set_duty(INT left, INT right);

#endif /* SUBSYSTEM2_H */
