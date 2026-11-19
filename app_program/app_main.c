/* Application lifecycle: create all subsystem objects, then start tasks.
 * S1: WiFi status and telemetry forwarding.
 * S2: motor PWM command consumer; optional encoder calibration in TEST=1.
 * S3: AO line sensing; GP28 reserved for future barcode decoding.
 * S4: IMU/hump sensing.
 * Analog steering is controller-owned; diagnostic benches are explicit modes.
 * S5: HC-SR04/servo snapshots; controller arbitrates obstacle and line requests.
 * Former producer/consumer/monitor sample tasks and interfaces are retired.
 * Returning from usermain shuts down the kernel; leave it sleeping instead.
 */

#include "app.h"
#include "controller.h"
#ifdef APP_OBSTACLE_BYPASS
#include "obstacle_bypass.h"
#endif
#include "subsystem1/subsystem1.h"
#include "subsystem2/encoder_move.h"
#include "subsystem2/subsystem2.h"
#include "subsystem3/subsystem3.h"
#include "subsystem5/subsystem5.h"

/* Standalone analog IR/steering test; other normal subsystem tasks stay off. */
extern void ir_adc_test(void) __attribute__((noreturn));

#if APP_LINE_FOLLOW_ENABLED && !APP_TEST
/* Preserve the teammate's AO policy. Both white is centered/forward;
 * both black crosses a junction. Pattern: centre/left=2, right=1. */
static void app_line_targets(UINT pattern, INT *left, INT *right)
{
#if S2_LINE_USE_RPM_CONTROL
    const INT forward = S2_LINE_FORWARD_RPM_X10, inner = S2_LINE_INNER_RPM_X10;
#else
    const INT forward = APP_FORWARD_DUTY, inner = APP_INNER_DUTY;
#endif
    *left = *right = 0;
    if (pattern > 3U) return;
    *left = pattern == 2U ? inner : forward;
    *right = pattern == 1U ? inner : forward;
}
static ER app_motor_request(INT left, INT right)
{
#if S2_LINE_USE_RPM_CONTROL
    return subsystem2_set_rpm_x10(left, right);
#else
    return subsystem2_set_duty(left, right);
#endif
}

static UW last_drive_ms;
#ifdef APP_OBSTACLE_BYPASS
static OBSTACLE_BYPASS bypass;
#else
#define APP_OBSTACLE_TURN_DEG 20
#define APP_EFFECTIVE_TRACK_MM 120U /* Calibrated with the S2 U-turn bench. */

static S2_ENCODER_MOVE avoidance_turn;
static UW avoidance_turn_id;
static BOOL avoidance_settling;

static void app_cancel_turn(void)
{
    if (avoidance_turn.result == S2_MOVE_IDLE || avoidance_turn.result == S2_MOVE_RUNNING)
        avoidance_turn.result = S2_MOVE_CANCELLED;
}
#endif

