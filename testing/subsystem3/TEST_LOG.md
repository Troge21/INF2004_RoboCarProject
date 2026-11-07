# Subsystem 3 — test log

## Current centre/right tracking setup - 3 November 2026

Left GP26 is barcode only. Centre GP6 and right GP16 contribute bits 2/1 to
the line pattern, sampled every 20 ms. Barcode capture samples every 500 us.
Normal builds enable line following after 3 seconds with 200 ms recovery;
TEST builds disable automatic driving. Historical three-line-sensor patterns
below do not describe this current interface.

Manual integration checks (not yet run):

| ID | Method | Expected result |
|---|---|---|
| S3-U11 | With driving disabled, present C/R=00,01,10,11; repeat at both left-sensor levels. | Pattern=0,1,2,3 respectively; left coverage never changes line pattern. |
| S3-U12 | With wheels raised, present centre-only/right-only, then both-white; repeat with both-black in between. | Turn toward last known side for up to 200 ms, then stop until reacquisition. No side history means immediate stop. |

Physical trials and formal results remain pending. Record the tested revision
and actual evidence in the Results table after completing each test.

## Earlier barcode software update - 3 November 2026

The sections below retain the original three-line-sensor setup and past test
IDs as history. GP26 is now the barcode input; only GP6/GP16 contribute to line
snapshots. See `BARCODE_GUIDE.md` for the current bench setup. Automatic driving
is disabled pending the remaining sensors' physical arrangement.

New on-target **synthetic software** checks: S3-U06 forward A, S3-U07 reverse A,
S3-U08 short leading quiet rejection, S3-U09 malformed start rejection,
S3-U10 invalid packed-result rejection. Host checks are in `test_barcode.py`.
Neither set constitutes a completed hardware recognition trial. The original
20/100 ms sampling notes below do not describe the new 500 us barcode sampler.

**Subsystem:** IR sensing, line-pattern detection and barcode decoding  ·  2503451  ·   Troy Ang Songze (Troge21)  ·  `app_program/subsystem3/`

See `testing/README.md` for how to run tests and record results.

## Test setup

- Left sensor DO: GP26; centre sensor DO: GP6; right sensor DO: GP16.
- Confirmed polarity: logic 1 means black; logic 0 means no black detected.
- IR task samples and prints approximately every 100 ms. This is a stationary/slow manual test, not a validated barcode sampling rate.
- Mount sensors at the normal operating height and angle. Keep motors off for manual movement tests.
- Manual IR checks run in the normal firmware; `TEST=1` is needed for the automatic checks in `subsystem3_test.c`.
- Record the mounting height, lighting and surfaces used in each run's notes where practical.

## Test cases

Define a test here before running it. Test IDs never change.

| ID | What is tested | Requirement / interface | Method | Steps / how | Expected result |
|---|---|---|---|---|---|
| S3-U01 | statistics self-consistent (min ≤ max, 0..100) | | auto | `subsystem3_test.c` | `[TEST] S3-U01 PASS` |
| S3-U02 | IR polarity and pin mapping | Digital IR sensing | manual | At normal mounting height, place a light surface beneath all sensors. Move black tape beneath left, centre and right separately; return each to the light surface. | Light gives 0 and black gives 1 for each sensor; only the corresponding L/C/R field changes. |
| S3-U03 | All eight sensor-pattern labels | Line-pattern detection | manual | Present each L/C/R combination in the pattern table below, holding each still long enough to read the console. | L/C/R values and printed pattern match every row in the table. |
| S3-U04 | Stationary reading stability | Digital IR sensing | manual | Hold a light surface beneath all three sensors for 10 seconds, then black beneath all three for 10 seconds, at normal mounting height. | Values remain 000 / NO_BLACK on the light surface and 111 / ALL_BLACK on black, without flickering. |
| S3-U05 | Pattern changes during slow movement | Line sensing | manual | With motors off, slowly slide the car sideways across a black line. Pause at each sensor position and compare the physical coverage with the console. | Pattern follows which sensors are over black, in either direction; there is no claim that the 100 ms period captures fast transitions. |

*Method:* **auto** = `TEST_CHECK` in code, result printed on the console;
**manual** = a person observes the physical setup and console readings.

S3-U01 remains the original consumer-example test; it does not test the IR sensors. S3-U02 through S3-U05 are manual checks and do not print automatic `[TEST]` PASS lines.

## Expected patterns

A 1 means that the corresponding sensor detects black.

| Left | Centre | Right | Expected pattern |
|---|---|---|---|
| 0 | 0 | 0 | `NO_BLACK` |
| 0 | 0 | 1 | `RIGHT_ONLY` |
| 0 | 1 | 0 | `CENTRE_ONLY` |
| 0 | 1 | 1 | `CENTRE_RIGHT` |
| 1 | 0 | 0 | `LEFT_ONLY` |
| 1 | 0 | 1 | `LEFT_RIGHT` |
| 1 | 1 | 0 | `LEFT_CENTRE` |
| 1 | 1 | 1 | `ALL_BLACK` |

These labels describe sensor coverage only. `ALL_BLACK` alone does not establish a junction or a valid barcode.

- logic 1 detects black and that black detection works as intended.
- Pattern output works correctly. Individual coverage of all eight combinations has not been recorded; rerun S3-U03 using the table above.
- Tilting the car upwards made black detection less reliable. Check mounting height and angle when repeating the tests.
- The timed stability test (S3-U04) and slow movement test (S3-U05) have not been reported as completed.

## Results

No formal runs are recorded yet. The original template placeholder was not a test result.

Commit the firmware changes before the next recorded run, build and flash that revision, then save actual console output or other evidence in the existing `logs/` folder. Add one row per completed run with its actual date, tested commit, tester, PASS/FAIL result, evidence path and observations. Do not mark a case PASS until all its steps are completed. Preserve every real result row, including failures; add a new row for a rerun.

| Date | Test ID | Commit | Tester | Result | Log / evidence | Notes |
|---|---|---|---|---|---|---|

## Development check - line-reading extraction, 8 November 2026

Local changes based on `0c2ff82`: line GPIO setup, sampling, semaphore and
`subsystem3_get_line()` implementation moved into `line_detection.c/.h`.
The existing S3 task still configures GP26, processes barcode edges, calls
the line sampler, prints diagnostics and requests its 20 ms delay. Public
interfaces, sensor pins/polarity, task priority/core, controller recovery and
barcode capture/decoding settings are unchanged.

`python testing/subsystem3/test_barcode.py`: all 12 host test groups passed,
including the actual S3 task and snapshot API across all four centre/right
patterns, both barcode levels, and line following enabled/disabled. The
host harness now includes both source modules. Normal firmware compilation,
linking and UF2 generation succeeded, including the new line module. The
Windows-CRLF-aware whitespace check passed. The usual image is
`build_make/mtk3pico_smp1_usb_cdc_wifi.uf2`; no extra UF2 copy was created.
No firmware was flashed or physical sensor/driving test performed.
