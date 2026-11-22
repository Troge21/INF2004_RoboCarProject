#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define APP_H
#define APP_TEST 0
#define APP_BENCH_MODE 0
#define EXPORT
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_IO (-57)
#define E_TMOUT (-50)
#define E_BUSY (-65)
typedef int INT, BOOL, ER;
typedef unsigned UINT;
typedef uint32_t UW;

#include "encoder_move.c"
#include "motion_control.c"

static S2_MOTION_DATA sample(UW now, int64_t position)
{
    return (S2_MOTION_DATA){.encoder_valid = TRUE, .speed_valid = TRUE,
        .timestamp_ms = now, .speed_timestamp_ms = now,
        .distance_centre_mm_x10 = position};
}

static void distance_and_failure_checks(void)
{
    S2_ENCODER_MOVE move = {0};
    const UW start = UINT32_MAX - 10U;
    const int64_t origin = -500;
    S2_MOTION_DATA motion = sample(start, origin);
    S2_MOVE_TARGETS targets;

    assert(s2_linear_begin(&move, 0, &motion, start) == E_PAR);
    assert(s2_linear_begin(&move, S2_LINEAR_MAX_MM + 1, &motion, start) == E_PAR);
    assert(s2_linear_begin(&move, 300, NULL, start) == E_TMOUT);
    motion.actual_left_rpm_x10 = 1;
    assert(s2_linear_begin(&move, 300, &motion, start) == E_BUSY);
    motion.actual_left_rpm_x10 = 0;
    assert(s2_linear_begin(&move, 300, &motion, start) == E_OK);
    assert(s2_linear_begin(&move, 500, &motion, start) == E_BUSY);

    motion = sample(start + 20, origin - 3500);
    assert(s2_encoder_move_step(&move, &motion, start + 20, TRUE, &targets) == S2_MOVE_RUNNING);
    INT cruise = targets.left_rpm_x10;
    assert(targets.right_rpm_x10 == cruise);
    assert(cruise > 0);
    motion = sample(start + 40, origin + 2500);
    assert(s2_encoder_move_step(&move, &motion, start + 40, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 > 0 && targets.left_rpm_x10 < cruise);
    assert(targets.right_rpm_x10 == targets.left_rpm_x10);
    motion = sample(start + 60, origin + 2999);
    assert(s2_encoder_move_step(&move, &motion, start + 60, TRUE, &targets) == S2_MOVE_RUNNING && targets.left_rpm_x10 > 0 && targets.right_rpm_x10 > 0);
    motion = sample(start + 80, origin + 3004);
    assert(s2_encoder_move_step(&move, &motion, start + 80, TRUE, &targets) == S2_MOVE_REACHED && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
    motion = sample(start + 100, origin + 2950);
    assert(s2_encoder_move_step(&move, &motion, start + 100, TRUE, &targets) == S2_MOVE_REACHED && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);

    assert(s2_linear_begin(&move, 300, &motion, start + 100) == E_OK);
    assert(s2_encoder_move_step(&move, &motion, start + 100, FALSE, &targets) == S2_MOVE_CANCELLED && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
    assert(s2_encoder_move_step(&move, &motion, start + 100, TRUE, &targets) == S2_MOVE_CANCELLED && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
    assert(s2_linear_begin(&move, 300, &motion, start + 100) == E_OK);
    assert(s2_encoder_move_step(&move, &motion, start + 200, TRUE, &targets) == S2_MOVE_FEEDBACK_LOST && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
    motion = sample(start + 200, origin);
    assert(s2_linear_begin(&move, 300, &motion, start + 200) == E_OK);
    motion.speed_timestamp_ms -= S2_FEEDBACK_MAX_AGE_MS + 1;
    assert(s2_encoder_move_step(&move, &motion, start + 200, TRUE, &targets) == S2_MOVE_FEEDBACK_LOST && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
    motion = sample(start + 200, origin);
    assert(s2_linear_begin(&move, 300, &motion, start + 200) == E_OK);
    motion.faults = S2_FAULT_LEFT_STALL;
    assert(s2_encoder_move_step(&move, &motion, start + 200, TRUE, &targets) == S2_MOVE_FAULT && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
    motion = sample(start + 200, origin);
    assert(s2_linear_begin(&move, 300, &motion, start + 200) == E_OK);
    motion = sample(start + 12000, origin);
    assert(s2_encoder_move_step(&move, &motion, start + 12000, TRUE, &targets) == S2_MOVE_TIMEOUT && targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
}

static void motor_cutoff_check(INT direction)
{
    S2_ENCODER_MOVE move = {0};
    S2_CONTROL_STATE motor;
    S2_ENCODER_COUNTS counts = {0};
    S2_COMMAND cmd = {.mode = S2_MODE_STOP, .valid = TRUE};
    S2_MOVE_RESULT result = S2_MOVE_IDLE;
    BOOL moving = FALSE, correcting = FALSE;
    s2_control_init(&motor, 0, counts);

    for (UW ms = 20; ms < 11500; ms += 20) {
        if (motor.data.duty_left_x100) counts.left += (UW)(7 * direction);
        if (motor.data.duty_right_x100) counts.right += (UW)(5 * direction);
        s2_control_step(&motor, ms, counts, &cmd, TRUE, TRUE);
        if (ms == S2_SPEED_SAMPLE_MS)
            assert(s2_linear_begin(&move, direction * 300, &motor.data, ms) == E_OK);
        S2_MOVE_TARGETS targets;
        result = s2_encoder_move_step(&move, &motor.data, ms, TRUE, &targets);
        if (result == S2_MOVE_REACHED) {
            assert(moving && correcting && direction * motor.data.distance_centre_mm_x10 >= 3000);
            assert(targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
            cmd = (S2_COMMAND){.mode = S2_MODE_STOP, .timestamp_ms = ms, .valid = TRUE};
            s2_control_step(&motor, ms + 20, counts, &cmd, TRUE, TRUE);
            assert(motor.data.duty_left_x100 == 0 && motor.data.duty_right_x100 == 0);
            break;
        }
        if (motor.data.duty_left_x100 && motor.data.duty_right_x100) {
            moving = TRUE;
            assert(direction * motor.data.duty_left_x100 > 0 && direction * motor.data.duty_right_x100 > 0);
            if (direction * motor.data.target_left_rpm_x10 < direction * motor.data.target_right_rpm_x10)
                correcting = TRUE;
        }
        cmd = (S2_COMMAND){.mode = result == S2_MOVE_RUNNING ? S2_MODE_RPM : S2_MODE_STOP,
            .left = targets.left_rpm_x10, .right = targets.right_rpm_x10, .timestamp_ms = ms, .valid = TRUE};
    }
    assert(result == S2_MOVE_REACHED);
}

static void reverse_fault_check(BOOL wrong_direction)
{
    S2_CONTROL_STATE motor;
    S2_ENCODER_COUNTS counts = {0};
    S2_COMMAND command = {.mode = S2_MODE_RPM, .left = -350, .right = -350, .valid = TRUE};
    s2_control_init(&motor, 0, counts);
    for (UW ms = 20; ms <= S2_STALL_TIMEOUT_MS + 2000U && !motor.data.faults; ms += 20) {
        if (wrong_direction && motor.data.duty_left_x100 < 0) counts.left += 7;
        if (wrong_direction && motor.data.duty_right_x100 < 0) counts.right -= 6;
        command.timestamp_ms = ms;
        s2_control_step(&motor, ms, counts, &command, TRUE, TRUE);
    }
    UW expected = wrong_direction ? S2_FAULT_LEFT_DIRECTION : S2_FAULT_LEFT_STALL;
    assert(motor.data.faults & expected);
    if (wrong_direction)
        assert(motor.data.timestamp_ms < S2_STALL_TIMEOUT_MS);
    else
        assert(motor.data.timestamp_ms - motor.no_edge_ms[0] == S2_STALL_TIMEOUT_MS);
    assert(!motor.data.duty_left_x100 && !motor.data.duty_right_x100);
}

static S2_MOTION_DATA wheel_sample(UW now, int64_t left, int64_t right)
{
    S2_MOTION_DATA motion = sample(now, (left + right) / 2);
    motion.distance_left_mm_x10 = left;
    motion.distance_right_mm_x10 = right;
    return motion;
}

static void correction_checks(void)
{
    S2_ENCODER_MOVE move = {0};
    S2_MOVE_TARGETS targets;
    const int64_t left_origin = -420, right_origin = 2600;
    S2_MOTION_DATA motion = wheel_sample(1000, left_origin, right_origin);
    assert(s2_linear_begin(&move, 300, &motion, 1000) == E_OK);
    assert(s2_encoder_move_step(&move, &motion, 1000, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 350 && targets.right_rpm_x10 == 350);

    motion = wheel_sample(1200, left_origin + 120, right_origin + 100);
    assert(s2_encoder_move_step(&move, &motion, 1200, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 350 && targets.right_rpm_x10 == 350);

    motion = wheel_sample(1400, left_origin + 250, right_origin + 175);
    assert(s2_encoder_move_step(&move, &motion, 1400, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 340 && targets.right_rpm_x10 == 360);

    motion = wheel_sample(1420, left_origin + 255, right_origin + 255);
    motion.speed_timestamp_ms = 1400;
    assert(s2_encoder_move_step(&move, &motion, 1420, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 340 && targets.right_rpm_x10 == 360);

    motion = wheel_sample(1600, left_origin + 600, right_origin + 1200);
    assert(s2_encoder_move_step(&move, &motion, 1600, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 380 && targets.right_rpm_x10 == 320);

    motion = wheel_sample(1800, left_origin + 2800, right_origin + 2200);
    assert(s2_encoder_move_step(&move, &motion, 1800, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 210 && targets.right_rpm_x10 == 270);

    motion = wheel_sample(2000, left_origin + 3310, right_origin + 2700);
    assert(s2_encoder_move_step(&move, &motion, 2000, TRUE, &targets) == S2_MOVE_REACHED);
    assert(targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
    assert(s2_linear_begin(&move, 300, &motion, 2000) == E_OK);
    assert(s2_encoder_move_step(&move, &motion, 2000, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 350 && targets.right_rpm_x10 == 350);

    move = (S2_ENCODER_MOVE){0};
    motion = wheel_sample(3000, left_origin, right_origin);
    assert(s2_linear_begin(&move, -S2_LINEAR_MAX_MM - 1, &motion, 3000) == E_PAR);
    assert(s2_linear_begin(&move, -300, &motion, 3000) == E_OK);
    motion = wheel_sample(3200, left_origin + 3200, right_origin + 3200);
    assert(s2_encoder_move_step(&move, &motion, 3200, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == -350 && targets.right_rpm_x10 == -350);
}

static void turn_checks(void)
{
    S2_ENCODER_MOVE move = {0};
    S2_MOVE_TARGETS targets;
    const int64_t left_origin = 900, right_origin = -300;
    const UW start = UINT32_MAX - 40U;
    S2_MOTION_DATA motion = wheel_sample(start, left_origin, right_origin);
    assert(s2_turn_begin(&move, 0, 120, &motion, start) == E_PAR);
    assert(s2_turn_begin(&move, 181, 120, &motion, start) == E_PAR);
    assert(s2_turn_begin(&move, 90, 0, &motion, start) == E_PAR);
    assert(s2_turn_begin(&move, 90, S2_TURN_MAX_TRACK_MM + 1U, &motion, start) == E_PAR);
    assert(s2_turn_begin(&move, 90, 120, &motion, start) == E_OK);
    assert(move.target_mm_x10 == 943);
    assert(s2_turn_begin(&move, -90, 120, &motion, start) == E_BUSY);
    assert(s2_encoder_move_step(&move, &motion, start, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 < 0 && targets.right_rpm_x10 > 0);

    /* A leading wheel stops immediately. An equal average arc is not enough
     * to finish until the trailing wheel also reaches its target. */
    motion = wheel_sample(start + 40, left_origin - 1000, right_origin + 890);
    assert(s2_encoder_move_step(&move, &motion, start + 40, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 0 && targets.right_rpm_x10 > 0);
    motion = wheel_sample(start + 60, left_origin - 935, right_origin + 920);
    assert(s2_encoder_move_step(&move, &motion, start + 60, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 == 0 && targets.right_rpm_x10 > 0);
    motion = wheel_sample(start + 80, left_origin - 935, right_origin + 950);
    assert(s2_encoder_move_step(&move, &motion, start + 80, TRUE, &targets) == S2_MOVE_REACHED);
    assert(targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);

    motion = wheel_sample(0, left_origin, right_origin);
    assert(s2_turn_begin(&move, -90, 120, &motion, 0) == E_OK);
    assert(move.target_mm_x10 == 943);
    assert(s2_encoder_move_step(&move, &motion, 0, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 > 0 && targets.right_rpm_x10 < 0);
    motion = wheel_sample(20, left_origin - 1100, right_origin + 1100);
    assert(s2_encoder_move_step(&move, &motion, 20, TRUE, &targets) == S2_MOVE_RUNNING);
    assert(targets.left_rpm_x10 > 0 && targets.right_rpm_x10 < 0);
    assert(s2_encoder_move_step(&move, &motion, 20, FALSE, &targets) == S2_MOVE_CANCELLED);
    assert(targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);

    motion = wheel_sample(30, left_origin, right_origin);
    assert(s2_turn_begin(&move, 180, 120, &motion, 30) == E_OK);
    assert(move.target_mm_x10 == 1885);
    motion = wheel_sample(50, left_origin - 950, right_origin + 950);
    assert(s2_encoder_move_step(&move, &motion, 50, TRUE, &targets) == S2_MOVE_RUNNING);
    motion = wheel_sample(70, left_origin - 1885, right_origin + 1885);
    assert(s2_encoder_move_step(&move, &motion, 70, TRUE, &targets) == S2_MOVE_REACHED);
    assert(targets.left_rpm_x10 == 0 && targets.right_rpm_x10 == 0);
}

static void motor_turn_check(void)
{
    S2_ENCODER_MOVE move = {0};
    S2_CONTROL_STATE motor;
    S2_ENCODER_COUNTS counts = {0};
    S2_COMMAND command = {.mode = S2_MODE_STOP, .valid = TRUE};
    s2_control_init(&motor, 0, counts);
    s2_control_step(&motor, 200, counts, &command, TRUE, TRUE);
    assert(s2_turn_begin(&move, -180, 120, &motor.data, 200) == E_OK);

    for (UW now = 200; now < 6500; now += 20) {
        S2_MOVE_TARGETS targets;
        S2_MOVE_RESULT result = s2_encoder_move_step(&move, &motor.data, now, TRUE, &targets);
        assert(result == S2_MOVE_RUNNING || result == S2_MOVE_REACHED);
        command = (S2_COMMAND){.mode = result == S2_MOVE_RUNNING ? S2_MODE_RPM : S2_MODE_STOP,
            .timestamp_ms = now, .left = targets.left_rpm_x10, .right = targets.right_rpm_x10,
            .valid = TRUE};
        if (motor.data.duty_left_x100 > 0) counts.left += 7;
        if (motor.data.duty_right_x100 < 0) counts.right -= 5;
        s2_control_step(&motor, now + 20, counts, &command, TRUE, TRUE);
        assert(!motor.data.faults);
        assert(motor.data.duty_left_x100 >= 0 && motor.data.duty_right_x100 <= 0);
        if (result == S2_MOVE_REACHED) {
            assert(motor.data.distance_left_mm_x10 >= 1885 && motor.data.distance_right_mm_x10 <= -1885);
            assert(!motor.data.duty_left_x100 && !motor.data.duty_right_x100);
            return;
        }
    }
    assert(!"U-turn did not finish in the motor fixture");
}

int main(void)
{
    distance_and_failure_checks();
    correction_checks();
    motor_cutoff_check(1);
    motor_cutoff_check(-1);
    reverse_fault_check(FALSE);
    reverse_fault_check(TRUE);
    turn_checks();
    motor_turn_check();
    puts("PASS: signed distance, left/right/U-turn geometry, per-wheel stopping, reverse faults, clock wrap and stale/timeout/cancel stops.");
    return 0;
}
