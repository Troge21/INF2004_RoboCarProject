/* Subsystem 5: servo-mounted HC-SR04 scanning and obstacle avoidance. */
#ifndef SUBSYSTEM5_H
#define SUBSYSTEM5_H

#include "../app.h"

#define S5_PRI_MAIN 20
#define S5_TRIG_PIN 4
#define S5_ECHO_PIN 5
#define S5_SERVO_PIN 12
#define S5_ULTRA_LOG_ENABLED 0 /* set to 1 to restore ultrasonic output */

/* Conservative SG90-style positions. Swap LEFT/RIGHT values if the horn is
 * mounted in the opposite direction. */
#define S5_SERVO_RIGHT_DEG       30U
#define S5_SERVO_CENTRE_DEG      90U
#define S5_SERVO_LEFT_DEG       150U
#define S5_SERVO_SETTLE_MS      300U
#define S5_OBSTACLE_MM          250U
#define S5_CLEARANCE_MARGIN_MM    30U
#define S5_FORWARD_RPM_X10       260
#define S5_TURN_OUTER_RPM_X10    260
#define S5_TURN_INNER_RPM_X10      0
#define S5_TURN_MS               500U

typedef enum {
    S5_MOTION_STOP,
    S5_MOTION_FORWARD,
    S5_MOTION_LEFT,
    S5_MOTION_RIGHT
} S5_MOTION;

/* Internal conversion shared with subsystem5_test.c. Valid range 20–4000 mm.
 * Returns E_PAR for invalid widths and clears *mm on failure. */
IMPORT ER s5_echo_to_mm(UW pulse_us, UW *mm);
IMPORT S5_MOTION s5_choose_motion(BOOL centre_valid, UW centre_mm,
                                  BOOL left_valid, UW left_mm,
                                  BOOL right_valid, UW right_mm);

#endif /* SUBSYSTEM5_H */
