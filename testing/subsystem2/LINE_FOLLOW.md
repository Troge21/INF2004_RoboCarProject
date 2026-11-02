# IR and motor integration

The runtime controller is `vehicle_controller_task()` in `app_program/app_main.c`.
`usermain()` initializes and starts every subsystem, then starts this controller.

- S3 owns IR GPIO reads and publishes a protected snapshot every 20 ms.
- The app_main controller reads S3, applies steering/recovery, and calls
  `subsystem2_set_duty(left, right)` every 20 ms.
- S2 owns PWM and reads a protected latest-command snapshot every 20 ms. Missing
  commands, read contention, or commands older than 100 ms result in zero duty.
- No S3 header or IR steering logic remains in S2. Only S2 writes motor PWM.

## Settings

Driving settings moved from S2 to `app_program/app.h`, with an `APP_` prefix:

| Setting | Current value |
|---|---:|
| APP_LINE_FOLLOW_ENABLED | 1 |
| APP_START_DELAY_MS | 3000 |
| APP_CONTROL_PERIOD_MS | 20 |
| APP_FORWARD_DUTY | 25 |
| APP_INNER_DUTY | 15 |
| APP_RECOVERY_DUTY | 20 |
| APP_RECOVERY_TIMEOUT_MS | 600 |
| APP_IR_MAX_AGE_MS | 100 |

Wheel power scales, wiring direction flags, and the motor command timeout stay
in `subsystem2.h`. Both wheel scales are currently 100 (no power trim).
Normal builds start following after 3 seconds. Set APP_LINE_FOLLOW_ENABLED=0
for stationary work. APP_TEST builds disable both controller and motor task;
manual encoder calibration retains its previous exclusive motor ownership.

## Sensor behavior

All three sensors point toward the floor; 1 means black. Pins: GP26 left,
GP6 centre, GP16 right. A sensor mounted farther behind delays its detection.

| L C R | Left request | Right request |
|---|---:|---:|
| 0 1 0 or 1 1 1 | 25% | 25% |
| 1 1 0 | 15% | 25% |
| 1 0 0 | 0% | 25% |
| 0 1 1 | 25% | 15% |
| 0 0 1 | 25% | 0% |
| 1 0 1 | 0% | 0% |
| 0 0 0 | recover toward last known side, otherwise stop | |

Left history runs only the right wheel during recovery; right history runs only
the left wheel. Centre/all-black preserve side history. Recovery lasts at most
600 ms, then stays stopped until a recognized line pattern returns. Unavailable
or stale sensor data, 101, and invalid patterns stop and clear history.
All-black commands forward; this is not a full junction classification system.

## Bench check

Raise wheels before flashing. Robo Pico wiring is GP8/9 left A/B and GP10/11
right A/B. Recorded bench results say A means vehicle forward for both wheels.
Confirm both wheels run forward on centre-only black. Test all steering patterns,
all-black forward, and 101 stop. Present left, then centre, then all-white: only
the right wheel should run for up to 600 ms. Mirror for right recovery. Return
centre during recovery and verify normal following resumes immediately. Start
on all-white and verify no motion. Physical verification remains pending.

No obstacle stop, encoder speed regulation, or remote stop arbitration is added.
WiFi and ultrasonic status logging remain muted; their tasks still run.

## Build and checks

```sh
export PICO_SDK_PATH="$HOME/pico/pico-sdk"
cd /mnt/c/robocar/build_make
make all -j8
```

Flash `build_make/mtk3pico_smp1_usb_cdc_wifi.uf2`. Startup now prints
`[CTRL] Line follower starts in 3000 ms; duty=25%.`.

Host checks (gcc required) run the actual controller policy and motor API/loop:

```sh
python testing/subsystem2/test_line_recovery.py
python testing/subsystem2/test_motor_commands.py
```

They cover recovery and timeout, all-black forward, stale/ambiguous stops,
command bounds/readiness, motor command expiry, lock contention, and clock wrap.
