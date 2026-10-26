# Subsystem 2 — motor/encoder test log

Motor control and encoder calibration now live in subsystem2, matching TEAM.md.
Earlier bench tests ran temporarily in subsystem1. Their S1-U04 through
S1-U13 identifiers and raw serial output are retained for traceability;
they are historical IDs, not renamed or newly executed subsystem2 tests.

## Current state

The LED example is removed, including subsystem4's LED pause/resume calls.
IR console monitoring remains muted via S3_IR_LOG_ENABLED=0.
Encoder calibration is retained but **disabled by default** so other subsystem
testing can continue. No motor or encoder test starts with the default setting.

To repeat calibration, set `S2_ENCODER_CALIBRATION` to `1` in
`app_program/subsystem2/subsystem2.h`, then run `make TEST=1 -j8` from
`build_make` and flash `mtk3pico_smp1_usb_cdc_wifi_test.uf2`. Keep
`CONSOLE=usb_cdc CRASH_REPORT=0`: the right encoder uses GP0/GP1.
Set the flag back to `0` and rebuild when finished. Normal builds use TEST=0.
While calibration is enabled, subsystem5 skips ultrasonic startup so the two
bench handlers do not compete for GPIO IRQ13.

In calibration mode motors stay off/coasting; raise the wheels, use 3.3 V
encoder signals with common ground, and do not press motor-test buttons.
Record START, manually turn one wheel exactly 10 revolutions in one direction,
record END, and calculate `abs(END - START) / 10`. Live counts print at
115200 baud once per second and reset to zero on reboot. Do not force a
gearbox that resists hand rotation. The x1 decoder counts A rising edges
and reads B for sign; it owns GPIO IRQ13 exclusively on core 0.

## Tests performed (historical hardware evidence)

1. Verified left GP8/GP9 and right GP10/GP11 motor outputs with board buttons.
   Right motor wiring was reversed so the A input corresponds to vehicle forward.
2. Powered bench sequence: 60% PWM, two seconds per direction, one wheel at
   a time, with brake pauses. Fixed missing PWM reset release with USE_PTMR=0.
3. Verified encoder activity, opposite signs on reversal, and zero rising
   edges for one second after a 500 ms stopping/settling delay.
4. Replaced powered test with manual calibration and completed three reported
   10-turn trials per wheel. The powered sequence is historical and no longer
   present in the current calibration source.

## Test cases

S1-U01 through S1-U03 are retired LED examples. S1-U04 through S1-U11 refer
to the earlier powered bench image, not the current calibration firmware.

| ID | What is tested | Requirement / interface | Method | Steps / how | Expected result |
|---|---|---|---|---|---|
| S1-U01 | cyclic handler is running (legacy) | | retired | Removed from the focused motor bench test | ID retained for historical results |
| S1-U02 | LED task sleeps between ticks (legacy) | | retired | Removed from the focused motor bench test | ID retained for historical results |
| S1-U03 | LED on GP19 toggles every 250 ms (legacy) | | retired | LED example and monitor coupling removed | ID retained for historical results |
| S1-U04 | Left motor bench sequence completes | GP8/GP9 PWM; right GP10/GP11 stopped | auto + manual | Flash TEST=1 image, raise wheels, wait 15 seconds; watch left forward and reverse at 60% duty for 2 seconds each | U04 PASS means sequence completed; visually confirm left wheel reverses and right wheel stays stopped |
| S1-U05 | Left encoder responds in both directions | GP7 encoder A, GP28 encoder B, x1 rising-A decoding | auto | Same sequence; read `[ENC]` deltas | At least 4 counts in magnitude per direction |
| S1-U06 | Left encoder direction reverses | GP7/GP28 | auto | Compare forward/reverse deltas | Opposite signs; either sign may correspond to forward depending on encoder wiring |
| S1-U07 | No pulses while stopped | GP7 rising edges | auto | After reverse, allow 500 ms settling then observe for 1 second without touching wheel | Zero rising edges; noise or continued motion causes FAIL |
| S1-U08 | Right motor bench sequence completes | GP10/GP11 PWM; left stopped | auto + manual | After the left wheel test, right runs forward/reverse at 60% for 2 seconds each | Sequence completes; visually confirm direction and that left stays stopped |
| S1-U09 | Right encoder responds in both directions | GP0=A, GP1=B, x1 rising-A decoding | auto | Read right forward/reverse counts | Magnitude at least 4 each |
| S1-U10 | Right encoder direction reverses | GP0/GP1 | auto | Compare right forward/reverse signs | Opposite signs; either sign may mean forward |
| S1-U11 | Right encoder stays quiet while stopped | GP0 rising edges | auto | Settle 500 ms, observe 1 second | Zero rising edges |

