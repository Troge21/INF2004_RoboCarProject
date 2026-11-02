# Subsystem 5 — HC-SR04 ultrasonic test log

**Owner:** Siyona, 2502714. Fixed-position distance measurement only; servo
GP12 remains unused. No scan profile or controller clearance decisions yet.
README.md is unchanged.

## Wiring (power off while connecting)

| HC-SR04 | Connection |
|---|---|
| VCC | Regulated 5 V supply, e.g. Pico VBUS when USB powered; do not assume battery voltage is 5 V |
| GND | Board/Pico ground (common with sensor supply) |
| TRIG | GP4 directly |
| ECHO | GP5 **through a voltage divider**, never directly from the standard 5 V sensor |

```text
HC-SR04 ECHO ---- 2.2 kOhm ----+---- GP5
                              |
                           3.3 kOhm
                              |
                             GND
```

Use ordinary 1% resistors: at 5 V the divider gives about 3.0 V to GP5.
Both resistor values matter; swapping them changes the voltage. Sensor VCC
still needs 5 V; the divider goes only on ECHO. Pico ground and sensor ground
must be connected. Do not attach a motor or encoder to these GP4/GP5 pins.

Reference: [HC-SR04 datasheet](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf)
(5 V supply, >=10 us trigger, cm=echo_us/58, nominal 2–400 cm, >60 ms between
measurements). This implementation triggers for 12 us and waits at least
500 ms after every attempt. GPIO IRQ timestamps use the free-running 1 us
RP2040 timer; interrupt latency still affects accuracy, so the datasheet's
accuracy is not claimed for this firmware.

## Build and observe

From `build_make`, run `make -j8` for normal operation, or `make TEST=1 -j8`
to include software conversion checks. Flash the matching UF2:
`mtk3pico_smp1_usb_cdc_wifi.uf2` or `mtk3pico_smp1_usb_cdc_wifi_test.uf2`.
Leave `S2_ENCODER_CALIBRATION=0` for ultrasonic testing. Calibration and this
bench driver both own GPIO IRQ13; when enabled, calibration takes precedence
and ultrasonic startup is skipped. The task runs
at priority 20 on core 0 (TP_PRC1), matching its GPIO interrupt routing.

Open USB Serial Monitor at 115200. IR monitoring remains enabled; filter the
monitor for `[ULTRA]` if supported. Expected examples (not measured results):

```text
[ULTRA] distance=200 mm echo=1160 us
[ULTRA] INVALID NO_ECHO_TIMEOUT echo=0 us (not clear space)
```

A 40 ms wait bounds missing echo pulses; out-of-range or invalid edge sequences
are rejected. An invalid reading never means the path is clear. A disconnected
ECHO should produce NO_ECHO_TIMEOUT; ECHO_HIGH_BEFORE_TRIGGER suggests a stuck
high signal/wiring problem. ECHO_END_TIMEOUT means a rise was seen without a
completed pulse before the deadline. INVALID_EDGE_SEQUENCE may indicate noise
or interrupt delay. These are diagnostic clues, not proven fault diagnoses.

## Test cases

| ID | What is tested | Method | Steps | Expected result |
|---|---|---|---|---|
| S5-U01 | Task-context example (retired) | historical | Original template only | ID retained; not a hardware test |
| S5-U02 | Distance against ruler | manual | Place a flat board perpendicular to sensor at 100, 200, 500 and 1000 mm, measured from transducer face; save 10 readings at each position | Distance follows target; provisional bench target is within max(20 mm, 5% of ruler distance), to be agreed for product requirements |
| S5-U03 | Echo conversion | auto, TEST=1 | Convert 580 and 5800 us | 100 and 1000 mm |
| S5-U04 | Valid range endpoints | auto, TEST=1 | Convert 116 and 23200 us | 20 and 4000 mm |
| S5-U05 | Invalid widths | auto, TEST=1 | Convert 0, 115 and 23201 us | E_PAR; output distance cleared |
| S5-U06 | Missing echo | manual | Power off, disconnect ECHO at the sensor side, power on and observe | NO_ECHO_TIMEOUT repeats; other tasks continue; never prints a valid distance from a missing pulse |
| S5-U07 | Recovery | manual | Power off, restore proper wiring and divider, power on with flat target at 200 mm | Valid readings resume near 200 mm |
| S5-U08 | Target movement | manual | Move a flat target from 200 to 500 mm | Reading increases and settles; invalid readings remain explicitly marked |

Do not assume pointing into open space always gives a timeout: nearby objects
can reflect sound. Servo movement, temperature compensation, full scan profiles,
and controller interfaces remain future work.

## Results

No HC-SR04 hardware results have been reported yet. Successful compilation
and software-only conversion checks do not prove wiring or physical accuracy.

| Date | Test ID | Commit | Tester | Result | Log / evidence | Notes |
|---|---|---|---|---|---|---|
| YYYY-MM-DD | S5-U01 | `abc1234` | | PASS / FAIL | `logs/YYYY-MM-DD_….txt` | Original template row; not an executed result |
