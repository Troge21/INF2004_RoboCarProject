# Left IR barcode bench build

This update repurposes the left digital IR input (GP26) for barcode reading.
There are now TWO IR sensors total: one barcode sensor and one line sensor.
The remaining line sensor's GPIO is unconfirmed. `S3_LINE_PIN` defaults to -1
in `app_program/subsystem3/subsystem3.h`: no GP6/GP16 input is configured or
read for line sensing, and line snapshots stay invalid (not a white reading).
After verifying the wiring, set this single setting to 6 or 16. This enables
only the line reading, not automatic driving. Pattern 1 means black and 0
means white when the snapshot is valid; it is not a steering direction.

Automatic line following remains OFF. The old three-sensor controller is
retained for reference and compile-time blocked from consuming this new
single-sensor format. A new tracking strategy must be implemented and tested
before automatic driving can be enabled. Barcode characters do not generate
motor commands.

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
- The optional single-line snapshot never includes the barcode sensor. Unknown wiring is reported as invalid.

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
form a separate cluster with a mean ratio of 1.8-3.4 times narrow. Elements
within either cluster must lie within 25% of its mean, and character-to-character
narrow means must differ by no more than 40%. These are prototype tolerances,
not measured sensor guarantees. An uninterrupted element over 350 ms or a
frame over 8 seconds times out. Motion must remain reasonably steady during
a scan; sharp steering or stopping halfway through a barcode can invalidate it.

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
   Pico-compatible signal voltage. Keep the car stationary and motors stopped.
2. Build the normal firmware using the team's existing SDK/toolchain setup
   (`cd build_make`, then `make -j8`). Network options are unchanged. New C
   files are picked up by the existing wildcard source rule. No UF2 is supplied
   with this update; the full linked firmware still needs building on that setup.
3. Open the USB serial monitor. Expect an IR setup line containing
   `barcode=GP26` and `barcode sample=500 us`. A setup error is not a valid scan.
4. Move the A label steadily across the left sensor, including white space
   before and after the bars. Expected decoded output:
   `[BARCODE] 'A' scan=forward (character only; no motor action)`.
5. Repeat in the other direction, then use both A sizes and both Z sizes.
   A reverse scan should still report the same character with `scan=reverse`.
6. Try partial labels, stopping on black, insufficient white margins, and a
   very fast pass. These must not result in a navigation action. Diagnostic
   `rejected`/`lost` counters help distinguish decode and acquisition failures.
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
mock GPIO/kernel calls for disabled/GP6/GP16 line configurations and verify
configuration guards. Fixture provenance is recorded in the JSON file.
Host tests cannot establish real interrupt latency or physical recognition.

Before driving, confirm the single line sensor's wiring and placement, implement
and validate a suitable single-sensor tracking strategy, agree the barcode-to-command mapping, and connect decoded
commands to the controller's navigation state machine. The A-D mapping in the
project brief is an example and has deliberately not been assumed here.

References: [Code 39 structure](https://barcodeguide.seagullscientific.com/content/Symbologies/Code_39_Regular.htm),
[RP2040 timer registers](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2040/hardware_regs/include/hardware/regs/timer.h).
