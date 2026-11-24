#include "obstacle_bypass.h"
#include "controller.h"

#ifdef APP_OBSTACLE_BYPASS
#define BYPASS_EFFECTIVE_TRACK_MM 120U
#define BYPASS_SHIFT_MM 280
#define BYPASS_PASS_MM 600
#define BYPASS_RANGE_MAX_AGE_MS 500U
#define BYPASS_SENSOR_WAIT_MS 1600U
#define BYPASS_START_LIMIT_MS 3500U
#define BYPASS_CLEAR_HOLD_MS 1400U
#define BYPASS_CLEAR_HOLD_MM_X10 900
#define BYPASS_CRAWL_RPM_X10 180
#define BYPASS_LEG_LIMIT_MS 25000U
#ifdef APP_BYPASS_LINE_RETURN
#define BYPASS_RETURN_SEARCH_MM 400U
#define BYPASS_RETURN_MIN_TAPE_MM_X10 1000
#define BYPASS_RETURN_AXLE_MM_X10 600
#define BYPASS_RETURN_RPM_X10 234
#endif

enum { WAIT_BACKGROUND, WAIT_LINE, LINE_ONCE, LINE_CONFIRMED };

/* Encoder leg targets for the course, with a bounded tape search on return. */
static const struct { int16_t turn_deg; uint16_t distance_mm; } route[] = {
    [BYPASS_TURN_OUT] = {90, 0},
    [BYPASS_SHIFT_OUT] = {0, BYPASS_SHIFT_MM},
    [BYPASS_TURN_ALONG] = {-90, 0},
    [BYPASS_PASS] = {0, BYPASS_PASS_MM},
    [BYPASS_TURN_IN] = {-90, 0},
    [BYPASS_SHIFT_IN] = {0, BYPASS_SHIFT_MM},
    [BYPASS_ALIGN] = {90, 0},
};

static void set_phase(OBSTACLE_BYPASS *bypass, BYPASS_PHASE phase, UW now_ms)
{
    bypass->phase = (uint8_t)phase;
    bypass->phase_started_ms = now_ms;
    bypass->move = (S2_ENCODER_MOVE){0};
    bypass->has_clearance = FALSE;
    bypass->return_clear_samples = 0;
}

static unsigned fail_bypass(OBSTACLE_BYPASS *bypass)
{
    bypass->phase = BYPASS_FAILED;
    return APP_CTRL_FAULT;
}

static BOOL opening_turn(const S5_NAV_DATA *nav)
{
    return nav && nav->turn_id && (nav->phase == S5_NAV_LEFT || nav->phase == S5_NAV_RIGHT);
}

static BOOL range_available(const BYPASS_INPUT *input)
{
    const S5_RANGE_DATA *range = input->range;
    return range && (range->valid || range->no_return) &&
        (UW)(input->now_ms - range->timestamp_ms) <= BYPASS_RANGE_MAX_AGE_MS;
}

static void observe_return_line(OBSTACLE_BYPASS *bypass, const S3_LINE_DATA *line)
{
    if (bypass->line_state == LINE_CONFIRMED || line->timestamp_ms == bypass->line_timestamp_ms)
        return;
    bypass->line_timestamp_ms = line->timestamp_ms;
    if (!line->pattern) bypass->line_state = WAIT_LINE;
    else if (bypass->line_state >= WAIT_LINE) ++bypass->line_state;
}