| S1-U12 | Both live encoder channels | GP7/GP28 left, GP0/GP1 right | manual | Turn one wheel at a time and stop | Its count changes, then stays steady; other channel should stay steady |
| S1-U13 | Counts per wheel revolution | x1 decoding | manual | Three 10-revolution trials per wheel | Repeatable count differences; save provisional averages |

## Powered bench evidence

User-pasted serial excerpts are saved in
[logs/2026-10-26-motor-encoder.txt](logs/2026-10-26-motor-encoder.txt).
These are reported observations, not a tool-controlled hardware run or full
serial capture. The right test explicitly printed four PASS results; the
left summary meets pulse, reversal and stopped criteria.

| Wheel | Forward | Reverse | Stopped | Error |
|---|---:|---:|---:|---:|
| Left | -2867 | 2879 | 0 | 0 |
| Right | 2532 | -3027 | 0 | 0 |

Additional repeats also showed opposite signs and zero stopped counts.
For the observed wiring, left vehicle-forward counts are negative and right
vehicle-forward counts are positive. Normalize these signs in the final
controller. Time-based counts are not counts per revolution.

## Calibration measurements

Counts are cumulative, not reset between trials. Calculation assumes the
reported movement was exactly 10 wheel revolutions each time, initial LEFT=0,
and first right trial START=2 (the observed count before turning right).

| Trial | Left start | Left end | Left counts/rev | Right start | Right end | Right counts/rev |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 0 | -6341 | 634.1 | 2 | 5354 | 535.2 |
| 2 | -6341 | -12662 | 632.1 | 5354 | 10731 | 537.7 |
| 3 | -12662 | -19005 | 634.3 | 10731 | 16079 | 534.8 |
| Average | | | **633.5** | | | **535.9** |

These are provisional x1 counts per wheel revolution. The repeatable
left/right difference is not yet explained; check encoder/gearbox specs and
verify with a measured-distance test before relying on it for navigation.
The user estimated circumference at 20–22 cm; 210 mm is only an approximate
working value, giving about 0.331 mm/count left and 0.392 mm/count right.
No calibrated distance, speed accuracy, straight driving, turning, target
stopping or closed-loop control has been verified yet.

## Results

Dates use the session date. Commit is unrecorded; code is uncommitted.

| Date | Test ID | Commit | Tester | Result | Log / evidence | Notes |
|---|---|---|---|---|---|---|
| YYYY-MM-DD | S1-U01 | `abc1234` | | PASS / FAIL | `logs/YYYY-MM-DD_….txt` | Original template row; not an executed result |
| 2026-10-26 | S1-U04–S1-U07 | unrecorded | User | Criteria met from reported summary | logs/2026-10-26-motor-encoder.txt | Left counts nonzero/opposite; stopped=0, error=0; explicit left PASS lines not supplied |
| 2026-10-26 | S1-U08–S1-U11 | unrecorded | User | PASS (reported) | logs/2026-10-26-motor-encoder.txt | RIGHT SUMMARY 4 passed, 0 failed |
| 2026-10-26 | S1-U12 | unrecorded | User | Live counts observed | logs/2026-10-26-motor-encoder.txt | LEFT stayed -6341 while RIGHT reached 5354; initial RIGHT=2 remains unexplained |
| 2026-10-26 | S1-U13 | unrecorded | User | Three trials recorded; provisional | Calibration table above | Counts/rev repeatable; physical accuracy still needs distance verification |

## Earlier subsystem2 sample-demo record (retired)

### Original log

**Subsystem:** Producer (example)  ·  **Student ID:** <student ID>  ·  **Code:** `app_program/subsystem2/`

See `testing/README.md` for how to run tests and record results.

## Test cases

Define a test here before running it. Test IDs never change.

| ID | What is tested | Requirement / interface | Method | Steps / how | Expected result |
|---|---|---|---|---|---|
| S2-U01 | sample values stay within 0..100 | | auto | `subsystem2_test.c` | `[TEST] S2-U01 PASS` |
| S2-U02 | free slots and free blocks within pool size | IF-02 | auto | `subsystem2_test.c` | `[TEST] S2-U02 PASS` |

*Method:* **auto** = `TEST_CHECK` in code, result printed on the console;
**manual** = a person observes the result (LED, scope, meter, phone app…).

## Results

Add a new row for every run. Never edit or delete old rows.

| Date | Test ID | Commit | Tester | Result | Log / evidence | Notes |
|---|---|---|---|---|---|---|
| YYYY-MM-DD | S2-U01 | `abc1234` | | PASS / FAIL | `logs/YYYY-MM-DD_….txt` | |
