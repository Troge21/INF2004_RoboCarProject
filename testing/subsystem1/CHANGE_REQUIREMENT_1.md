# Change Requirement 1 for WiFi communication and telemetry

Recorded 6 November 2026. Source: the professor-provided **Change Requirement
1.docx**, titled "Minimum-Bit Data Representation". This is an implementation
and evidence record for the current S1 telemetry path, not a declaration that
the whole team requirement or all hardware testing is complete.

## What the professor is asking

Keep the required robot information and behavior, but represent each field
using only the bits needed for its valid range. For example, a value from
1 to 5 needs three bits, not a separate 32-bit integer. Several small flags
can share a storage unit. MQTT application fields should be binary instead
of text. Access to packed data shared by tasks or interrupts must remain safe.

This applies throughout application firmware: motor/encoder data, IR/barcode,
ultrasonic data, hump estimates, controller states, faults, shared snapshots,
queues and MQTT telemetry/commands. It is broader than changing a web page
or compressing a JSON string.

The four required evidence items are a data representation table, RAM usage
comparison, message-size comparison, and correctness/performance evidence.
This record supplies the current S1 table and development measurements.
Physical performance trials and the other owners' data remain open.

## What is implemented in this stage

- The existing distance/IR telemetry is a **six-byte binary frame**.
- S1 stores packed snapshots and queue records, not the previous native
  integer structure or text buffers. There is no packed-structure cast to
  native words: explicit masks/shifts and byte access define the layout.
- Heartbeats use a five-byte binary frame with the full 32-bit counter.
- MQTT publishes explicit binary lengths; zero bytes are preserved. Logs
  report lengths or render hex instead of treating payloads as C strings.
- The dashboard accepts this format in **Auto** or **Packed v2** mode,
  retains legacy JSON support, and displays decoded values or raw hex.
  Unavailable distance is still unavailable, not a numeric zero.
- Sensor GPIOs, acquisition algorithms, steering and command execution are
  unchanged. S5's handoff alone was adapted to call the S1 packing constructor.
  Include that small interface change in the S5 owner's review.

Files: `app_program/subsystem1/s1_protocol.h`, `s1_protocol.c`,
`subsystem1.h`, `subsystem1.c`, the S5 handoff, the existing MQTT service,
and the dashboard decoder/inspector. The MQTT service remains the only
owner of radio/lwIP operations. No new application task or kernel object
was introduced, and kernel internals were not repacked.

## Data representation table

| Field | Valid information | Required logical bits | Implementation |
| --- | --- | ---: | --- |
| Telemetry sequence | 0..4294967295 with modular wrap | 32 | Four explicit little-endian bytes |
| Obstacle distance | 20..4000 mm, plus unavailable | 12 | Codes 20..4000 are measurements; code 0 means unavailable; other codes rejected |
| Separate distance-valid flag | Derived from distance code | 0 additional | Zero is outside the valid physical range, so no separate stored boolean is needed |
| Left line detection | Background / black, when valid | 1 | Bit 1 of the three-bit line field; GP6 (firmware calls it centre) |
| Right line detection | Background / black, when valid | 1 | Bit 0 of the line field; GP16 |
| IR snapshot validity | Unavailable / valid | 1 | Bit 2 of line field; unavailable is canonical 0, valid states are 4..7 |
| Telemetry format marker | Legacy family / current family | 1 | High bit of final word identifies current v2; framing metadata |
| Distance acquisition timestamp | Full 32-bit kernel uptime, milliseconds | 32 | Four local bytes in each queue record; supports wrap-safe freshness checks |
| IR source timestamp | Full 32-bit kernel uptime, milliseconds | 32 | Four local bytes; rechecked at transmission; not sent as laptop time |
| Heartbeat counter | 0..4294967295, with wrap | 32 | Four little-endian bytes |
| Heartbeat format marker | Legacy text / current binary | 1 logical | Marker byte 0x80; its seven padding bits are zero; framing metadata |

Distance has 3981 valid numeric values plus one unavailable state: 3982
states require 12 bits. IR has four valid patterns plus unavailable: five
states require three bits. Full 32-bit sequence/counter/timestamp ranges
preserve the existing wraparound contract rather than silently narrowing it.
Standalone C byte arrays round up to whole bytes; padding is not application
data. Kernel IDs, pointers and register widths are platform types, not fields
to shrink without regard to the RTOS/hardware ABI.

## Telemetry wire layout

| Overall bits | Meaning |
| --- | --- |
| 0..31 | Sequence, little endian |
| 32..43 | Distance code, zero unavailable or 20..4000 mm |
| 44 | Right IR black |
| 45 | Left line IR black (GP6) |
| 46 | IR snapshot valid |
| 47 | Current-format marker, must be 1 |

The last two bytes form a little-endian word:

```c
word = 0x8000 | distance_code | (line_bits << 12);
```

Line codes are 0 (unavailable), 4 (both background), 5 (right black),
6 (left black), and 7 (both black). Codes 1..3 are rejected rather than
interpreted as valid sensor states. When distance is unavailable, its code
is zero even if IR remains valid. When IR ages past 100 ms, its field is
cleared without discarding a still-fresh distance sample.

