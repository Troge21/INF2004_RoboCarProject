#include "motion_control.h"

static INT clamp(INT value, INT low, INT high)
{
    return value < low ? low : (value > high ? high : value);
}

INT s2_count_delta(UW now, UW previous)
{
    UW delta = now - previous;
    return delta <= 0x7fffffffU ? (INT)delta : -1 - (INT)(~delta);
}

INT s2_rpm_x10(INT delta, UW elapsed_ms, UINT cpr_x10)
{
    if (elapsed_ms == 0U || cpr_x10 == 0U) return 0;
    return (INT)((int64_t)delta * 6000000LL / ((int64_t)cpr_x10 * elapsed_ms));
}

int64_t s2_distance_mm_x10(int64_t counts, UINT cpr_x10)
{
    if (cpr_x10 == 0U) return 0;
    return counts * S2_WHEEL_DIAMETER_MM * 355LL * 100LL / (113LL * cpr_x10);
}

static void pid_reset(S2_PID_STATE *pid)
{
    *pid = (S2_PID_STATE){0};
}

static void pid_step(S2_PID_STATE *pid, INT target, INT measured,
                     UW integral_dt, UW measurement_dt, UW output_dt, INT ff)
{
    INT error, p, d = 0, candidate_i, unsaturated, desired, max_step;
    BOOL reject_i;
    const INT direction = target < 0 ? -1 : 1;
    const INT feedforward = (INT)((int64_t)target * direction * ff / S2_PID_FF_RPM_X10);
    if (target != pid->previous_target_x10) pid->integral_x100 = 0;
    error = direction * (target - measured);
    p = (INT)((int64_t)error * S2_PID_KP_X1000 / 100);
    if (pid->previous_valid && measurement_dt != 0U)
        d = (INT)(-((int64_t)measured - pid->previous_rpm_x10) * direction *
                  S2_PID_KD_X1000 * 10 / measurement_dt);
    candidate_i = clamp(pid->integral_x100 +
        (INT)((int64_t)error * S2_PID_KI_X1000 * integral_dt / 100000),
        -S2_PID_INTEGRAL_LIMIT_X100, S2_PID_INTEGRAL_LIMIT_X100);
    unsaturated = feedforward + p + candidate_i + d;
    desired = clamp(unsaturated, 0, S2_PID_MAX_DUTY_X100);
    max_step = (INT)((int64_t)S2_PID_SLEW_X100_PER_S * output_dt / 1000);
    reject_i = (unsaturated > S2_PID_MAX_DUTY_X100 && error > 0) ||
               (unsaturated < 0 && error < 0) ||
               (desired > pid->output_x100 + max_step && error > 0) ||
               (desired < pid->output_x100 - max_step && error < 0);
    if (!reject_i) pid->integral_x100 = candidate_i;
    else {
        unsaturated = feedforward + p + pid->integral_x100 + d;
        desired = clamp(unsaturated, 0, S2_PID_MAX_DUTY_X100);
    }
    pid->output_x100 = clamp(desired, pid->output_x100 - max_step,
                            pid->output_x100 + max_step);
    pid->previous_rpm_x10 = measured;
    pid->previous_target_x10 = target;
    pid->previous_valid = TRUE;
}

static BOOL direction_ready(const S2_CONTROL_STATE *s, UINT wheel, INT direction,
                            INT rpm, UW now)
{
    if (!s->last_drive_direction[wheel] || s->last_drive_direction[wheel] == direction)
        return TRUE;
    /* The complete zero-speed window must begin after cutting the old drive,
     * even if a zero command or timeout reset the PID in the meantime. */
    const UW age_of_window_start = (UW)(now - s->data.speed_timestamp_ms) + s->data.speed_period_ms;
    return s->data.speed_valid && rpm == 0 && age_of_window_start <= now - s->last_powered_ms[wheel];
}

void s2_control_init(S2_CONTROL_STATE *s, UW now, S2_ENCODER_COUNTS counts)
{
    *s = (S2_CONTROL_STATE){0};
    s->previous = s->speed_counts = counts;
    s->last_ms = s->speed_ms = now;
    s->data.encoder_valid = TRUE;
    s->data.timestamp_ms = now;
}

