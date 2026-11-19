/* S5 owns HC-SR04/servo sensing. Only the shared controller requests wheels. */
#ifndef SUBSYSTEM5_H
#define SUBSYSTEM5_H

#include "../app.h"

#define S5_PRI_MAIN 20
#define S5_TRIG_PIN 4
#define S5_ECHO_PIN 5
#define S5_SERVO_PIN 12
#define S5_ULTRA_LOG_ENABLED 0 /* set to 1 to restore ultrasonic output */

/* Vehicle bearings for the installed servo mount, increasing toward the left. */
#define S5_SCAN_RIGHT_DEG        30U
#define S5_SCAN_CENTRE_DEG       90U
#define S5_SCAN_LEFT_DEG        150U
#define S5_SERVO_SETTLE_MS      300U
#define S5_OBSTACLE_MM          250U
#define S5_CLEARANCE_MARGIN_MM   30U
#define S5_NAV_MAX_AGE_MS        200U
typedef enum {
    S5_MOTION_STOP, S5_MOTION_FORWARD, S5_MOTION_LEFT, S5_MOTION_RIGHT
} S5_MOTION;
typedef enum {
    S5_NAV_UNAVAILABLE, S5_NAV_CLEAR, S5_NAV_SCANNING,
    S5_NAV_LEFT, S5_NAV_RIGHT, S5_NAV_BLOCKED
} S5_NAV_PHASE;
typedef struct { UW timestamp_ms, turn_id; UINT phase; } S5_NAV_DATA;
/* Nonconsuming publication; the controller checks age and its own STOP latch. */
EXPORT ER subsystem5_get_navigation(S5_NAV_DATA *out);
/* End the matching turn attempt after the controller observes stopped wheels. */
EXPORT ER subsystem5_end_turn(UW turn_id);
EXPORT S5_MOTION s5_choose_motion(BOOL centre_valid, UW centre_mm,
                                BOOL left_valid, UW left_mm,
                                BOOL right_valid, UW right_mm);
typedef struct { UW timestamp_ms,sequence,distance_mm; BOOL valid; } S5_RANGE_DATA;
/* Protected passive snapshot. E_TMOUT before first result or when busy.
 * A valid snapshot may report valid=FALSE (no usable echo), never clear space. */
EXPORT ER subsystem5_get_range(S5_RANGE_DATA *out);

/* Internal conversion shared with subsystem5_test.c. Valid range 20–4000 mm.
 * Returns E_PAR for invalid widths and clears *mm on failure. */
IMPORT ER s5_echo_to_mm(UW pulse_us, UW *mm);

#endif /* SUBSYSTEM5_H */
