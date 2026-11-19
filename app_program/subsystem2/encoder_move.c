#include "encoder_move.h"

#define FORWARD_MOTION_MAX_AGE_MS (3U * S2_PERIOD_MS)
#define FORWARD_CRUISE_RPM_X10 350
#define FORWARD_APPROACH_RPM_X10 240
#define FORWARD_APPROACH_MM_X10 800
#define TURN_CRUISE_RPM_X10 260
#define TURN_APPROACH_MM_X10 250U
#define TURN_PI_NUMERATOR 355U
#define TURN_ARC_DENOMINATOR (360U * 113U)
#define FORWARD_START_ALLOWANCE_MS 2500U
#define FORWARD_MS_PER_MM 30U
/* Ignore small encoder differences. Add 0.5 RPM of trim per additional
 * 2 mm of imbalance, up to 3 RPM on either wheel. */
#define FORWARD_TRIM_DEADBAND_MM_X10 20U
#define FORWARD_TRIM_STEP_DISTANCE_X10 20U
#define FORWARD_TRIM_STEP_RPM_X10 5U
#define FORWARD_TRIM_MAX_STEPS 6U
#define FORWARD_MAX_TRIM_X10 (FORWARD_TRIM_STEP_RPM_X10 * FORWARD_TRIM_MAX_STEPS)

_Static_assert(S2_LINEAR_MAX_MM * 10U <= UINT16_MAX, "Distance must fit the move record");
_Static_assert((S2_TURN_MAX_TRACK_MM * S2_TURN_MAX_DEG * TURN_PI_NUMERATOR * 10U +
               TURN_ARC_DENOMINATOR - 1U) / TURN_ARC_DENOMINATOR <= UINT16_MAX,
               "Turn arc must fit the move record");
_Static_assert(FORWARD_MAX_TRIM_X10 <= INT8_MAX, "Trim must fit the move record");
_Static_assert(FORWARD_CRUISE_RPM_X10 + FORWARD_MAX_TRIM_X10 <= S2_MAX_RPM_X10 &&
               FORWARD_APPROACH_RPM_X10 > FORWARD_MAX_TRIM_X10,
               "Trim must leave both wheel targets in the forward RPM range");

static BOOL feedback_fresh(const S2_MOTION_DATA *sample, UW now_ms)
{
    return sample && sample->encoder_valid && sample->speed_valid &&
           (UW)(now_ms - sample->timestamp_ms) <= FORWARD_MOTION_MAX_AGE_MS &&
           (UW)(now_ms - sample->speed_timestamp_ms) <= S2_FEEDBACK_MAX_AGE_MS;
}

/* A movement opposite the requested direction does not count as progress. */
static uint64_t directed_progress(int64_t position, int64_t origin, INT direction)
{
    if (direction < 0) return position <= origin ? (uint64_t)origin - (uint64_t)position : 0U;
    return position >= origin ? (uint64_t)position - (uint64_t)origin : 0U;
}

static int8_t distance_trim(uint64_t left, uint64_t right)
{
    uint64_t gap = left >= right ? left - right : right - left;
    if (gap <= FORWARD_TRIM_DEADBAND_MM_X10) return 0;

    uint64_t excess = gap - FORWARD_TRIM_DEADBAND_MM_X10;
    UINT steps = FORWARD_TRIM_MAX_STEPS;
    if (excess < FORWARD_TRIM_MAX_STEPS * FORWARD_TRIM_STEP_DISTANCE_X10)
        steps = (UINT)excess / FORWARD_TRIM_STEP_DISTANCE_X10;
    INT trim = (INT)(steps * FORWARD_TRIM_STEP_RPM_X10);
    return (int8_t)(left >= right ? trim : -trim);
}

static ER begin_move(S2_ENCODER_MOVE *move, uint16_t target_mm_x10,
                     INT left_direction, INT right_direction,
                     const S2_MOTION_DATA *sample, UW now_ms)
{
    if (!move) return E_PAR;
    if (move->result == S2_MOVE_RUNNING) return E_BUSY;
    if (sample && sample->faults) return E_IO;
    if (!feedback_fresh(sample, now_ms)) return E_TMOUT;
    if (sample->mode != S2_MODE_STOP || sample->command_active ||
        sample->duty_left_x100 || sample->duty_right_x100 ||
        sample->actual_left_rpm_x10 || sample->actual_right_rpm_x10) return E_BUSY;

    move->origin_mm_x10 = sample->distance_centre_mm_x10;
    move->origin_left_mm_x10 = sample->distance_left_mm_x10;
    move->origin_right_mm_x10 = sample->distance_right_mm_x10;
    move->started_ms = now_ms;
    move->trim_sample_ms = sample->speed_timestamp_ms;
    move->left_direction = (int8_t)left_direction;
    move->right_direction = (int8_t)right_direction;
    move->target_mm_x10 = target_mm_x10;
    move->wheels_reached = 0;
    move->result = S2_MOVE_RUNNING;
    move->trim_rpm_x10 = 0;
    return E_OK;
}