unsigned app_bypass_step(OBSTACLE_BYPASS *bypass, const BYPASS_INPUT *input,
                         S2_MOVE_TARGETS *targets)
{
    *targets = (S2_MOVE_TARGETS){0};
    if (bypass->phase == BYPASS_FAILED) return APP_CTRL_FAULT;
    if (bypass->phase == BYPASS_IDLE) {
        if (!input->ready || !opening_turn(input->nav) || input->nav->turn_id == bypass->scan_id)
            return APP_CTRL_STOPPED;
        bypass->scan_id = input->nav->turn_id;
        bypass->side = input->nav->phase == S5_NAV_LEFT ? 1 : -1;
        bypass->line_state = WAIT_BACKGROUND;
        bypass->line_timestamp_ms = input->line->timestamp_ms;
        set_phase(bypass, BYPASS_TURN_OUT, input->now_ms);
    }
    const BYPASS_PHASE phase = (BYPASS_PHASE)bypass->phase;
    const UW elapsed = input->now_ms - bypass->phase_started_ms;
    const BOOL initial = phase == BYPASS_TURN_OUT;
    const BOOL matching_turn = opening_turn(input->nav) && input->nav->turn_id == bypass->scan_id;
    const BOOL awaiting_start = bypass->move.result == S2_MOVE_IDLE;
    const BOOL reached = bypass->move.result == S2_MOVE_REACHED;
    const BOOL pivoting = route[phase].turn_deg != 0 && !awaiting_start && !reached;
    const BOOL have_range = range_available(input);
    const BOOL close = !initial && !awaiting_start && !reached && have_range &&
        input->range->valid && input->range->distance_mm <= S5_OBSTACLE_MM;
    if (input->motion->faults || elapsed >= BYPASS_LEG_LIMIT_MS ||
        (initial && input->nav && !matching_turn) ||
        (close && phase < BYPASS_TURN_IN))
        return fail_bypass(bypass);

    const BOOL clearance = have_range && (input->range->no_return || input->range->distance_mm > S5_OBSTACLE_MM) &&
        input->nav && input->nav->phase == S5_NAV_CLEAR;
    /* Stop immediately on a close echo during the return. Confirm it before
     * faulting, or require two clear samples before finishing the move. */
    if (close && !bypass->return_clear_samples) {
        bypass->checked_return_echo_ms = input->range->timestamp_ms;
        bypass->return_clear_samples = 1;
    }
    if (bypass->return_clear_samples) {
        if (input->range && input->range->timestamp_ms != bypass->checked_return_echo_ms) {
            bypass->checked_return_echo_ms = input->range->timestamp_ms;
            if (close) return fail_bypass(bypass);
            if (clearance && bypass->return_clear_samples == 2) bypass->return_clear_samples = 0;
            else bypass->return_clear_samples = clearance ? 2 : 1;
        }
        if (bypass->return_clear_samples) {
            if ((UW)(input->now_ms - bypass->last_ready_ms) >= BYPASS_SENSOR_WAIT_MS)
                return fail_bypass(bypass);
            return APP_CTRL_STOPPED;
        }
    }
    const BOOL straight = route[phase].turn_deg == 0;
    if (straight && clearance && (!bypass->has_clearance || bypass->clearance_ms != input->range->timestamp_ms)) {
        bypass->clearance_ms = input->range->timestamp_ms;
        bypass->clearance_position_mm_x10 = input->motion->distance_centre_mm_x10;
        bypass->has_clearance = TRUE;
    }
    /* Within an already-cleared straight leg, bridge short unusable echoes
     * at reduced speed. Neither a new heading nor a close return can use
     * this window, and distance and time independently bound the travel. */
    const BOOL crawl = straight && !awaiting_start && !reached && !clearance && bypass->has_clearance &&
        (UW)(input->now_ms - bypass->clearance_ms) < BYPASS_CLEAR_HOLD_MS &&
        input->motion->distance_centre_mm_x10 - bypass->clearance_position_mm_x10 < BYPASS_CLEAR_HOLD_MM_X10;
    /* A pivot starts with clearance, then stays in its measured wheel arcs
     * through open bearings. A real close echo still aborts above. Forward
     * legs always wait for fresh clearance after the heading settles. */
    const BOOL ready = input->ready && (initial ? matching_turn : (reached || pivoting || clearance || crawl));
    if (awaiting_start && elapsed >= BYPASS_START_LIMIT_MS) return fail_bypass(bypass);
    if (!ready) {
        if (!awaiting_start && (UW)(input->now_ms - bypass->last_ready_ms) >= BYPASS_SENSOR_WAIT_MS)
            return fail_bypass(bypass);
        /* Leave the command at zero during a short loss of sensor data.
         * The encoder move keeps its original distance and time limit. */
        return APP_CTRL_STOPPED;
    }
    bypass->last_ready_ms = input->now_ms;

    /* Tape can enter the beam just as the encoder target cuts the drive.
     * Count fresh samples through the existing stopped-speed confirmation. */
#ifndef APP_BYPASS_LINE_RETURN
    if (phase == BYPASS_SHIFT_IN && !awaiting_start)
        observe_return_line(bypass, input->line);
#endif

    if (reached) {
        if (!input->settled) return APP_CTRL_STOPPED;
        if (initial && subsystem5_end_turn(bypass->scan_id) != E_OK) return APP_CTRL_STOPPED;
#ifndef APP_BYPASS_ENCODER_ALIGN
        if (phase == BYPASS_SHIFT_IN && bypass->line_state != LINE_CONFIRMED)
            return fail_bypass(bypass);
#endif
        set_phase(bypass, phase == BYPASS_ALIGN ? BYPASS_IDLE : (BYPASS_PHASE)(phase + 1),
                  input->now_ms);
        return APP_CTRL_STOPPED;
    }

    const INT turn = route[phase].turn_deg;
    const S2_MOTION_DATA *motion = input->motion;
    S2_MOTION_DATA calibrated;
    if (!turn) {
        /* The straight-line calibration models extra travel per left-wheel
         * encoder unit. Use the same ratio for progress and the RPM request. */
        calibrated = *motion;
        calibrated.distance_left_mm_x10 = calibrated.distance_left_mm_x10 *
            input->straight_right_rpm_x10 / input->straight_left_rpm_x10;
        calibrated.distance_centre_mm_x10 =
            (calibrated.distance_left_mm_x10 + calibrated.distance_right_mm_x10) / 2;
        motion = &calibrated;
    }

    if (awaiting_start) {
        /* Before a new leg, require a front sample taken after the previous
         * leg settled, especially when the car has just changed heading. */
        if (!input->settled ||
            (!initial && (UW)(input->now_ms - input->range->timestamp_ms) >= elapsed))
            return APP_CTRL_STOPPED;
        INT distance_mm = route[phase].distance_mm;
#ifdef APP_BYPASS_LINE_RETURN
        if (phase == BYPASS_SHIFT_IN) distance_mm = BYPASS_RETURN_SEARCH_MM;
#endif
        ER er = turn ? s2_turn_begin(&bypass->move, turn * bypass->side,
                                    BYPASS_EFFECTIVE_TRACK_MM, motion, input->now_ms) :
                       s2_linear_begin(&bypass->move, distance_mm, motion, input->now_ms);
        if (er == E_BUSY || er == E_TMOUT) return APP_CTRL_STOPPED;
        if (er != E_OK) return fail_bypass(bypass);
        bypass->line_timestamp_ms = input->line->timestamp_ms;
    }

#ifdef APP_BYPASS_LINE_RETURN
    if (phase == BYPASS_SHIFT_IN && bypass->line_state != LINE_CONFIRMED) {
        int64_t progress = motion->distance_centre_mm_x10 - bypass->move.origin_mm_x10;
        if (progress <= BYPASS_RETURN_SEARCH_MM * 10 &&
            (!input->line->pattern || progress >= BYPASS_RETURN_MIN_TAPE_MM_X10))
            observe_return_line(bypass, input->line);
        if (bypass->line_state == LINE_CONFIRMED)
            bypass->move.target_mm_x10 = (uint16_t)(progress + BYPASS_RETURN_AXLE_MM_X10);
    }
#endif
    S2_MOVE_RESULT result = s2_encoder_move_step(&bypass->move, motion, input->now_ms,
                                                TRUE, targets);
    if (result == S2_MOVE_REACHED) return APP_CTRL_STOPPED;
    if (result != S2_MOVE_RUNNING) return fail_bypass(bypass);
    INT speed_limit = crawl ? BYPASS_CRAWL_RPM_X10 : 0;
#ifdef APP_BYPASS_LINE_RETURN
    if (!speed_limit && phase == BYPASS_SHIFT_IN) speed_limit = BYPASS_RETURN_RPM_X10;
#endif
    if (speed_limit) {
        const INT fastest = targets->left_rpm_x10 > targets->right_rpm_x10 ? targets->left_rpm_x10 : targets->right_rpm_x10;
        if (fastest > speed_limit) {
            targets->left_rpm_x10 = targets->left_rpm_x10 * speed_limit / fastest;
            targets->right_rpm_x10 = targets->right_rpm_x10 * speed_limit / fastest;
        }
    }
    if (!turn)
        targets->left_rpm_x10 = targets->left_rpm_x10 * input->straight_left_rpm_x10 /
            input->straight_right_rpm_x10;
    if (initial) return bypass->side > 0 ? APP_CTRL_TURNING_LEFT : APP_CTRL_TURNING_RIGHT;
    return phase >= BYPASS_TURN_IN ? APP_CTRL_RECOVERING : APP_CTRL_AVOIDING;
}
#endif
