/* Exclusive S2 bench: TEST=1, USB CDC, CRASH_REPORT=0.
 * POWERED_TEST=0: hand-turn counts. POWERED_TEST=1: timed motor trial.
 * PID_TEST=1 requests RPM; PID_TEST=0 keeps the calibrated 18/20 duty trial.
 * The bench uses normal S2 feedback/control; no second encoder ISR. */
#include "subsystem2.h"
#include "encoder_capture.h"
#include "encoder_move.h"
#include "motion_control.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

#if APP_TEST && S2_ENCODER_CALIBRATION
static const char * volatile bench_stage = "STARTING";
static const char * volatile bench_result = "not started";
static volatile ER bench_error;
static volatile BOOL bench_running;

#if defined(S2_BENCH_DISTANCE_MM) || defined(S2_BENCH_TURN_DEG)
#define S2_BENCH_COUNTDOWN_S 10U
#if defined(S2_BENCH_TURN_DEG)
#define S2_BENCH_TAG "[S2-TURN]"
_Static_assert(S2_BENCH_TRACK_MM > 0 && S2_BENCH_TRACK_MM <= S2_TURN_MAX_TRACK_MM,
               "Set a valid driven-track spacing in millimetres");
_Static_assert(S2_BENCH_TURN_DEG != 0 && S2_BENCH_TURN_DEG >= -S2_TURN_MAX_DEG &&
               S2_BENCH_TURN_DEG <= S2_TURN_MAX_DEG, "Set a valid signed angle");

static INT estimate_turn_x10(int64_t left, int64_t right)
{
    return (INT)((right - left) * 180LL * 113LL / (355LL * S2_BENCH_TRACK_MM));
}
#else
#define S2_BENCH_TAG "[S2-DIST]"
#endif

static void __attribute__((noreturn)) finish_move(
    const char *reason, const S2_ENCODER_MOVE *move, const S2_MOTION_DATA *cutoff, INT peak_trim)
{
    const ER stop = subsystem2_set_rpm_x10(0, 0);
    S2_MOTION_DATA final;
    SYSTIM now;
    (void)tk_dly_tsk(600);
    ER er = subsystem2_get_motion(&final);
    (void)tk_get_tim(&now);
    BOOL settled = er == E_OK && final.encoder_valid && final.speed_valid &&
        (UW)(now.lo - final.timestamp_ms) <= S2_FEEDBACK_MAX_AGE_MS &&
        (UW)(now.lo - final.speed_timestamp_ms) <= S2_FEEDBACK_MAX_AGE_MS &&
        !final.duty_left_x100 && !final.duty_right_x100 &&
        !final.actual_left_rpm_x10 && !final.actual_right_rpm_x10;

    for (;;) {
        if (move && stop == E_OK && settled && !final.faults) {
            int64_t left = final.distance_left_mm_x10 - move->origin_left_mm_x10;
            int64_t right = final.distance_right_mm_x10 - move->origin_right_mm_x10;
#if defined(S2_BENCH_TURN_DEG)
            INT cutoff_angle = estimate_turn_x10(cutoff->distance_left_mm_x10 - move->origin_left_mm_x10,
                cutoff->distance_right_mm_x10 - move->origin_right_mm_x10);
            tm_printf((UB *)S2_BENCH_TAG " DONE target_deg=%d track_mm=%u cutoff_deg_x10=%d settled_deg_x10=%d left_mm=%d right_mm=%d peak_trim_rpm_x10=%d PWM=0\n",
                S2_BENCH_TURN_DEG, (UINT)S2_BENCH_TRACK_MM, cutoff_angle,
                estimate_turn_x10(left, right), (INT)(left / 10), (INT)(right / 10), peak_trim);
#else
            tm_printf((UB *)S2_BENCH_TAG " DONE target_mm=%d cutoff_mm=%d settled_mm=%d left_mm=%d right_mm=%d peak_trim_rpm_x10=%d PWM=0\n",
                S2_BENCH_DISTANCE_MM, (INT)((cutoff->distance_centre_mm_x10 - move->origin_mm_x10) / 10),
                (INT)((final.distance_centre_mm_x10 - move->origin_mm_x10) / 10),
                (INT)(left / 10), (INT)(right / 10), peak_trim);
#endif
        } else {
            tm_printf((UB *)S2_BENCH_TAG " ABORT %s stop=%d settled=%u faults=0x%x\n",
                reason, stop, (UINT)settled, er == E_OK ? (UINT)final.faults : 0U);
        }
        (void)tk_dly_tsk(3000);
    }
}

