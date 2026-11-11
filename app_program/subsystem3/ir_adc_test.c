/* Standalone analog IR and centre/right steering test. Called from usermain().
 * AO wiring: left GP28/ADC2, centre GP27/ADC1, right GP26/ADC0.
 * The line sits between GP27/GP26: both WHITE when correctly aligned.
 * WHITE/WHITE=forward, BLACK/WHITE=left, WHITE/BLACK=right.
 * BLACK/BLACK also drives forward to cross junctions.
 * Starts ONLY the existing S2 motor task, after ADC setup succeeds.
 * GP28 is read for display only; barcode decoding and other subsystems stay off.
 */
#include "../app.h"
#include "../subsystem2/subsystem2.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

/* Set to 0 to return to stationary readings. APP_TEST also disables driving. */
#ifndef IR_TEST_STEERING_ENABLED
#define IR_TEST_STEERING_ENABLED 1
#endif
#define IR_TEST_SAMPLES 2U
#define IR_TEST_LOG_MS  500U
#define IR_TEST_LEFT_THRESHOLD   1390U
#define IR_TEST_CENTRE_THRESHOLD 950U
#define IR_TEST_RIGHT_THRESHOLD  1420U
#if S2_LINE_USE_RPM_CONTROL
#define IR_TARGET_LABEL "target_rpm_x10"
#else
#define IR_TARGET_LABEL "duty"
#endif

static ER read_raw(ID adc, INT channel, UW *raw)
{
    SZ received = 0;
    ER er = tk_srea_dev(adc, channel, raw, 1, &received);
    if (er != E_OK) return er;
    return received == 1 && *raw <= 4095U ? E_OK : E_IO;
}

static void __attribute__((noreturn)) setup_failed(const char *step, ER er)
{
    tm_printf((UB *)"[IR-ADC] %s failed: %d. Motors OFF.\n", step, er);
    for (;;) (void)tk_dly_tsk(1000);
}

#if IR_TEST_STEERING_ENABLED && !APP_TEST
static const char *steering_targets(BOOL centre_black, BOOL right_black,
                                    INT *left, INT *right)
{
#if S2_LINE_USE_RPM_CONTROL
    const INT forward = S2_LINE_FORWARD_RPM_X10, inner = S2_LINE_INNER_RPM_X10;
#else
    const INT forward = APP_FORWARD_DUTY, inner = APP_INNER_DUTY;
#endif
    *left = *right = forward;
    if (centre_black && right_black) return "FORWARD";
    *left = centre_black ? inner : forward;
    *right = right_black ? inner : forward;
    return centre_black ? "LEFT" : (right_black ? "RIGHT" : "FORWARD");
}
#endif