Golden example: sequence 12, distance 451 mm, left black/right background:

```text
0c 00 00 00 c3 e1
```

The comparable old JSON was:

```json
{"sequence":12,"obstacle_mm":451,"obstacle_valid":true,"line_bits":6}
```

Those are **69 versus 6 application-payload bytes**, a 91.3% reduction for
this example. This excludes MQTT topic/header, TCP and WiFi overhead. The
old proposed distance-only compact v1 decoder remains for compatibility,
but v1 was not this firmware's published baseline. Compare against actual
JSON, not against the earlier proposed six-byte design.

## Heartbeat wire layout

```text
80 cc cc cc cc
```

The first byte is the current binary marker; the next four bytes hold the
counter little endian. A marker avoids ambiguity with old ASCII messages:
four arbitrary counter bytes could otherwise also spell `hb 0`. The marker
uses one logical bit, and the byte-addressed frame has seven zero padding
bits. The dashboard can therefore accept old and new firmware on the same
topics without guessing or truncating the counter range.

Five binary bytes are not smaller than every early text heartbeat:
`hb 0` is 4 bytes, `hb 12` is 5, and `hb 4294967295` is 13. Report that
tradeoff rather than claiming every heartbeat shrank. The binary size is
fixed, unambiguous and contains the full 32-bit application field.

## Synchronization and queue safety

Packed snapshots are built in producer-local storage, validated before
masking could truncate an out-of-range value, and copied through the existing
kernel message buffers. Receivers work on private copies. There is no shared
application read-modify-write on a packed word.

S1 reads S3 through `subsystem3_get_line()`, which uses S3's existing snapshot
semaphore without waiting. S3 retains GPIO ownership. S1 packs the copied
result locally and checks its timestamp both when gathering and serializing.
Distance still expires after two seconds. All sends remain nonblocking;
overflow drops the new message, and the network drains up to four messages
to keep the newest fresh one. The single network task owns the heartbeat
counter, timeout chain and MQTT calls.

Packed queue records are byte arrays: input is 6 frame bytes + 4 timestamp
bytes = **10 bytes**; output adds 4 IR timestamp bytes = **14 bytes**.
ARM static assertions enforce these sizes. The kernel nevertheless charges
a four-byte length header and rounds each message to a four-byte boundary.
Buffer sizing includes that rounding so each queue still holds four records.
The host queue model now uses the same capacity calculation.

## Before and after measurements

Identical firmware flags: `NET=lwip MQTT=1 TEST=1 SMP=1 CONSOLE=usb_cdc`.
The saved baseline includes the earlier clock-startup fix. This is a test-image
comparison, not a production TEST=0 result. See the
[linker output](logs/2026-11-06-cr1-link-size-comparison.txt) and measured
[before](logs/2026-11-06-cr1-layout-before.json) /
[after](logs/2026-11-06-cr1-layout-after.json) C layouts.

| Item | Before | After | Difference |
| --- | ---: | ---: | ---: |
| Linked `.data` | 100 B | 100 B | 0 B |
| Linked `.bss` | 82640 B | 82640 B | 0 B |
| Linked static RAM (`data+bss`) | 82740 B | 82740 B | 0 B |
| Linked text/read-only image | 346052 B | 346532 B | +480 B |
| S1 measurement object | 12 B | 6 B | -6 B |
| S1 local line snapshot | 8 B | 5 B | -3 B |
| Input queue record, before kernel padding | 16 B | 10 B | -6 B |
| Output queue record, before kernel padding | 24 B | 14 B | -10 B |
| Four-slot input message buffer | 80 B | 64 B | -16 B |
| Four-slot output message buffer | 112 B | 80 B | -32 B |
| Total requested S1 message-buffer storage | 192 B | 144 B | **-48 B (25%)** |
| Telemetry transmit byte array | 128 B | 6 B | -122 B |
| Heartbeat transmit byte array | 48 B | 5 B | -43 B |
| Example telemetry MQTT payload | 69 B | 6 B | **-63 B (91.3%)** |

Static RAM did not shrink: queues are allocated at runtime from the kernel
heap, so changing their requested storage does not remove a static global
array from `.bss`. The 48-byte queue reduction includes message padding but
excludes allocator/control-block overhead. The smaller transmit arrays are
local stack arrays, not a measured whole-stack high-water reduction. Do not
add those figures together as a measured whole-device peak RAM saving.
Whole-device heap/stack high-water measurements remain a hardware task.

The first development build omitted per-record kernel rounding in its size
formula. That was caught before flashing and corrected; final queue tests
enforce four slots under the real rounding rule. Use the `*-final-*` evidence,
not the earlier `cr1-host-tests.txt` or `cr1-build-after.txt` interim results.

## Correctness evidence

- **24 S1 parser/codec checks + 9 queue checks passed on the host.** They
  compile actual S1 sources with stub kernel queues/clock and the real codec.
  [Final host log](logs/2026-11-06-cr1-host-final-tests.txt).