static void __attribute__((noreturn)) move_trial(void)
{
    S2_ENCODER_MOVE move = {0};
    S2_MOTION_DATA motion;
    SYSTIM now;
    INT peak_trim = 0;
    bench_running = TRUE;
    ER er = subsystem2_init();
    if (er == E_OK) er = subsystem2_start();
    if (er != E_OK) finish_move("motor setup", NULL, NULL, 0);

    for (UINT remaining = S2_BENCH_COUNTDOWN_S; remaining > 0U; --remaining) {
#if defined(S2_BENCH_TURN_DEG)
        tm_printf((UB *)S2_BENCH_TAG " One %d deg in-place turn starts in %u s\n",
                  S2_BENCH_TURN_DEG, remaining);
#else
        tm_printf((UB *)S2_BENCH_TAG " One %d mm move starts in %u s\n",
                  S2_BENCH_DISTANCE_MM, remaining);
#endif
        (void)tk_dly_tsk(1000);
    }
    er = subsystem2_get_motion(&motion);
    (void)tk_get_tim(&now);
    if (er == E_OK) {
#if defined(S2_BENCH_TURN_DEG)
        er = s2_turn_begin(&move, S2_BENCH_TURN_DEG, S2_BENCH_TRACK_MM, &motion, now.lo);
#else
        er = s2_linear_begin(&move, S2_BENCH_DISTANCE_MM, &motion, now.lo);
#endif
    }
    if (er != E_OK) finish_move("initial feedback", NULL, NULL, 0);

    for (;;) {
        S2_MOVE_TARGETS targets;
        er = subsystem2_get_motion(&motion);
        (void)tk_get_tim(&now);
        S2_MOVE_RESULT result = s2_encoder_move_step(&move,
            er == E_OK ? &motion : NULL, now.lo, TRUE, &targets);
        if (result == S2_MOVE_REACHED)
            finish_move("target reached; stop or settling check failed", &move, &motion, peak_trim);
        if (result != S2_MOVE_RUNNING) {
            const char *reason = result == S2_MOVE_TIMEOUT ? "travel timed out" :
                (result == S2_MOVE_FAULT ? "motor fault" : "invalid feedback");
            finish_move(reason, NULL, NULL, 0);
        }
        INT trim = move.trim_rpm_x10 < 0 ? -move.trim_rpm_x10 : move.trim_rpm_x10;
        if (trim > peak_trim) peak_trim = trim;
        if (subsystem2_set_signed_rpm_x10(targets.left_rpm_x10, targets.right_rpm_x10) != E_OK)
            finish_move("motor command rejected", NULL, NULL, 0);
        (void)tk_dly_tsk(S2_PERIOD_MS);
    }
}
#elif S2_ENCODER_POWERED_TEST
/* Retain the late samples too when the test duration is increased. */
static S2_MOTION_DATA records[S2_ENCODER_TEST_RUN_MS / 1000U + 1U], baseline;
static UINT record_count;
static BOOL have_baseline;

static void print_record(UINT number, const S2_MOTION_DATA *m)
{
    tm_printf((UB *)"[S2-PID] sample=%u target_rpm_x10 L=%d R=%d; actual_rpm_x10 L=%d R=%d; duty_x100 L=%d R=%d; speed_valid=%d faults=0x%x\n",
              number, m->target_left_rpm_x10, m->target_right_rpm_x10,
              m->actual_left_rpm_x10, m->actual_right_rpm_x10,
              m->duty_left_x100, m->duty_right_x100, m->speed_valid, m->faults);
}