void __attribute__((noreturn)) ir_adc_test(void)
{
    const UINT pins[] = {28, 27, 26};
    const INT channels[] = {2, 1, 0};
    const char *names[] = {"L", "C", "R"};
    ID adc;
    UW last_report_ms = 0;
    UINT i, pin;

    /* Hold outputs low until ADC setup and motor initialization succeed. */
    for (pin = 8; pin <= 11; ++pin) {
        (void)gpio_set_val(pin, 0);
        (void)gpio_set_pin(pin, GPIO_MODE_OUT);
    }
    for (i = 0; i < 3U; ++i) {
        out_w(GPIO_CTRL(pins[i]), GPIO_CTRL_FUNCSEL_NULL);
        clr_w(GPIO(pins[i]), GPIO_IE | GPIO_PUE | GPIO_PDE);
    }
    adc = tk_opn_dev((UB *)"adca", TD_READ);
    if (adc < E_OK) setup_failed("Open adca", adc);
    tm_printf((UB *)"[IR-ADC] AO: L=GP28 (display only), C=GP27, R=GP26. Raw range 0..4095.\n");
    tm_printf((UB *)"[IR-ADC] Thresholds L=%u C=%u R=%u; BLACK when value >= threshold.\n",
              IR_TEST_LEFT_THRESHOLD, IR_TEST_CENTRE_THRESHOLD, IR_TEST_RIGHT_THRESHOLD);
#if IR_TEST_STEERING_ENABLED && !APP_TEST
    {
        ER er = subsystem2_init();
        if (er != E_OK) setup_failed("Motor init", er);
        er = subsystem2_start();
        if (er != E_OK) setup_failed("Motor task", er);
    }
    tm_printf((UB *)"[STEER] Both WHITE=forward; both BLACK=forward; C-only BLACK=left; R-only BLACK=right.\n");
#if S2_LINE_USE_RPM_CONTROL
    tm_printf((UB *)"[STEER] Starts in %d ms; PID targets x10 RPM: forward=%d inner=%d.\n",
              APP_START_DELAY_MS, S2_LINE_FORWARD_RPM_X10, S2_LINE_INNER_RPM_X10);
#else
    tm_printf((UB *)"[STEER] Starts in %d ms; forward=%d%% inner=%d%%.\n",
              APP_START_DELAY_MS, APP_FORWARD_DUTY, APP_INNER_DUTY);
#endif
    (void)tk_dly_tsk(APP_START_DELAY_MS);
#else
    tm_printf((UB *)"[IR-ADC] Stationary test. Motors OFF.\n");
#endif
    for (;;) {
        UW values[3] = {0};
        SYSTIM started, now;
        BOOL valid = TRUE;
        ER read_error = E_OK;
        UINT failed_sensor = 0;
        INT left = 0, right = 0;
        const char *action = "MONITOR";
        (void)tk_get_tim(&started);
        for (i = 0; i < 3U; ++i) {
            UW raw = 0, sum = 0;
            UINT sample;
            /* Discard one conversion after selecting a different input. */
            ER er = read_raw(adc, channels[i], &raw);
            for (sample = 0; sample < IR_TEST_SAMPLES && er == E_OK; ++sample) {
                er = read_raw(adc, channels[i], &raw);
                if (er == E_OK) sum += raw;
            }
            if (er != E_OK) {
                read_error = er;
                failed_sensor = i;
                valid = FALSE;
                break;
            }
            values[i] = sum / IR_TEST_SAMPLES;
        }
        (void)tk_get_tim(&now);
        /* A delayed ADC transaction must not renew a moving motor command
         * using old samples. S2 also independently expires missing commands. */
        if ((UW)((UW)now.lo - (UW)started.lo) > S2_COMMAND_MAX_AGE_MS) valid = FALSE;
#if IR_TEST_STEERING_ENABLED && !APP_TEST
        if (valid)
            action = steering_targets(values[1] >= IR_TEST_CENTRE_THRESHOLD,
                                      values[2] >= IR_TEST_RIGHT_THRESHOLD,
                                      &left, &right);
        else action = "STOP";
        {
#if S2_LINE_USE_RPM_CONTROL
            ER er = subsystem2_set_rpm_x10(left, right);
#else
            ER er = subsystem2_set_duty(left, right);
#endif
            if (er != E_OK) {
                left = right = 0;
                (void)subsystem2_set_duty(0, 0);
                action = "COMMAND_ERROR";
            }
        }
#endif
        if ((UW)((UW)now.lo - last_report_ms) >= IR_TEST_LOG_MS) {
            if (read_error != E_OK)
                tm_printf((UB *)"[IR-ADC] %s read failed: %d. STOP.\n", names[failed_sensor], read_error);
            else if (!valid)
                tm_printf((UB *)"[IR-ADC] Sample took >%u ms. STOP.\n", S2_COMMAND_MAX_AGE_MS);
            else
                tm_printf((UB *)"[IR-ADC] L=%u %s C=%u %s R=%u %s; %s %s L=%d R=%d\n",
                          values[0], values[0] >= IR_TEST_LEFT_THRESHOLD ? "BLACK" : "WHITE",
                          values[1], values[1] >= IR_TEST_CENTRE_THRESHOLD ? "BLACK" : "WHITE",
                          values[2], values[2] >= IR_TEST_RIGHT_THRESHOLD ? "BLACK" : "WHITE",
                          action, IR_TARGET_LABEL, left, right);
#if IR_TEST_STEERING_ENABLED && !APP_TEST
            {
                S2_MOTION_DATA motion;
                if (subsystem2_get_motion(&motion) == E_OK)
                    tm_printf((UB *)"[MOTION] target_rpm_x10 L=%d R=%d; actual_rpm_x10 L=%d R=%d; duty_x100 L=%d R=%d; distance_mm=%d; speed_valid=%d faults=0x%x\n",
                              motion.target_left_rpm_x10, motion.target_right_rpm_x10,
                              motion.actual_left_rpm_x10, motion.actual_right_rpm_x10,
                              motion.duty_left_x100, motion.duty_right_x100,
                              (INT)(motion.distance_centre_mm_x10 / 10), motion.speed_valid,
                              motion.faults);
            }
#endif
            last_report_ms = (UW)now.lo;
        }
        /* Sensor/control updates are fast; console printing is rate-limited. */
        (void)tk_dly_tsk(APP_CONTROL_PERIOD_MS);
    }
}
