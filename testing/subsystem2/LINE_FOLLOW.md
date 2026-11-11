# IR and motor integration

The runtime controller is `vehicle_controller_task()` in `app_program/app_main.c`.
`usermain()` initializes and starts every subsystem, then starts this controller.

- S3 owns IR GPIO reads and publishes a protected snapshot every 20 ms.
- The app_main controller reads S3, applies steering, and calls
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
| APP_INNER_DUTY | 20 |
| APP_RECOVERY_DUTY | 20 |
| APP_RECOVERY_TIMEOUT_MS | 200 |
| APP_IR_MAX_AGE_MS | 100 |

Wheel power scales, wiring direction flags, and the motor command timeout stay
in `subsystem2.h`. Both wheel scales are currently 100 (no power trim).
Normal builds start following after 3 seconds. Set APP_LINE_FOLLOW_ENABLED=0
for stationary work. APP_TEST builds disable both controller and motor task;
manual encoder calibration retains its previous exclusive motor ownership.

## Sensor behavior

Centre (GP6) and right (GP16) track the line; 1 means black. Centre is
the left member of this tracking pair. Mount the pair so both see black when
aligned on the line. Only centre/right contribute to the line pattern. Left GP26 is sampled
independently for barcode decoding and cannot affect steering. Barcode results
print on the console; decoded characters do not issue motor commands.

| Centre | Right | Left request | Right request |
|---|---|---:|---:|
| 1 | 1 | 25% | 25% |
| 1 | 0 | 15% | 25% |
| 0 | 1 | 25% | 15% |
| 0 | 0 | recover toward last detected side, otherwise stop | |

When neither sensor sees black, recover toward the last detected side for up to
200 ms at 20% duty: centre-only history runs the right wheel; right-only history
runs the left wheel. Both-black preserves side history. No side history means
stop immediately. After timeout, remain stopped until a recognized line returns.
Unavailable/stale data and invalid patterns stop and clear history. Following
resumes on reacquisition. Tasks each run every 20 ms; physical stopping can lag
zero duty. Both-black commands forward; junction classification is pending.

## Bench check

Raise wheels before flashing. Robo Pico wiring is GP8/9 left A/B and GP10/11
right A/B. Recorded bench results say A means vehicle forward for both wheels.
Confirm both wheels run forward when centre/right both see black. Test each
single-sensor correction. After each single-sensor correction, present both-white and verify a recovery
turn toward that side for at most 200 ms, followed by zero power on both wheels.
Keep both-white and verify they stay stopped. Return a line sensor to black and verify following resumes.
Start on both-white and verify no motion.
Physical verification remains pending.

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

The existing legacy controller-policy check (gcc required) is:

```sh
python testing/subsystem2/test_line_recovery.py
```

It covers the legacy controller's recovery direction and timeout, steering,
reacquisition, stale/invalid stops, and clock wrap. The current analog/PID
setup is documented in `testing/subsystem2/MOTOR_FEEDBACK.md`.