static ER read_feedback(S2_MOTION_DATA *m)
{
    SYSTIM now;
    ER er = subsystem2_get_motion(m);
    if (er != E_OK) return er;
    (void)tk_get_tim(&now);
    if ((UW)((UW)now.lo - m->timestamp_ms) > S2_FEEDBACK_MAX_AGE_MS) return E_TMOUT;
    return E_OK;
}

static void __attribute__((noreturn)) finish_trial(const char *reason)
{
    S2_MOTION_DATA final;
    ER er = subsystem2_set_rpm_x10(0, 0);
    UINT i;
    if (er != E_OK) CHECK(er, "S2 bench stop request");
    (void)tk_dly_tsk(S2_COMMAND_MAX_AGE_MS + 2U * S2_PERIOD_MS);
    bench_running = FALSE;
    bench_stage = "STOPPED";
    bench_result = reason;
    for (i = 0; i < record_count; ++i) print_record(i + 1U, &records[i]);
    er = read_feedback(&final);
    if (er == E_OK) {
        print_record(0, &final); /* final applied duty should be zero */
        if (have_baseline) {
            tm_printf((UB *)"[S2-DIST] counts L=%d R=%d; distance_mm_x10 L=%d R=%d centre=%d\n",
                      (INT)(final.count_left - baseline.count_left),
                      (INT)(final.count_right - baseline.count_right),
                      (INT)(final.distance_left_mm_x10 - baseline.distance_left_mm_x10),
                      (INT)(final.distance_right_mm_x10 - baseline.distance_right_mm_x10),
                      (INT)(final.distance_centre_mm_x10 - baseline.distance_centre_mm_x10));
        }
    } else CHECK(er, "S2 final feedback");
    tm_printf((UB *)"[S2-PID] %s. Motor trial stopped; reset repeats countdown.\n", reason);
    for (;;) (void)tk_dly_tsk(1000);
}

static void powered_trial(void)
{
    SYSTIM now;
    UW start_ms, report_ms;
    UINT remaining, attempt;
    ER er;
    S2_MOTION_DATA motion;
    bench_stage = "MOTOR_INIT";
    er = subsystem2_init();
    if (er == E_OK) er = subsystem2_start();
    if (er != E_OK) { bench_error = er; finish_trial("ABORT: motor setup"); }
    for (attempt = 0; attempt < 25U; ++attempt) {
        er = read_feedback(&motion);
        if (er == E_OK) break;
        (void)tk_dly_tsk(20);
    }
    if (er != E_OK || motion.faults != 0U) {
        bench_error = er != E_OK ? er : E_IO;
        finish_trial("ABORT: encoder setup/feedback");
    }
    bench_stage = "COUNTDOWN";
#if S2_ENCODER_PID_TEST
    tm_printf((UB *)"[S2-PID] RAISE WHEELS: target=%d x10 RPM on each wheel; run=%u ms; PID tuning is provisional.\n",
              S2_ENCODER_TEST_RPM_X10, S2_ENCODER_TEST_RUN_MS);
#else
    tm_printf((UB *)"[S2-PID] OPEN-LOOP trial: duty L=%d R=%d; run=%u ms.\n",
              S2_ENCODER_TEST_LEFT_DUTY, S2_ENCODER_TEST_RIGHT_DUTY, S2_ENCODER_TEST_RUN_MS);
#endif
    for (remaining = S2_ENCODER_TEST_WAIT_MS; remaining > 0U;) {
        UINT delay = remaining > 1000U ? 1000U : remaining;
        tm_printf((UB *)"[S2-PID] COUNTDOWN %u ms; raise wheels.\n", remaining);
        (void)tk_dly_tsk(delay);
        remaining -= delay;
    }
    er = read_feedback(&baseline);
    if (er != E_OK || !baseline.speed_valid || baseline.faults != 0U) {
        bench_error = er != E_OK ? er : E_IO;
        finish_trial("ABORT: feedback not ready");
    }
    have_baseline = TRUE;
    (void)tk_get_tim(&now);
    start_ms = report_ms = (UW)now.lo;
    bench_stage = "RUNNING";
    bench_running = TRUE;
    for (;;) {
        (void)tk_get_tim(&now);
        if ((UW)((UW)now.lo - start_ms) >= S2_ENCODER_TEST_RUN_MS) break;
#if S2_ENCODER_PID_TEST
        er = subsystem2_set_rpm_x10(S2_ENCODER_TEST_RPM_X10, S2_ENCODER_TEST_RPM_X10);
#else
        er = subsystem2_set_duty(S2_ENCODER_TEST_LEFT_DUTY, S2_ENCODER_TEST_RIGHT_DUTY);
#endif
        if (er != E_OK) { bench_error = er; finish_trial("ABORT: command rejected"); }
        er = read_feedback(&motion);
        if (er == E_OK && motion.faults != 0U) {
            bench_error = E_IO;
            finish_trial("ABORT: motor fault (see faults bitmask)");
        }
        if (er == E_OK && (UW)((UW)now.lo - report_ms) >= 1000U) {
            if (record_count < sizeof(records) / sizeof(records[0])) records[record_count++] = motion;
            report_ms = (UW)now.lo;
        }
        (void)tk_dly_tsk(S2_PERIOD_MS);
    }
    finish_trial("DONE");
}
#endif

