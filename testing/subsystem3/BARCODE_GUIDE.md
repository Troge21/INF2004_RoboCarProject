# Left IR barcode bench build

Current firmware uses left GP26 for barcode capture, centre GP6 and right GP16
for line tracking. Centre/right bits are 2/1; barcode input never contributes.
Normal builds drive after a 3-second startup delay. Both-black drives forward,
centre-only steers left, and right-only steers right. Both-white searches toward
the last detected side for up to 200 ms, then stops until reacquisition. With
no side history it stops immediately. Stale/invalid data stops and clears history.
Mount the tracking pair so both detect black when aligned on the line.

Barcode scanning remains available even without a label; characters do not
issue motor commands. For stationary barcode trials, use TEST=1 for synthetic
tests or set APP_LINE_FOLLOW_ENABLED=0 for normal firmware with barcode reporting.
In TEST=1 the barcode report task is disabled, although capture still runs.

The input assumption is the existing digital reflectance module: 1 means
black, 0 means white. If a different module is installed, confirm its output
type and polarity before using this driver. An analog-only sensor or a serial
scanner needs a different acquisition interface.

## What is implemented

- Standard Code 39, exactly one data character between start/stop markers.
  All 43 standard data characters are supported, including A and Z.
- Forward and reverse scans, width-based classification and ratio checks.
- Validation of start/stop, both inter-character gaps and leading/trailing
  white quiet zones. A label is reported once, after trailing white.
- Timeouts and resynchronisation after missed samples or edge queue overflow.
- Nonblocking, one-byte decoded-result channel to a diagnostic consumer.
- The centre/right line snapshot never includes the barcode sensor.

Multi-character labels, optional check digits and Full ASCII expansion are
not supported. An arbitrary long barcode must not be treated as a navigation
command. Code 39 provides no mandatory checksum, so successful structural
validation is not proof that noisy hardware can never substitute a character.

## Acquisition and resources

The existing kernel tick is 10 ms; sleeping a task for 1 ms would not establish
1 ms sampling. S3 instead reserves RP2040 timer alarm 2 / IRQ2 on physical
core 0, nominally sampling GP26 every 500 microseconds. The existing ultrasonic
GPIO IRQ13 handler is untouched. No SDK alarm pool is used. The repository
contains no prior timer-alarm owner; future code must respect this reservation.

The ISR records transitions in a fixed ring with 64 slots (63 usable). It does
no decoding, printing, waiting or kernel-object calls. S3 drains up to 64
entries per task iteration. Both sides run on core 0, with IRQ2 disabled only
for short ring accesses. A gap greater than 2000 us or a full ring invalidates
the capture and flushes old edges. This detects observed sampling delays;
it cannot recover a pulse that occurs entirely between samples.

The accepted nominal narrow-element interval is 2-100 ms. Wide elements must
form a separate cluster with a mean ratio of 1.8-4.0 times narrow. Elements
within either cluster must lie within 40% of its mean, and character-to-character
narrow means must differ by no more than 40%. These are prototype tolerances,
not measured sensor guarantees. An uninterrupted element over 600 ms or a
frame over 8 seconds times out. Motion must remain reasonably steady during
a scan; sharp steering or stopping halfway through a barcode can invalidate it.
The 40% consistency tolerance accommodates the recorded reverse A hand scan
from 9 November 2026. Replay passes in software; repeat physical scans to assess
recognition reliability. Wider tolerance also admits more distorted inputs.
The empirical 4.0 ratio ceiling accommodates the recorded reverse F frame
with measured ratios of 3.84 and 3.78. It is a prototype sensor allowance,
not a claim about printed Code 39 specification limits or physical accuracy.

White quiet zones must last at least ten estimated narrow widths at BOTH
ends. Leave generous white margins when printing/cutting the supplied samples;
the PDF pages themselves crop closely to the outside bars. Read across the
bars, with the sensor passing perpendicular to them. A left sensor still
pointing at plain track tape will not decode a side barcode.

