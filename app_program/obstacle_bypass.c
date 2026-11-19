#include "obstacle_bypass.h"
#include "controller.h"

#ifdef APP_OBSTACLE_BYPASS
#define BYPASS_EFFECTIVE_TRACK_MM 120U
#define BYPASS_SHIFT_MM 280
#define BYPASS_PASS_MM 550
#define BYPASS_RANGE_MAX_AGE_MS 500U
#define BYPASS_SENSOR_WAIT_MS 500U
#define BYPASS_START_LIMIT_MS 2500U
#define BYPASS_LEG_LIMIT_MS 25000U

enum { WAIT_BACKGROUND, WAIT_LINE, LINE_ONCE, LINE_CONFIRMED };

/* The known-obstacle course uses equal outward and return offsets. */
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
    return range && range->valid &&
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
    const BOOL have_range = range_available(input);
    if (input->motion->faults || elapsed >= BYPASS_LEG_LIMIT_MS ||
        (initial && input->nav && !matching_turn) ||
        (!initial && !awaiting_start && !reached && have_range &&
         input->range->distance_mm <= S5_OBSTACLE_MM))
        return fail_bypass(bypass);

    const BOOL clearance = have_range && input->range->distance_mm > S5_OBSTACLE_MM &&
        input->nav && input->nav->phase == S5_NAV_CLEAR;
    const BOOL ready = input->ready && (initial ? matching_turn : (reached || clearance));
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
    if (phase == BYPASS_SHIFT_IN && !awaiting_start)
        observe_return_line(bypass, input->line);

    if (reached) {
        if (!input->settled) return APP_CTRL_STOPPED;
        if (initial && subsystem5_end_turn(bypass->scan_id) != E_OK) return APP_CTRL_STOPPED;
        if (phase == BYPASS_SHIFT_IN && bypass->line_state != LINE_CONFIRMED)
            return fail_bypass(bypass);
        set_phase(bypass, phase == BYPASS_ALIGN ? BYPASS_IDLE : (BYPASS_PHASE)(phase + 1),
                  input->now_ms);
        return APP_CTRL_STOPPED;
    }

    if (awaiting_start) {
        /* Before a new leg, require a front sample taken after the previous
         * leg settled, especially when the car has just changed heading. */
        if (!input->settled ||
            (!initial && (UW)(input->now_ms - input->range->timestamp_ms) >= elapsed))
            return APP_CTRL_STOPPED;
        ER er = route[phase].turn_deg ?
            s2_turn_begin(&bypass->move, route[phase].turn_deg * bypass->side,
                          BYPASS_EFFECTIVE_TRACK_MM, input->motion, input->now_ms) :
            s2_linear_begin(&bypass->move, route[phase].distance_mm, input->motion, input->now_ms);
        if (er == E_BUSY || er == E_TMOUT) return APP_CTRL_STOPPED;
        if (er != E_OK) return fail_bypass(bypass);
        bypass->line_timestamp_ms = input->line->timestamp_ms;
    }

    S2_MOVE_RESULT result = s2_encoder_move_step(&bypass->move, input->motion, input->now_ms,
                                                TRUE, targets);
    if (result == S2_MOVE_REACHED) return APP_CTRL_STOPPED;
    if (result != S2_MOVE_RUNNING) return fail_bypass(bypass);
    if (initial) return bypass->side > 0 ? APP_CTRL_TURNING_LEFT : APP_CTRL_TURNING_RIGHT;
    return phase >= BYPASS_TURN_IN ? APP_CTRL_RECOVERING : APP_CTRL_AVOIDING;
}
#endif
