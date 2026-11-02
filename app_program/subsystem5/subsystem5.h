/* Subsystem 5: fixed-position HC-SR04 bench measurement (servo not started). */
#ifndef SUBSYSTEM5_H
#define SUBSYSTEM5_H

#include "../app.h"

#define S5_PRI_MAIN 20
#define S5_TRIG_PIN 4
#define S5_ECHO_PIN 5
#define S5_ULTRA_LOG_ENABLED 0 /* set to 1 to restore ultrasonic output */

/* Internal conversion shared with subsystem5_test.c. Valid range 20–4000 mm.
 * Returns E_PAR for invalid widths and clears *mm on failure. */
IMPORT ER s5_echo_to_mm(UW pulse_us, UW *mm);

#endif /* SUBSYSTEM5_H */