Decoded messages use bits 0-5 for a symbol index, bit 6 for reverse direction
and bit 7 for validity. Use `s3_barcode_character(code)` to obtain the character;
never interpret a zero/invalid byte as a turn. The result message buffer has
64 bytes of kernel buffer storage, a one-byte application payload and one
consumer. Full queues drop the new result and increment the diagnostic count.
The kernel has its own per-message overhead; this is not a 64-result queue.

This update does NOT claim project-wide minimum-bit compliance or measured
RAM savings. In particular, capture/frame timing uses 32-bit microseconds and
the frame-width workspace uses 32-bit elements. The team's representation
inventory and before/after RAM/timing evidence remain separate work.

## Try it on the board

1. Confirm GP26 is still the sensor's digital output, with common ground and
   Pico-compatible signal voltage. Use a normal build with APP_LINE_FOLLOW_ENABLED=0 to keep motors stopped
   while retaining barcode reporting.
2. Build the normal firmware using the team's existing SDK/toolchain setup
   (`cd build_make`, then `make -j8`). Network options are unchanged. New C
   files are picked up by the existing wildcard source rule. Rebuild after changing settings so the UF2 matches the intended mode.
3. Open the USB serial monitor. Expect an IR setup line containing
   `barcode=GP26` and `barcode sample=500 us`. A setup error is not a valid scan.
   The timing-log build also prints `trace=BC-TRACE-1` on startup.
4. Move the A label steadily across the left sensor, including white space
   before and after the bars. Expected decoded output:
   `[BARCODE] 'A' scan=forward (character only; no motor action)`.
5. Repeat in the other direction, then use both A sizes and both Z sizes.
   A reverse scan should still report the same character with `scan=reverse`.
6. Try partial labels, stopping on black, insufficient white margins, and a
   very fast pass. These must not result in a navigation action. Diagnostic
   `rejected`/`lost` counters help distinguish decode and acquisition failures.
   A new rejection prints three lines: `[BARCODE] REJECT ...`, `[BARCODE]
   timing ...`, and `[BARCODE] runs_us=...`. Copy all three from one pass.
   `INVALID_FRAME` is a complete frame that failed validation;
   `TRAILING_WHITE_INTERRUPTED` is a candidate interrupted by new black before
   enough trailing white. `RUN_TIMEOUT` and `FRAME_TIMEOUT` distinguish a
   section over 600 ms from an entire frame over 8 seconds. For timeouts,
   `runs_us` contains only completed sections; `timing run_us` is the unfinished
   section. `CAPTURE_LOST` means the capture driver discarded unreliable data.
   These logs preserve the existing decoder limits and state layout.
7. Record sensor height, lighting, printed barcode dimensions, direction,
   scan speed, expected/actual character and counter changes. Run at least
   ten trials per sample/direction before claiming recognition accuracy.

In a `TEST=1` build the on-target tests exercise synthetic decoder inputs;
they do not certify optics, placement, CPU load or actual moving scans.

## Software checks

Run `python testing/subsystem3/test_barcode.py` with a host C compiler installed.
Alternatively pass `--compiler /path/to/zig` or `--compiler /path/to/gcc`.
Only the Python standard library is needed to run the saved fixtures. The tests
compile and call the actual C decoder and exercise the actual capture driver
against a register/IRQ model. Additional tests run the actual S3 task with
mock GPIO/kernel calls for all four line patterns at both barcode input
levels, with line following enabled and disabled. Fixture provenance is recorded in the JSON file.
Host tests cannot establish real interrupt latency or physical recognition.

Before driving, verify centre/right polarity, placement and steering with wheels
raised, then test the 200 ms recovery on the track. Barcode-to-command mapping
and navigation integration remain unimplemented. The A-D mapping in the project
brief has deliberately not been assumed here.

References: [Code 39 structure](https://barcodeguide.seagullscientific.com/content/Symbologies/Code_39_Regular.htm),
[RP2040 timer registers](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2040/hardware_regs/include/hardware/regs/timer.h).