void s2_control_step(S2_CONTROL_STATE *s, UW now, S2_ENCODER_COUNTS counts,
                     const S2_COMMAND *cmd, BOOL command_available, BOOL encoder_ok)
{
    S2_MOTION_DATA *out = &s->data;
    INT delta[2] = {0, 0};
    INT targets[2] = {0, 0}, rpms[2], duties[2] = {0, 0};
    UW speed_dt = now - s->speed_ms;
    UW loop_dt = now - s->last_ms;
    BOOL new_speed = FALSE, active;
    UINT i;
    if (!encoder_ok) out->faults |= S2_FAULT_ENCODER;
    if (out->duty_left_x100) s->last_powered_ms[0] = now;
    if (out->duty_right_x100) s->last_powered_ms[1] = now;
    if (loop_dt > S2_LOOP_MAX_GAP_MS &&
        (out->duty_left_x100 || out->duty_right_x100))
        out->faults |= S2_FAULT_TIMING;
    s->last_ms = now;
    if (encoder_ok) {
        delta[0] = s2_count_delta(counts.left, s->previous.left);
        delta[1] = s2_count_delta(counts.right, s->previous.right);
        out->count_left += delta[0];
        out->count_right += delta[1];
        s->previous = counts;
        if (speed_dt >= S2_SPEED_SAMPLE_MS) {
            out->actual_left_rpm_x10 = s2_rpm_x10(
                s2_count_delta(counts.left, s->speed_counts.left), speed_dt, S2_LEFT_COUNTS_PER_REV_X10);
            out->actual_right_rpm_x10 = s2_rpm_x10(
                s2_count_delta(counts.right, s->speed_counts.right), speed_dt, S2_RIGHT_COUNTS_PER_REV_X10);
            out->speed_valid = speed_dt <= S2_FEEDBACK_MAX_AGE_MS;
            out->speed_timestamp_ms = now;
            out->speed_period_ms = speed_dt;
            s->speed_counts = counts;
            s->speed_ms = now;
            new_speed = TRUE;
        }
    }
    out->encoder_valid = encoder_ok;
    if (!encoder_ok || now - out->speed_timestamp_ms > S2_FEEDBACK_MAX_AGE_MS)
        out->speed_valid = FALSE;
    active = command_available && cmd->valid && now - cmd->timestamp_ms <= S2_COMMAND_MAX_AGE_MS;
    out->stop_reason = !command_available ? S2_STOP_COMMAND_BUSY :
        (!active && cmd->valid ? S2_STOP_COMMAND_TIMEOUT : S2_STOP_REQUESTED);
    out->mode = active ? cmd->mode : S2_MODE_STOP;
    if (out->mode == S2_MODE_RPM && !out->speed_valid) {
        /* At boot wait for the first full speed window, with outputs off. */
        active = FALSE;
        out->stop_reason = S2_STOP_FEEDBACK_WAIT;
    }
    if (s->previous_mode != out->mode) {
        pid_reset(&s->pid[0]); pid_reset(&s->pid[1]);
        s->previous_mode = out->mode;
    }
    rpms[0] = out->actual_left_rpm_x10; rpms[1] = out->actual_right_rpm_x10;
    if (active && out->faults == 0U) {
        if (cmd->mode == S2_MODE_RPM) {
            targets[0] = cmd->left; targets[1] = cmd->right;
            for (i = 0; i < 2U; ++i) {
                S2_PID_STATE *pid = &s->pid[i];
                INT direction = targets[i] < 0 ? -1 : 1;
                if (targets[i] == 0 || !direction_ready(s, i, direction, rpms[i], now)) {
                    pid_reset(pid);
                    continue;
                }
                if (pid->previous_valid && (pid->previous_target_x10 < 0) != (direction < 0))
                    pid_reset(pid);
                if (new_speed || targets[i] != pid->previous_target_x10) {
                    /* A steering update can run between encoder samples. The
                     * next sample only earns ramp time since that update. */
                    UW output_dt = pid->previous_valid ? now - pid->last_update_ms : loop_dt;
                    UW integral_dt = targets[i] == pid->previous_target_x10 ? output_dt : loop_dt;
                    pid_step(pid, targets[i], rpms[i], integral_dt,
                             new_speed ? speed_dt : 0U, output_dt,
                             i == 0U ? S2_PID_LEFT_FF_X100 : S2_PID_RIGHT_FF_X100);
                    pid->last_update_ms = now;
                }
                duties[i] = direction * pid->output_x100;
            }
        } else if (cmd->mode == S2_MODE_DUTY) {
            if (direction_ready(s, 0, 1, rpms[0], now)) duties[0] = cmd->left * S2_LEFT_POWER_SCALE;
            if (direction_ready(s, 1, 1, rpms[1], now)) duties[1] = cmd->right * S2_RIGHT_POWER_SCALE;
        }
        for (i = 0; i < 2U; ++i) {
            BOOL requesting = (duties[i] >= 1000 || duties[i] <= -1000) &&
                (cmd->mode != S2_MODE_RPM || targets[i] >= 50 || targets[i] <= -50);
            if (!requesting || delta[i] != 0) {
                s->no_edge_ms[i] = now;
                s->stall_armed[i] = requesting;
            } else if (!s->stall_armed[i]) {
                s->no_edge_ms[i] = now;
                s->stall_armed[i] = TRUE;
            } else if (now - s->no_edge_ms[i] >= S2_STALL_TIMEOUT_MS)
                out->faults |= i == 0U ? S2_FAULT_LEFT_STALL : S2_FAULT_RIGHT_STALL;
            BOOL wrong_direction = (duties[i] > 0 && rpms[i] < -30) ||
                                   (duties[i] < 0 && rpms[i] > 30);
            if (cmd->mode == S2_MODE_RPM && out->speed_valid && wrong_direction) {
                if (!s->direction_armed[i]) { s->direction_ms[i] = now; s->direction_armed[i] = TRUE; }
                else if (now - s->direction_ms[i] >= 400U)
                    out->faults |= i == 0U ? S2_FAULT_LEFT_DIRECTION : S2_FAULT_RIGHT_DIRECTION;
            } else s->direction_armed[i] = FALSE;
        }
    }
    if (!active || cmd->mode == S2_MODE_STOP || out->faults != 0U) {
        duties[0] = duties[1] = targets[0] = targets[1] = 0;
        pid_reset(&s->pid[0]); pid_reset(&s->pid[1]);
        s->stall_armed[0] = s->stall_armed[1] = FALSE;
        s->direction_armed[0] = s->direction_armed[1] = FALSE;
        if (out->faults != 0U) out->stop_reason = S2_STOP_FAULT;
        out->mode = S2_MODE_STOP;
    } else if (cmd->mode != S2_MODE_STOP) out->stop_reason = S2_STOP_NONE;
    out->command_active = active && out->faults == 0U && cmd->mode != S2_MODE_STOP;
    out->target_left_rpm_x10 = targets[0]; out->target_right_rpm_x10 = targets[1];
    out->error_left_rpm_x10 = targets[0] != 0 ? targets[0] - rpms[0] : 0;
    out->error_right_rpm_x10 = targets[1] != 0 ? targets[1] - rpms[1] : 0;
    for (i = 0; i < 2U; ++i) {
        if (duties[i]) s->last_drive_direction[i] = duties[i] < 0 ? -1 : 1;
    }
    out->duty_left_x100 = duties[0]; out->duty_right_x100 = duties[1];
    out->distance_left_mm_x10 = s2_distance_mm_x10(out->count_left, S2_LEFT_COUNTS_PER_REV_X10);
    out->distance_right_mm_x10 = s2_distance_mm_x10(out->count_right, S2_RIGHT_COUNTS_PER_REV_X10);
    out->distance_centre_mm_x10 = (out->distance_left_mm_x10 + out->distance_right_mm_x10) / 2;
    out->speed_centre_mm_s_x10 = (INT)((int64_t)(rpms[0] + rpms[1]) *
        S2_WHEEL_DIAMETER_MM * 355 / (2LL * 113 * 60));
    out->timestamp_ms = now;
    ++out->sequence;
}
