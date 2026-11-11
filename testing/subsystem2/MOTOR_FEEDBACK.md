# Motor feedback, PID and future dashboard reference

This implements S2 motor feedback and speed control only. No Wi-Fi,
MQTT, network payload, dashboard or S1 code is changed or added.
The shared GPIO dispatcher also replaces the ultrasonic sensor's private
IRQ13 handler so ultrasonic waits cannot mask wheel encoder interrupts.
Its telemetry publishing code is unchanged.

## What now runs

`subsystem2_init()` creates command and feedback semaphores and initializes
PWM with zero output. `subsystem2_start()` starts the core-0 motor task at
priority 14. That task starts the shared encoder driver, updates counts and
odometry, estimates wheel RPM over actual intervals of at least 200 ms,
applies duty or PID control, writes PWM, and publishes one coherent snapshot.
It never prints or sends network messages.

The encoder driver counts A rising edges, reads B for direction and makes
vehicle-forward movement positive for both wheels. Left A/B are GP16/17;
right A/B GP0/1. The current calibration is L=633.7 and R=540.9 x1 counts per
wheel revolution, with 65 mm outside tyre diameter on both wheels.
These replace the earlier provisional 636.8/588.2 values after the user
confirmed a new zero-start, 10-revolution run ending at raw L=-6337 and
R=5409. The old right denominator under-reported RPM and distance, which
could make PID drive the right wheel too fast while reporting equal RPM.
The PID gains and targets were retained during this calibration update.
Subsequent floor runs are recorded in the validation section below.
Use USB CDC with `CRASH_REPORT=0`; UART uses the right encoder pins.

The current normal firmware still enters the standalone analog IR function.
It now requests **35 RPM / 35 RPM** for straight motion and **26 / 35 RPM**
or **35 / 26 RPM** for steering. The centre/right sensor mapping, both-white
alignment, both-black straight junction behaviour and barcode-only left
sensor are preserved. Set `S2_LINE_USE_RPM_CONTROL=0` to use the earlier
duty-command path. Legacy full-subsystem startup and old digital S3 wiring
are still inactive; they are not an alternate analog configuration.

## Motion APIs (no network dependencies)

```c
#include "subsystem2/subsystem2.h"

/* Refresh every 20 ms while movement is wanted. Units: tenths of RPM. */
ER er = subsystem2_set_rpm_x10(350, 350); /* both wheels 35.0 RPM */
/* Stop request: both targets zero. */
er = subsystem2_set_rpm_x10(0, 0);

/* Read-only; may be called later by a separate telemetry producer. */
S2_MOTION_DATA motion;
er = subsystem2_get_motion(&motion);
```

Speed targets are forward-only, 0..60.0 RPM per wheel. The duty API remains
`subsystem2_set_duty(left, right)` with percentages 0..100. Either command
replaces the previous mode. Zero stops immediately, bypassing the PID ramp.
Commands expire after 100 ms and are not renewed by reading feedback.
Setters return acceptance, not proof the wheel moved: inspect feedback.
`E_OBJ` means S2 is not ready; `E_TMOUT` means semaphore contention, retry
later; `E_PAR` means invalid parameters. Snapshot reads are nonblocking.

Counts and distances accumulate from S2 startup and reset on reboot.
They are signed **net** travel, not absolute forward-plus-backward travel.
There is no position/heading estimate or encoder-based turn command yet.

## Fields to display on the dashboard later

All fields below are already available in `S2_MOTION_DATA`. Polling the
snapshot at roughly 4 Hz is sufficient for a demo; no network integration
is implemented in this change.

| Display | Snapshot fields | Conversion / meaning |
|---|---|---|
| Left/right actual wheel RPM | `actual_left_rpm_x10`, `actual_right_rpm_x10` | Divide by 10. Main speed gauges/lines. |
| Left/right requested wheel RPM | `target_left_rpm_x10`, `target_right_rpm_x10` | Divide by 10. Plot against actual RPM to demonstrate PID. Zero outside active RPM mode. |
| Left/right speed error | `error_left_rpm_x10`, `error_right_rpm_x10` | Divide by 10; target minus actual, only for nonzero RPM targets. |
| Left/right applied motor duty | `duty_left_x100`, `duty_right_x100` | Divide by 100 for percent. Duty may differ while RPM matches. This is PWM duty, not electrical watts. |
| Left/right encoder counts | `count_left`, `count_right` | Signed 64-bit x1 counts, forward positive. Each wheel has its own counts/revolution. |
| Left/right wheel travel | `distance_left_mm_x10`, `distance_right_mm_x10` | Divide by 10 for mm or 10000 for metres. |
| Estimated vehicle travel | `distance_centre_mm_x10` | Divide by 10 for mm or 10000 for metres; average of wheel travel. |
| Estimated vehicle speed | `speed_centre_mm_s_x10` | Divide by 10 for mm/s, 100 for cm/s, or 10000 for m/s. |
| Control mode | `mode` | `S2_MODE_STOP=0`, `S2_MODE_DUTY=1`, `S2_MODE_RPM=2`. |
| Command active | `command_active` | Current, unexpired motion request with no latched fault. |
| Health / fault text | `faults`, `stop_reason` | Decode the masks/reasons below. Fault takes priority over normal mode text. |
| Freshness / live indicator | `timestamp_ms`, `speed_timestamp_ms`, `speed_valid`, `encoder_valid`, `sequence` | See freshness rules below. |
| Diagnostic sample period | `speed_period_ms` | Actual encoder speed window duration. |

Keep the UI simple: actual/target RPM, duty %, distance and status are the
main view. Counts, errors and timing can be a details section.

