# S3 software validation

## Current integration checks

**12 November 2026:** integrated S3 owns GP28/GP27/GP26 analog inputs through
the kernel ADC device. GP27 is the left/centre line input, GP26 is right and
GP28 is barcode-reserved. GP16 is now an encoder; the old digital capture
and line GPIO paths are inactive.

The 19 November host suite passes 17 groups. The ADC/task harness checks
GP28 fault isolation, channel selection, discarded conversions, averaging,
threshold boundaries, all four line patterns, invalid reads, stale sampling,
wrap, contention, the eight-byte snapshot and tick-aware delay. The decoder
suite checks gap validation under uneven speed in both scan directions.
Barcode decoder/capture register models remain
software-only checks of the retained decoder; no live AO barcode decoding
or recognition accuracy is claimed. The current fixes passed a stationary
integrated ARM build. See [the current integration evidence](../subsystem1/TEST_LOG.md)
and [the physical procedure](../subsystem1/MOTOR_STOP_TEST.md).

The earlier GP26 barcode / GP6+GP16 line configuration and 200 ms recovery
tests are historical. Their physical observations do not validate the new
analog wiring or thresholds.

## Imported barcode-only validation (historical)

The remaining sections describe the imported single-line bench revision,
including its former pin selection and validation environment.


Source: supplied INF2004_RoboCarProject.zip (local copy; no remote repository changes).
Compiler: Zig 0.16.0 C compiler, Windows x64 host.

## Completed checks

- 13 host test groups PASS, compiling and calling the actual C decoder.
- All 43 standard Code 39 characters, both directions, five narrow-element
  intervals and three wide/narrow ratios: 1,290 valid decoding cases.
- 860 deterministic jittered scans PASS (43 characters, both directions,
  ten variations each).
- Stripe widths measured from all four supplied PDF samples decode as A, A,
  Z, Z, including reversed order and three scan-speed scales.
- Malformed guards/clusters/gaps, invalid result encodings, short quiet zones,
  incomplete scans, stuck inputs, decoder resynchronisation and timer wrap
  checks PASS.
- Actual capture driver with host register/IRQ model PASS: exclusive alarm
  checks, unchanged other-alarm bits, transition timestamps, repeated levels,
  ring overflow/discard/recovery, late sampling, core ownership and timer wrap.
- Changed application files compile to ARM Cortex-M0+ ELF object files with
  the project's REAL micro T-Kernel/BSP headers, warnings treated as errors.
  Profiles checked: SMP=1/APP_TEST=0, SMP=1/APP_TEST=1, SMP=0/APP_TEST=0.

## Two-sensor configuration checks

The actual S3 task and snapshot API were tested with host kernel/GPIO stubs
for S3_LINE_PIN=-1, 6 and 16. With -1, only GP26 is configured and no line GPIO
is read; snapshots return E_TMOUT with valid=FALSE. With 6/16, only the selected
line pin is configured/read, and both black/white levels produce the expected
single-bit status. Unsupported pins and enabling the legacy steering flag
are rejected during compilation. Both configured pins also compile against
the real Pico headers for Cortex-M0+ (SMP normal build).

## Not yet established

No board was connected. No physical barcode trial or real interrupt-latency,
CPU-load, RAM-savings or recognition-accuracy measurement was made. The full
firmware was not linked against the Pico SDK and no UF2 was produced. The
on-target synthetic test functions compiled but were not executed on a board.
The team's existing SDK/Arm GNU build must still be run before flashing.

Default automatic driving is OFF. Only ONE line sensor remains, and its GPIO/placement and
letter-to-command mapping are unconfirmed. S3_LINE_PIN defaults to -1. This is a barcode bench prototype,
not a completed integrated navigation demonstration or a claim of project-wide
minimum-bit compliance. See testing/subsystem3/BARCODE_GUIDE.md.