/* Sole integrated steering owner; S2 retains PWM/encoder/PID ownership. */
static void app_controller_step(UW started_ms)
{
    S3_LINE_DATA line = {0};
    S2_MOTION_DATA motion = {0};
    S5_NAV_DATA navigation = {0};
    SYSTIM now = {0}, operating = {0};
    INT left = 0, right = 0;
    unsigned state = APP_CTRL_STARTING;
    (void)app_controller_poll();
    ER line_er = subsystem3_get_line(&line);
    ER motion_er = subsystem2_get_motion(&motion);
    ER navigation_er = subsystem5_get_navigation(&navigation);
#ifdef APP_OBSTACLE_BYPASS
    S5_RANGE_DATA range = {0};
    ER range_er = bypass.phase != BYPASS_IDLE ? subsystem5_get_range(&range) : E_OBJ;
#endif
    (void)tk_get_tim(&now);
    (void)tk_get_otm(&operating);
    BOOL startup_done = (UW)(operating.lo - started_ms) >= APP_START_DELAY_MS;
    BOOL fresh = line_er == E_OK && line.valid && line.pattern <= 3U &&
        (UW)(now.lo - line.timestamp_ms) <= APP_IR_MAX_AGE_MS;
    BOOL motion_fresh = motion_er == E_OK &&
        (UW)(now.lo - motion.timestamp_ms) <= S2_FEEDBACK_MAX_AGE_MS;
    BOOL navigation_fresh = navigation_er == E_OK &&
        (UW)(now.lo - navigation.timestamp_ms) <= S5_NAV_MAX_AGE_MS;
    BOOL stopped = motion_fresh && motion.encoder_valid && motion.speed_valid && !motion.faults &&
        (UW)(now.lo - motion.speed_timestamp_ms) <= S2_FEEDBACK_MAX_AGE_MS &&
        motion.mode == S2_MODE_STOP && !motion.command_active &&
        motion.duty_left_x100 == 0 && motion.duty_right_x100 == 0 &&
        motion.actual_left_rpm_x10 == 0 && motion.actual_right_rpm_x10 == 0;
    if (motion_fresh && (motion.command_active || motion.duty_left_x100 || motion.duty_right_x100))
        last_drive_ms = now.lo;
    /* Wait for a full stopped speed window so neither wheel is held back
     * by S2's reversal guard when the encoder turn begins. */
    BOOL turn_settled = stopped &&
        (UW)(now.lo - motion.speed_timestamp_ms) + motion.speed_period_ms <= (UW)(now.lo - last_drive_ms);
    BOOL turn_requested = navigation_fresh && navigation.turn_id != 0U &&
        (navigation.phase == S5_NAV_LEFT || navigation.phase == S5_NAV_RIGHT);
    unsigned inhibit = app_controller_poll_ready(startup_done && fresh && stopped &&
        navigation_fresh && navigation.phase == S5_NAV_CLEAR);
    BOOL ready = fresh && motion_fresh && (!S2_LINE_USE_RPM_CONTROL || motion.speed_valid);
    BOOL permitted = startup_done && !inhibit && ready && !motion.faults;

#ifdef APP_OBSTACLE_BYPASS
    BOOL bypass_requested = turn_requested || bypass.phase != BYPASS_IDLE;
    if ((inhibit & APP_CTRL_LATCH) && bypass.phase != BYPASS_FAILED)
        bypass.phase = BYPASS_IDLE;
#else
    if (turn_requested && navigation.turn_id != avoidance_turn_id) {
        avoidance_turn_id = navigation.turn_id;
        avoidance_turn = (S2_ENCODER_MOVE){0};
    }
    if (avoidance_turn_id && (!turn_requested || !permitted)) app_cancel_turn();
    if (turn_settled) avoidance_settling = FALSE;
#endif

#ifdef APP_OBSTACLE_BYPASS
    if (bypass.phase == BYPASS_FAILED) state = APP_CTRL_FAULT;
    else if (startup_done && !(inhibit & APP_CTRL_LATCH) && bypass_requested) {
        const BYPASS_INPUT inputs = {
            .motion = &motion, .line = &line,
            .nav = navigation_fresh ? &navigation : NULL,
            .range = range_er == E_OK ? &range : NULL,
            .now_ms = now.lo, .ready = permitted, .settled = turn_settled,
        };
        S2_MOVE_TARGETS targets;
        state = app_bypass_step(&bypass, &inputs, &targets);
        left = targets.left_rpm_x10;
        right = targets.right_rpm_x10;
    } else
#endif
    if (inhibit & APP_CTRL_LATCH) state = APP_CTRL_STOP_LATCHED;
    else if (inhibit & APP_CTRL_POLL_BUSY) state = APP_CTRL_WAITING_LINE;
    else if (startup_done) {
        state = ready ? APP_CTRL_STOPPED : APP_CTRL_WAITING_LINE;
        if (permitted && navigation_fresh) {
#ifndef APP_OBSTACLE_BYPASS
            if (turn_requested) {
                S2_MOVE_TARGETS targets;
                if (avoidance_turn.result == S2_MOVE_IDLE && turn_settled) {
                    INT angle = navigation.phase == S5_NAV_LEFT ?
                        APP_OBSTACLE_TURN_DEG : -APP_OBSTACLE_TURN_DEG;
                    ER begin = s2_turn_begin(&avoidance_turn, angle, APP_EFFECTIVE_TRACK_MM, &motion, now.lo);
                    if (begin != E_OK && begin != E_BUSY && begin != E_TMOUT) app_cancel_turn();
                }
                S2_MOVE_RESULT result = s2_encoder_move_step(&avoidance_turn,
                    &motion, now.lo, TRUE, &targets);
                left = targets.left_rpm_x10;
                right = targets.right_rpm_x10;
                if (result == S2_MOVE_RUNNING)
                    state = navigation.phase == S5_NAV_LEFT ? APP_CTRL_TURNING_LEFT : APP_CTRL_TURNING_RIGHT;
            } else
#endif
            if (navigation.phase == S5_NAV_CLEAR
#ifndef APP_OBSTACLE_BYPASS
                && !avoidance_settling
#endif
            ) {
                app_line_targets(line.pattern, &left, &right);
                state = APP_CTRL_FOLLOWING;
            } else if (navigation.phase == S5_NAV_SCANNING) {
                state = APP_CTRL_SCANNING;
            }
        }
    }
    if (motion_fresh && motion.faults != 0U) {
        left = right = 0;
        state = APP_CTRL_FAULT;
    }
    /* A STOP arriving during source reads or Resume admission wins. */
    inhibit = app_controller_poll();
    if (inhibit) {
#ifdef APP_OBSTACLE_BYPASS
        if ((inhibit & APP_CTRL_LATCH) && bypass.phase != BYPASS_FAILED)
            bypass.phase = BYPASS_IDLE;
        if (bypass.phase == BYPASS_FAILED) state = APP_CTRL_FAULT;
#else
        if (avoidance_turn_id) app_cancel_turn();
#endif
        left = right = 0;
        if (state != APP_CTRL_FAULT)
            state = (inhibit & APP_CTRL_LATCH) ? APP_CTRL_STOP_LATCHED : APP_CTRL_WAITING_LINE;
    }
#ifdef APP_OBSTACLE_BYPASS
    BOOL encoder_move = bypass_requested && (left != 0 || right != 0);
#else
    BOOL encoder_move = turn_requested && (left != 0 || right != 0);
#endif
    ER er = encoder_move ? subsystem2_set_signed_rpm_x10(left, right) : app_motor_request(left, right);
    if (er != E_OK) {
#ifdef APP_OBSTACLE_BYPASS
        if (bypass_requested) bypass.phase = BYPASS_FAILED;
#else
        if (avoidance_turn_id) app_cancel_turn();
#endif
        (void)app_motor_request(0, 0);
    } else {
        if (left != 0 || right != 0) last_drive_ms = now.lo;
#ifdef APP_OBSTACLE_BYPASS
        /* A latched STOP ends the scan request once the wheels settle, so
         * the sensor can update the front clearance needed for Resume. */
        if ((inhibit & APP_CTRL_LATCH) && turn_requested && turn_settled)
            (void)subsystem5_end_turn(navigation.turn_id);
#else
        if (encoder_move) avoidance_settling = TRUE;
        if (turn_requested && turn_settled && avoidance_turn.result >= S2_MOVE_REACHED)
            (void)subsystem5_end_turn(avoidance_turn_id);
#endif
    }
    app_controller_publish(state, er);
}
static void vehicle_controller_task(INT stacd, void *exinf)
{
    SYSTIM started = {0};
    (void)stacd; (void)exinf;
    (void)tk_get_otm(&started);
    tm_printf((UB *)"[CTRL] Integrated AO controller; startup=%d ms, RPM mode=%u.\n",
              APP_START_DELAY_MS, (UINT)S2_LINE_USE_RPM_CONTROL);
    for (;;) {
        app_controller_step(started.lo);
        (void)tk_dly_tsk(APP_CONTROL_PERIOD_MS);
    }
}
#else
/* Stationary integrated mode never calls S2. */
static void vehicle_controller_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    for (;;) {
        (void)app_controller_poll();
        app_controller_publish(APP_CTRL_DISABLED, E_OK);
        (void)tk_dly_tsk(APP_CONTROL_PERIOD_MS);
    }
}
#endif

