/* Application lifecycle: create all subsystem objects, then start tasks.
 * S1: WiFi status and telemetry forwarding.
 * S2: motor PWM command consumer; optional encoder calibration in TEST=1.
 * S3: digital IR sensing.
 * S4: IMU/hump sensing.
 * IR steering/recovery integration runs in vehicle_controller_task below.
 * S5: fixed-position HC-SR04 measurements.
 * Former producer/consumer/monitor sample tasks and interfaces are retired.
 * Returning from usermain shuts down the kernel; leave it sleeping instead.
 */

#include "app.h"
#include "subsystem2/subsystem2.h"
#include "subsystem3/subsystem3.h"

#if APP_LINE_FOLLOW_ENABLED && !APP_TEST
/* Controller owns IR interpretation; S2 owns motor hardware. */
typedef struct {
    INT last_side; /* -1 left, +1 right, 0 unknown */
    BOOL searching;
    UW search_start_ms;
} APP_LINE_TRACKER;

static void app_line_speeds(UINT pattern, INT *left, INT *right)
{
    if (left == NULL || right == NULL) return;
    *left = 0;
    *right = 0;
    switch (pattern) {
    case 7: /* All three black */
    case 2: *left = APP_FORWARD_DUTY; *right = APP_FORWARD_DUTY; break;
    case 6: *left = APP_INNER_DUTY; *right = APP_FORWARD_DUTY; break;
    case 4: *right = APP_FORWARD_DUTY; break;
    case 3: *left = APP_FORWARD_DUTY; *right = APP_INNER_DUTY; break;
    case 1: *left = APP_FORWARD_DUTY; break;
    default: break; /* All-white or unsupported pattern: no direct command. */
    }
}

/* Caller owns a zero-initialized tracker. A timed-out search stays stopped
 * until a recognized line pattern returns; repeated white cannot restart it. */
static void app_track_line(APP_LINE_TRACKER *tracker, UINT pattern,
                           BOOL fresh, UW now_ms, INT *left, INT *right)
{
    if (tracker == NULL || left == NULL || right == NULL) return;
    *left = 0;
    *right = 0;
    if (!fresh || pattern > 7U || pattern == 5U) {
        tracker->last_side = 0;
        tracker->searching = FALSE;
        return;
    }
    if (pattern != 0U) {
        tracker->searching = FALSE;
        if (pattern == 4U || pattern == 6U) tracker->last_side = -1;
        if (pattern == 1U || pattern == 3U) tracker->last_side = 1;
        app_line_speeds(pattern, left, right);
        return;
    }
    if (tracker->last_side == 0) return;
    if (!tracker->searching) {
        tracker->searching = TRUE;
        tracker->search_start_ms = now_ms;
    }
    if ((UW)(now_ms - tracker->search_start_ms) >= APP_RECOVERY_TIMEOUT_MS) return;
    if (tracker->last_side < 0) *right = APP_RECOVERY_DUTY;
    else *left = APP_RECOVERY_DUTY;
}

static void vehicle_controller_task(INT stacd, void *exinf)
{
    APP_LINE_TRACKER tracker = {0};
    (void)stacd;
    (void)exinf;
    tm_printf((UB *)"[CTRL] Line follower starts in %d ms; duty=%d%%.\n",
              APP_START_DELAY_MS, APP_FORWARD_DUTY);
    (void)tk_dly_tsk(APP_START_DELAY_MS);
    for (;;) {
        S3_LINE_DATA line;
        SYSTIM now;
        INT left = 0, right = 0;
        ER er = subsystem3_get_line(&line);
        (void)tk_get_tim(&now);
        BOOL fresh = er == E_OK &&
            (UW)((UW)now.lo - line.timestamp_ms) <= APP_IR_MAX_AGE_MS;
        app_track_line(&tracker, fresh ? line.pattern : 0U,
                       fresh, (UW)now.lo, &left, &right);
        er = subsystem2_set_duty(left, right);
        if (er != E_OK && er != E_TMOUT) CHECK(er, "controller motor command");
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
	return tid;
}

EXPORT INT usermain(void)
{
	T_RVER	ver;

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

	/* Phase 2: every subsystem starts its tasks */
	CHECK(subsystem1_start(), "subsystem1_start");
	CHECK(subsystem2_start(), "subsystem2_start");
	CHECK(subsystem3_start(), "subsystem3_start");
	CHECK(subsystem4_start(), "subsystem4_start");
	CHECK(subsystem5_start(), "subsystem5_start");

#if APP_LINE_FOLLOW_ENABLED && !APP_TEST
    CHECK(app_start_task((FP)vehicle_controller_task, APP_PRI_CONTROLLER,
                         TP_PRC1, "vehicle_controller"), "start controller");
#endif

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