ER s2_linear_begin(S2_ENCODER_MOVE *move, INT distance_mm,
                    const S2_MOTION_DATA *sample, UW now_ms)
{
    if (!distance_mm || distance_mm > S2_LINEAR_MAX_MM || distance_mm < -S2_LINEAR_MAX_MM)
        return E_PAR;
    INT direction = distance_mm < 0 ? -1 : 1;
    return begin_move(move, (uint16_t)(distance_mm * direction * 10),
                      direction, direction, sample, now_ms);
}

ER s2_turn_begin(S2_ENCODER_MOVE *move, INT angle_deg, UINT track_mm,
                 const S2_MOTION_DATA *sample, UW now_ms)
{
    if (!angle_deg || angle_deg < -S2_TURN_MAX_DEG || angle_deg > S2_TURN_MAX_DEG ||
        !track_mm || track_mm > S2_TURN_MAX_TRACK_MM) return E_PAR;
    INT direction = angle_deg < 0 ? -1 : 1;
    /* In-place turn: each wheel follows pi * spacing * degrees / 360.
     * Round up to the next tenth of a millimetre. */
    UINT numerator = track_mm * (UINT)(angle_deg * direction) * TURN_PI_NUMERATOR * 10U;
    uint16_t arc = (uint16_t)((numerator + TURN_ARC_DENOMINATOR - 1U) / TURN_ARC_DENOMINATOR);
    return begin_move(move, arc, -direction, direction, sample, now_ms);
}

static void update_targets(S2_ENCODER_MOVE *move, const S2_MOTION_DATA *sample, S2_MOVE_TARGETS *targets)
{
    const BOOL turning = move->left_direction != move->right_direction;
    const uint64_t left = directed_progress(sample->distance_left_mm_x10, move->origin_left_mm_x10, move->left_direction);
    const uint64_t right = directed_progress(sample->distance_right_mm_x10, move->origin_right_mm_x10, move->right_direction);
    UW remaining;

    if (turning) {
        if (left >= move->target_mm_x10) move->wheels_reached |= 1U;
        if (right >= move->target_mm_x10) move->wheels_reached |= 2U;
        if (move->wheels_reached == 3U) {
            move->result = S2_MOVE_REACHED;
            return;
        }
        uint64_t leading = left >= right ? left : right;
        remaining = leading >= move->target_mm_x10 ? 0U : (UW)(move->target_mm_x10 - leading);
    } else {
        uint64_t progress = directed_progress(sample->distance_centre_mm_x10, move->origin_mm_x10, move->left_direction);
        if (progress >= move->target_mm_x10) {
            move->result = S2_MOVE_REACHED;
            return;
        }
        remaining = (UW)(move->target_mm_x10 - progress);
    }

    /* Match the inner speed loop cadence. A wheel at its turn target stays
     * stopped while the other finishes, even if it rolls back slightly. */
    if (sample->speed_timestamp_ms != move->trim_sample_ms) {
        move->trim_rpm_x10 = distance_trim(left, right);
        move->trim_sample_ms = sample->speed_timestamp_ms;
    }
    INT base = turning ? TURN_CRUISE_RPM_X10 : FORWARD_CRUISE_RPM_X10;
    int8_t trim = move->trim_rpm_x10;
    if (remaining <= (turning ? TURN_APPROACH_MM_X10 : FORWARD_APPROACH_MM_X10))
        base = FORWARD_APPROACH_RPM_X10;
    if (move->wheels_reached) {
        base = FORWARD_APPROACH_RPM_X10;
        trim = 0;
    }
    if (!(move->wheels_reached & 1U)) targets->left_rpm_x10 = move->left_direction * (base - trim);
    if (!(move->wheels_reached & 2U)) targets->right_rpm_x10 = move->right_direction * (base + trim);
}

S2_MOVE_RESULT s2_encoder_move_step(S2_ENCODER_MOVE *move,
    const S2_MOTION_DATA *sample, UW now_ms, BOOL permit, S2_MOVE_TARGETS *targets)
{
    if (!targets) return S2_MOVE_FEEDBACK_LOST;
    *targets = (S2_MOVE_TARGETS){0};
    if (!move) return S2_MOVE_IDLE;
    if (move->result != S2_MOVE_RUNNING) return (S2_MOVE_RESULT)move->result;

    UW limit_ms = FORWARD_START_ALLOWANCE_MS + move->target_mm_x10 / 10U * FORWARD_MS_PER_MM;
    if (!permit) move->result = S2_MOVE_CANCELLED;
    else if (sample && sample->faults) move->result = S2_MOVE_FAULT;
    else if (!feedback_fresh(sample, now_ms)) move->result = S2_MOVE_FEEDBACK_LOST;
    else if ((UW)(now_ms - move->started_ms) >= limit_ms) move->result = S2_MOVE_TIMEOUT;
    else update_targets(move, sample, targets);
    return (S2_MOVE_RESULT)move->result;
}