- **27 dashboard checks passed**, covering old/new formats, binary zero bytes,
  malformed fields/lengths, retained/replayed packets, freshness, heartbeat
  counter limits and readable packet inspection.
  [Dashboard log](logs/2026-11-06-cr1-dashboard-tests.txt).
- **19914 actual C telemetry frames and 3 C heartbeats passed the JavaScript
  decoder and MQTT byte-preservation checks.** This covers all 3982 distance
  states times all 5 IR states, plus four edge cases. C also rejects all 114
  reserved distance codes. No frames are sent to a live broker by these tests.
  [C-to-JS log](logs/2026-11-06-cr1-c-to-js-tests.txt).
- **ARM WiFi/MQTT TEST firmware built successfully**, with target layout
  assertions. [Final build](logs/2026-11-06-cr1-build-final.txt).

The expected new Pico S1 summary is **25 passed, 0 failed**: the 24 host
checks plus the hardware-only clock check S1-U43. Its result has **not been
captured on the board for this packed image yet**. Existing successful JSON
hardware trials are not automatically passes for the new representation.

The user subsequently reported working `line_bits` and six-byte/five-byte
messages in the real dashboard. This is a partial functional bench result;
see the [user observation and remaining checks](logs/2026-11-06-cr1-user-bench-observation.md).
Distance, controlled per-pattern records, unavailable-state, restart and
performance evidence remain open. The earlier machine-readable evidence
file records the pre-flash development state and is preserved as recorded.

The codec uses fixed four/six-byte loops; queue draining remains bounded at
four records. Host tests do not establish real-time performance. Flash and
repeat physical distance/IR, no-echo, normal boot/power-bank, publish interval,
heartbeat interval and WiFi/broker loss trials before claiming performance
acceptance. Keep the existing team timing targets and measured conditions.

## Reproduce and test later

At repository root on this Windows setup:

```powershell
$testExe = Join-Path $env:TEMP 'robocar-s1-cr1-tests.exe'
& 'C:\msys64\ucrt64\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror -I testing/subsystem1/host/include testing/subsystem1/host/test_telemetry.c -o $testExe
if ($LASTEXITCODE -eq 0) { & $testExe }

$framesExe = Join-Path $env:TEMP 'robocar-s1-emit-frames.exe'
& 'C:\msys64\ucrt64\bin\gcc.exe' -std=c11 -O2 -Wall -Wextra -Werror testing/subsystem1/host/emit_frames.c app_program/subsystem1/s1_protocol.c -o $framesExe
if ($LASTEXITCODE -eq 0) { node testing/subsystem1/host/check_wire.mjs $framesExe }

node --test dashboard/tests/*.test.mjs
```

Linux/WSL can use `gcc` with the same flags and temporary output paths.
Use the normal project firmware build with `NET=lwip MQTT=1 TEST=1`.
The packed image is `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`;
the old JSON image is preserved locally with `_before_cr1` in its filename.
Firmware binaries contain local network settings: do not upload them as
evidence. They remain ignored by Git; keep source fingerprints and reports.

When ready for the hardware check, flash the new UF2, restart normally, and
select **Auto** or **Packed v2** in the dashboard (not Legacy JSON). Verify
six-byte accepted telemetry, five-byte heartbeats, real measurements and
unavailable states. A text terminal subscriber will show binary characters;
use the dashboard's decoded/hex inspector rather than expecting JSON text.

## Remaining project coverage

| Area | Status / next work |
| --- | --- |
| Current S1 telemetry fields, snapshots and queues | Packed; user reports functional IR and six-byte delivery. Distance, detailed hardware and timing trials pending |
| Current MQTT heartbeat application field | Packed; user reports five-byte messages. Counter continuity and interval evidence pending |
| Actual supervisory commands | Still disabled/unintegrated. Existing ASCII STOP/STATUS parser is only a local proposed vocabulary; command representation and controller delivery are not complete. Agree fields, validity, expiry/replay and acknowledgements before implementing binary commands. |
| S2 motion/encoder fields | Owner must audit ranges, packed state and ISR/task synchronization; measured speed interface still needed |
| S3 internal IR/barcode fields | Existing reader reused; internal state/decoder/queues still need owner audit and minimum-bit conversion |
| S4 IMU/hump fields | Not converted here; review live-pitch getter, synchronization, validity and speed dependency before integrating |
| S5 acquisition/servo state | Only its S1 handoff changed; internal pulse/state/scan data need owner audit |
| Vehicle/controller states and shared fault bitmask | Need team-agreed ranges, meanings and synchronized updates |
| Week 13 evidence | This is working-copy development evidence. Commit/review, then rerun required checks and hardware trials with the exact revision. |

Adding later measurements does not mean postponing this rule again: choose
their ranges/bit widths and update the versioned wire contract when adding
them. Completing every teammate's conversion is not a prerequisite for doing
useful S1 integration, but the whole project must meet the requirement for
final submission.
