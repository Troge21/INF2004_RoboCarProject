#ifndef S2_MOTION_CONTROL_H
#define S2_MOTION_CONTROL_H
#include "encoder_capture.h"

typedef struct {
    S2_CONTROL_MODE mode;
    INT left, right;
    UW timestamp_ms;
    BOOL valid;
} S2_COMMAND;
typedef struct {
    INT integral_x100, output_x100, previous_rpm_x10, previous_target_x10;
    UW last_update_ms;
    BOOL previous_valid;
} S2_PID_STATE;
typedef struct {
    S2_ENCODER_COUNTS previous, speed_counts;
    UW last_ms, speed_ms, no_edge_ms[2], direction_ms[2];
    UW last_powered_ms[2];
    int8_t last_drive_direction[2];
    BOOL stall_armed[2], direction_armed[2];
    S2_CONTROL_MODE previous_mode;
    S2_PID_STATE pid[2];
    S2_MOTION_DATA data;
} S2_CONTROL_STATE;

INT s2_count_delta(UW now, UW previous);
INT s2_rpm_x10(INT delta, UW elapsed_ms, UINT cpr_x10);
int64_t s2_distance_mm_x10(int64_t counts, UINT cpr_x10);
void s2_control_init(S2_CONTROL_STATE *state, UW now, S2_ENCODER_COUNTS counts);
void s2_control_step(S2_CONTROL_STATE *state, UW now, S2_ENCODER_COUNTS counts,
                     const S2_COMMAND *command, BOOL command_available,
                     BOOL encoder_ok);
#endif
