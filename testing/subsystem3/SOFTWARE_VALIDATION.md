# Barcode software validation - 3 November 2026

## Current integration checks

The working tree now uses left GP26 for barcode and centre GP6/right GP16 for
tracking with 200 ms recovery. Host barcode tests, controller policy tests and
motor-command tests pass. The task harness checks all four line patterns at
both barcode levels, with line following enabled and disabled. Full normal and
TEST firmware builds pass using the local Pico SDK and Arm GNU compiler.
These checks do not establish physical line tracking or barcode recognition.

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