Before treating a reading as live, the firmware consumer should check
`(UW)(now_ms - motion.timestamp_ms) <= S2_FEEDBACK_MAX_AGE_MS` (500 ms),
`encoder_valid` and `speed_valid`. If not, show unavailable/stale rather
than zero. A stalled encoder still produces a numerical zero speed;
`encoder_valid` describes driver/read health, not a cable-presence test.
Faults must be checked separately. `sequence` increments at each motor tick.
Timestamps are milliseconds since kernel boot, wrapping at 32 bits; they
are not calendar timestamps and cannot be compared with browser wall time.
A future sender can provide computed age and a receiver can track arrival
time. Do not put networking calls inside the motor task.

## PID behaviour and faults

The two wheels have independent PID states. Initial feed-forward comes
from the 18%/20% floor balance at approximately 35 RPM. Output has saturation,
integral anti-windup, slew limiting and derivative on measurement. Initial
gains are Kp=0.100, Ki=0.200 and Kd=0.000 in duty-percent and RPM units;
the D implementation is available but starts disabled to avoid amplifying
encoder quantization. These are **provisional gains requiring hardware tuning**.
PID duty is limited to 40%, with a 20 percentage-point/second slew limit.
Duty-mode commands retain the earlier 0..100% range and manual power scales.
The PID feed-forward already includes wheel balance, so those scales are
not applied a second time in RPM mode.

| Fault bit | Meaning |
|---|---|
| `0x01` | Encoder setup/read failed. |
| `0x02` | Motor loop gap exceeded 250 ms while output was active. |
| `0x04` / `0x08` | Left/right wheel produced no edges for 1.5 s while driven at at least 10% duty (and at least 5 RPM requested in RPM mode). |
| `0x10` / `0x20` | Left/right measured direction stayed opposite the positive RPM request for 400 ms, below -3 RPM. |

Any fault stops both outputs and latches until reboot. Command expiry or
command-lock contention also stops outputs, but a fresh command can resume
once no latched fault exists. `stop_reason` distinguishes requested stop,
normal motion, expired command, command contention, fault and waiting for
the initial feedback window. A zero target on one wheel stops that wheel
immediately without treating it as stalled.

## On-board motor test

Flash **`build_make/mtk3pico_smp1_usb_cdc_wifi_test_s2_pid.uf2`** with both
wheels raised. It waits **5 seconds**, requests **35 RPM on both wheels**
for **10 seconds**, stops, then prints the buffered samples. Reset repeats
the countdown. IR steering and all other application subsystems stay off
in this bench. The separate normal UF2 runs the PID line follower.

Expected fields (not promised measured values):

```text
[S2-PID] sample=... target_rpm_x10 L=350 R=350; actual_rpm_x10 L=... R=...; duty_x100 L=... R=...; speed_valid=1 faults=0x0
[S2-PID] sample=0 target_rpm_x10 L=0 R=0; ... duty_x100 L=0 R=0; ...
[S2-DIST] counts L=... R=...; distance_mm_x10 L=... R=... centre=...
[S2-PID] DONE. Motor trial stopped; reset repeats countdown.
```

For example, `actual_rpm_x10=348` means 34.8 RPM and `duty_x100=1875`
means 18.75% duty. Duty should adjust rather than stay fixed at 18/20.
The first sample includes acceleration/slew limiting; inspect later samples.
`sample=0` is the final stopped snapshot, not the first moving sample.
Actual RPM may briefly remain nonzero there because the speed window can
include the last movement. The trip-distance line subtracts the snapshot
at run start, excluding movement during the countdown.

Check visible forward rotation before floor testing or tuning. The run was
extended from 5 to 10 seconds to observe
settling after the initial overshoot; PID gains remain unchanged. Inspect
the late samples, normally 5 through 9, for convergence or oscillation.
Simulation does not validate physical PID response.

From `build_make`, with the Pico SDK configured:

```sh
make all TEST=1 CONSOLE=usb_cdc CRASH_REPORT=0 EXE_FILE=mtk3pico_smp1_usb_cdc_wifi_test_s2_pid -j8
```

Use `S2_ENCODER_PID_TEST=0` to compare the original open-loop duty trial,
or `S2_ENCODER_POWERED_TEST=0` for hand-turn counts. Rebuild after changes.
Hand-turn output now uses the shared driver's forward-positive sign on both
wheels. Encoder-based turns and exact move-distance commands remain future
features; this test still stops on elapsed time.

## Validation record

Before cleanup, PC checks passed for odometry/sign/wrap, PID P/I/D and
anti-windup, unequal-load response, timeouts, latched faults, command bounds,
coherent snapshots, applied PWM, setup errors, shared echo/encoder interrupts,
bench timing and IR mappings. The PC check scripts and their helper were
removed at the user's request; they were never part of the firmware build.
Normal and PID bench ARM builds passed before cleanup.

With the corrected 633.7/540.9 calibration, the user reported mostly straight
floor driving with very slight right drift. Two reported 10-second runs gave:

| Run | Left wheel estimate | Right wheel estimate | Average estimate |
|---|---|---|---|
| 1 | 1130.0 mm | 1121.2 mm | 1125.6 mm |
| 2 | 1139.7 mm | 1144.6 mm | 1142.1 mm |

These are encoder estimates, not tape-measured distances or lateral drift.
Both runs reported valid speed feedback, no faults and zero duty at the stop.
The current settings are a working baseline; precise heading correction,
further tuning and encoder-based turning remain unfinished. The retained
on-board bench can still be rebuilt for physical demonstrations.
