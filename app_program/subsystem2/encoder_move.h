#ifndef S2_ENCODER_MOVE_H
#define S2_ENCODER_MOVE_H

#include "subsystem2.h"

#define S2_LINEAR_MAX_MM 2000
#define S2_TURN_MAX_TRACK_MM 500U
#define S2_TURN_MAX_DEG 180

typedef enum {
    S2_MOVE_IDLE, S2_MOVE_RUNNING, S2_MOVE_REACHED,
    S2_MOVE_CANCELLED, S2_MOVE_FEEDBACK_LOST, S2_MOVE_FAULT, S2_MOVE_TIMEOUT
} S2_MOVE_RESULT;

/* Owned by the task that arbitrates motor commands. Initialize to zero. */
typedef struct {
    int64_t origin_mm_x10;
    int64_t origin_left_mm_x10, origin_right_mm_x10;
    UW started_ms, trim_sample_ms;
    uint16_t target_mm_x10;
    uint8_t result, wheels_reached;
    int8_t trim_rpm_x10, left_direction, right_direction;
} S2_ENCODER_MOVE;

typedef struct {
    INT left_rpm_x10, right_rpm_x10;
} S2_MOVE_TARGETS;

/* Start a nonzero signed distance from stopped wheels with fresh feedback.
 * Positive is forward. Magnitude is limited to S2_LINEAR_MAX_MM. */
ER s2_linear_begin(S2_ENCODER_MOVE *move, INT distance_mm,
                    const S2_MOTION_DATA *sample, UW now_ms);

/* In-place turn from stopped wheels. Positive is left, negative is right.
 * Pass the calibrated effective spacing between drive-wheel centres in mm. */
ER s2_turn_begin(S2_ENCODER_MOVE *move, INT angle_deg, UINT track_mm,
                 const S2_MOTION_DATA *sample, UW now_ms);

/* Call each control cycle with source-clock time and the latest sample (NULL
 * on a failed read). Submit the individual targets using S2's signed RPM API.
 * A false permit cancels the move. Every terminal result requests zero and
 * stays terminal until an explicit new begin. The caller owns all interlocks. */
S2_MOVE_RESULT s2_encoder_move_step(S2_ENCODER_MOVE *move,
    const S2_MOTION_DATA *sample, UW now_ms, BOOL permit, S2_MOVE_TARGETS *targets);

#endif
