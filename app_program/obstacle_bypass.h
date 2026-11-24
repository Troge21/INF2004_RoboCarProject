#ifndef APP_OBSTACLE_BYPASS_H
#define APP_OBSTACLE_BYPASS_H

#include "subsystem2/encoder_move.h"
#include "subsystem3/subsystem3.h"
#include "subsystem5/subsystem5.h"

typedef enum {
    BYPASS_IDLE, BYPASS_TURN_OUT, BYPASS_SHIFT_OUT, BYPASS_TURN_ALONG,
    BYPASS_PASS, BYPASS_TURN_IN, BYPASS_SHIFT_IN, BYPASS_ALIGN, BYPASS_FAILED
} BYPASS_PHASE;

/* One sequence, owned by the vehicle controller task. */
typedef struct {
    S2_ENCODER_MOVE move;
    int64_t clearance_position_mm_x10;
    UW scan_id, phase_started_ms, line_timestamp_ms, last_ready_ms, clearance_ms;
    UW checked_return_echo_ms;
    uint8_t phase, line_state, has_clearance, return_clear_samples;
    int8_t side;
} OBSTACLE_BYPASS;

typedef struct {
    const S2_MOTION_DATA *motion;
    const S3_LINE_DATA *line;
    const S5_NAV_DATA *nav;
    const S5_RANGE_DATA *range;
    UW now_ms;
    BOOL ready, settled;
    INT straight_left_rpm_x10, straight_right_rpm_x10;
} BYPASS_INPUT;

/* Returns the existing controller state and the single wheel request. */
unsigned app_bypass_step(OBSTACLE_BYPASS *bypass, const BYPASS_INPUT *input,
                         S2_MOVE_TARGETS *targets);

#endif