static void calibration_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
#if defined(S2_BENCH_DISTANCE_MM) || defined(S2_BENCH_TURN_DEG)
    move_trial();
#elif S2_ENCODER_POWERED_TEST
    powered_trial();
#else
    S2_ENCODER_COUNTS counts;
    ER er;
    UINT pin;
    for (pin = 8; pin <= 11; ++pin) {
        (void)gpio_set_val(pin, 0);
        (void)gpio_set_pin(pin, GPIO_MODE_OUT);
    }
    out_w(GPIO_OUT_SET, 0xfU << 8); /* coast for hand rotation */
    er = s2_encoder_start();
    if (er != E_OK) {
        bench_error = er; bench_stage = "FAILED";
        tk_exd_tsk(); return;
    }
    bench_stage = "HAND_TURN";
    tm_printf((UB *)"[CAL] motors OFF; forward now counts POSITIVE on both wheels.\n");
    for (;;) {
        if (s2_encoder_read(&counts) == E_OK)
            tm_printf((UB *)"[CAL] LEFT=%d RIGHT=%d (normalized x1 counts)\n",
                      s2_count_delta(counts.left, 0), s2_count_delta(counts.right, 0));
        (void)tk_dly_tsk(1000);
    }
#endif
}

EXPORT void subsystem2_test(void)
{
    ID tid;
#if defined(S2_BENCH_DISTANCE_MM) || defined(S2_BENCH_TURN_DEG)
    tm_printf((UB *)S2_BENCH_TAG " BOOT: isolated encoder-motion bench.\n");
#else
    tm_printf((UB *)"[S2-PID] BOOT: reusable motor feedback/PID bench.\n");
#endif
    tid = app_start_task((FP)calibration_task, S2_PRI_MOTOR + 1, TP_PRC1, "encoder_bench");
    if (tid < E_OK) { bench_error = tid; bench_stage = "FAILED"; }
}

EXPORT void subsystem2_test_status(void)
{
    if (!bench_running)
        tm_printf((UB *)"[S2-PID] STATUS: %s error=%d result=%s.\n", bench_stage, bench_error, bench_result);
}
#elif APP_TEST
EXPORT void subsystem2_test(void)
{
    tm_printf((UB *)"[TEST] S2 encoder bench disabled.\n");
}
#endif