/*
 * Create and start one task (declared in app.h for every subsystem).
 *   prc = TP_PRC1 (core 0), TP_PRC2 (core 1), or 0 for "any core".
 */
EXPORT ID app_start_task(FP entry, PRI pri, UW prc, const char *name)
{
	T_CTSK	ctsk = {0};
	ID	tid;
	ER	er;

	ctsk.task    = entry;		/* function the task runs */
	ctsk.itskpri = pri;		/* initial priority (1 = highest) */
	ctsk.stksz   = TASK_STACK_SIZE;	/* kernel allocates the stack */
	ctsk.tskatr  = TA_HLNG		/* written in a high-level language (C) */
		     | TA_RNG3;		/* runs at user protection level */
#if TK_SUPPORT_SMP
	if (prc != 0) {
		ctsk.tskatr |= TA_ASSPRC;	/* honour the affinity below */
		ctsk.assprc  = prc;		/* which core(s) it may run on */
	}
#else
	(void)prc;			/* single-core build: no affinity */
#endif

	/* tk_cre_tsk: create the task.  It starts in the DORMANT state. */
	tid = tk_cre_tsk(&ctsk);
	if (tid < E_OK) {
		CHECK(tid, name);
		return tid;
	}

	/* tk_sta_tsk: DORMANT -> READY.  The int is passed as stacd. */
	er = tk_sta_tsk(tid, 0);
	CHECK(er, name);
	if (er < E_OK) {
		(void)tk_del_tsk(tid);
		return er;
	}
#if APP_BENCH_MODE == APP_BENCH_INTEGRATED
    s1_resource_add_stack(TASK_STACK_SIZE);
#endif
	return tid;
}

