/*
 * subsystem4_test.c
 * Subsystem 4: On-target Unit & Algorithm Tests (make TEST=1)
 *
 * Verifies pitch estimation mathematics, parameter boundary checks,
 * and NC3 run-maximum retention / reset semantics.
 *
 * Conforms to Barr-C Coding Standard and test_report.h conventions.
 */

#include "subsystem4.h"
#include "../test_report.h"

#if APP_TEST

EXPORT void subsystem4_test(void)
{
    GY511_ACCEL_DATA_T vec_flat   = { .x_mg = 0,    .y_mg = 0, .z_mg = 1000 };
    GY511_ACCEL_DATA_T vec_tilt15 = { .x_mg = 259,  .y_mg = 0, .z_mg = 966  };
    GY511_ACCEL_DATA_T vec_down15 = { .x_mg = -259, .y_mg = 0, .z_mg = 966  };
    GY511_ACCEL_DATA_T vec_tilt30 = { .x_mg = 500,  .y_mg = 0, .z_mg = 866  };
    GY511_ACCEL_DATA_T front_up = { .x_mg = 0, .y_mg = -259, .z_mg = 966 };
    GY511_ACCEL_DATA_T front_down = { .x_mg = 0, .y_mg = 259, .z_mg = 966 };
    HUMP_TELEMETRY_T telem;
    int16_t pitch;
    ER er;

    TEST_INIT();

    /* ---------------------------------------------------------------------
     * S4-U02: Pitch Trigonometric Calculation Verification
     * --------------------------------------------------------------------- */
    pitch = drv_gy511_calc_pitch_x10(&vec_flat);
    TEST_CHECK("S4-U02a", (pitch >= -2) && (pitch <= 2),
               "flat acceleration vector computes 0.0 +/- 0.2 deg");

    pitch = drv_gy511_calc_pitch_x10(&vec_tilt15);
    TEST_CHECK("S4-U02b", (pitch >= 145) && (pitch <= 155),
               "+15 deg incline vector computes +15.0 +/- 0.5 deg");

    pitch = drv_gy511_calc_pitch_x10(&vec_down15);
    TEST_CHECK("S4-U02c", (pitch >= -155) && (pitch <= -145),
               "-15 deg decline vector computes -15.0 +/- 0.5 deg");

    pitch = drv_gy511_calc_pitch_x10(&vec_tilt30);
    TEST_CHECK("S4-U02d", (pitch >= 295) && (pitch <= 315),
               "+30 deg steep vector computes +30.0 +/- 1.0 deg");

    /* ---------------------------------------------------------------------
     * S4-U05: Telemetry Snapshot Interface (IF-02)
     * --------------------------------------------------------------------- */
    er = subsystem4_get_telemetry(&telem);
    TEST_CHECK("S4-U05a", (er == E_OK),
               "subsystem4_get_telemetry returns E_OK and valid snapshot");

    /* ---------------------------------------------------------------------
     * S4-U06: Decision NC3 Run Reset (IF-08)
     * --------------------------------------------------------------------- */
    er = subsystem4_reset_run();
    (void)subsystem4_get_telemetry(&telem);
    TEST_CHECK("S4-U06a", (er == E_OK) && (telem.max_hump_mm == 0U),
               "subsystem4_reset_run clears run maximum to 0 mm");

    pitch = subsystem4_calc_vehicle_pitch_x10(&front_up);
    TEST_CHECK("S4-U07a", pitch >= 145 && pitch <= 155,
               "mounted sensor front-up gives positive 15 deg");
    pitch = subsystem4_calc_vehicle_pitch_x10(&front_down);
    TEST_CHECK("S4-U07b", pitch >= -155 && pitch <= -145,
               "mounted sensor front-down gives negative 15 deg");
    pitch = subsystem4_calc_vehicle_pitch_x10(&vec_flat);
    TEST_CHECK("S4-U07c", pitch >= -2 && pitch <= 2,
               "mounted sensor flat gives zero pitch");
    pitch = subsystem4_calc_vehicle_pitch_x10(&vec_tilt15);
    TEST_CHECK("S4-U07d", pitch >= -2 && pitch <= 2,
               "side tilt does not produce longitudinal pitch");

    TEST_SUMMARY("subsystem4");
}

#endif /* APP_TEST */