EXPORT INT usermain(void)
{
	T_RVER	ver;

#if APP_BENCH_MODE == APP_BENCH_ENCODER
    /* Explicit exclusive encoder bench; powered mode starts only S2 motor PWM. */
    subsystem2_test();
    for (;;) {
        (void)tk_dly_tsk(2000);
        subsystem2_test_status();
    }
#elif APP_BENCH_MODE == APP_BENCH_IR
    ir_adc_test();
#endif

	/* tk_ref_ver: ask the kernel who it is */
	tk_ref_ver(&ver);
	tm_printf((UB*)"\n=== micro T-Kernel on Raspberry Pi Pico W"
		  " (spec 0x%04x, product 0x%04x) ===\n", ver.spver, ver.prver);
#if TK_SUPPORT_SMP
	tm_printf((UB*)"dual-core (SMP) build, usermain on core %d\n",
		  tk_get_prc() - 1);	/* tk_get_prc() counts from 1 */
#else
	tm_printf((UB*)"single-core build\n");
#endif

	/* Phase 1: every subsystem creates its kernel objects */
	CHECK(subsystem1_init(), "subsystem1_init");
	CHECK(subsystem2_init(), "subsystem2_init");
	CHECK(subsystem3_init(), "subsystem3_init");
	CHECK(subsystem4_init(), "subsystem4_init");
	CHECK(subsystem5_init(), "subsystem5_init");
    CHECK(app_controller_init(), "controller_init");

	/* Phase 2: every subsystem starts its tasks */
	CHECK(subsystem1_start(), "subsystem1_start");
	CHECK(subsystem2_start(), "subsystem2_start");
	CHECK(subsystem3_start(), "subsystem3_start");
	CHECK(subsystem4_start(), "subsystem4_start");
	CHECK(subsystem5_start(), "subsystem5_start");

    CHECK(app_start_task((FP)vehicle_controller_task, APP_PRI_CONTROLLER,
                         TP_PRC1, "vehicle_controller"), "start controller");

	tm_printf((UB*)"all subsystems started\n\n");

#if APP_TEST
	/* Test build (make TEST=1): run every subsystem's tests in turn,
	   here in the initial task, while the system is running.  Tests may
	   call tk_dly_tsk() to let the other tasks work for a while. */
	tm_printf((UB*)"[TEST] === test run start ===\n");
	subsystem1_test();
	subsystem2_test();
	subsystem3_test();
	subsystem4_test();
	subsystem5_test();
	tm_printf((UB*)"[TEST] === test run end ===\n");
#endif

	/* Sleep forever.  Returning from usermain() would shut down. */
	tk_slp_tsk(TMO_FEVR);
	return 0;
}
