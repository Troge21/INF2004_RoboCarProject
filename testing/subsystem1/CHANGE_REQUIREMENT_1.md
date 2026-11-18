# Change Requirement 1 evidence for Subsystem 1

**18 November 2026. S1 evidence complete.** This covers the Buddy 1
communication, telemetry and supervisory-command implementation. The team's
submission still needs the application-data audit for the other subsystems.

The professor's *Change Requirement 1.docx*, **Minimum-Bit Data
Representation**, asks for field ranges and bit widths, before/after RAM and
message sizes, and correctness and performance evidence. It also requires
safe sharing of packed data across tasks or interrupts. This report supplies
those items for S1. Detailed development history and earlier schemas are
preserved in the [stage archive](CHANGE_REQUIREMENT_1_HISTORY.md).

The current normal image uses Packed v4 with STATUS, STOP and Resume. The
721920-byte local UF2 has SHA-256
`2c5a4652c7c434fb1d27743a3579b6a9ff0dcf2f2f9a4ee79a813840ecd1bcbe`.
The UF2, both linked ELF hashes and all 102 public source hashes from the Windows working tree
matched the [build fingerprint](logs/2026-11-17-resume-fingerprint.json).
The [evidence JSON](logs/2026-11-18-cr1-s1-evidence.json) indexes the size
measurements, rerun checks and recalculated bench results.

## 1. Data representation

The main frame starts with the one-byte marker `b4`, then the 32-bit sequence,
32-bit Pico operating time in milliseconds and nonzero 32-bit MQTT session.
The following 264 bits form a 33-byte body, making **46 bytes in total**.
The table lists the body offsets from its first bit. The bit counts follow
the implemented field ranges, including unavailable codes where used.
Multibyte integers are little endian, with explicit masks and shifts.

| Body bit offset | Field | Valid information and encoding | Bits |
| ---: | --- | --- | ---: |
| 0 | Front distance | 20..4000 mm, 0 unavailable | 12 |
| 12 | Line snapshot | Left GP27 and right GP26, four patterns plus unavailable. Codes 4..7 valid, 0 unavailable | 3 |
| 15 | IMU pitch | -900..900 tenths of a degree encoded +901, 0 unavailable | 11 |
| 26, 33 | Last and maximum hump height | Each 6..80 mm, 0 unavailable. Last cannot exceed maximum | 7 each |
| 40 | IMU state | 0..6 owner states, 7 unavailable | 3 |
| 43 | IMU flags | Sensor OK, hump active, new event, max updated, supplied speed, sensor fault. Pitch validity derives from its code | 6 |
| 49, 63 | Left and right wheel velocity | Each -5000..5000 in 0.1 mm/s encoded +5000, 16383 unavailable | 14 each |
| 77, 109 | Left and right encoder count | Each -2147483647..2147483647 counts, INT32_MIN unavailable | 32 each |
| 141 | Signed net travel | -2147483647..2147483647 in 0.1 mm, INT32_MIN unavailable | 32 |
| 173 | Cumulative encoder path | 0..4294967294 mm, UINT32_MAX unavailable | 32 |
| 205, 216 | Known and active fault masks | One known bit and one active bit per fault, preserving unknown, clear and active. Active is a subset of known | 11 each |
| 227 | Barcode | 43 Code 39 characters and reverse flag. Index+1 in six bits, direction in one, 0 unavailable | 7 |
| 234 | Barcode event sequence | Full uint16 wrap. Ignored and encoded 0 when no barcode is available | 16 |
| 250 | Controller state, STOP latch and TEST mode | State 0..13 uses four bits, with 0 unavailable. Latch and TEST use one bit each | 6 |
| 256 | Motor mode | STOP, duty, RPM or unavailable | 2 |
| 258 | Motor stop reason | Codes 0..5, 7 unavailable | 3 |
| 261 | Barcode navigation | 0 unmapped, 1 left, 2 right, 3 straight, 4 U-turn | 3 |

Fault bits 0..5 cover encoder input, motor timing, left/right stall and
left/right encoder direction. Bits 6 and 7 cover IMU and controller faults.
Bits 8..10 reserve ultrasonic, barcode input and scan faults for the external
aggregate. Barcode and external faults remain unavailable until their producer
getters report data. Average wheel velocity and distance validity are derived
in the dashboard, with zero additional transmitted bits.

| Record | Range, required field widths and storage |
| --- | --- |
| Heartbeat `80` | Full uint32 wrapping counter, 32 bits plus marker, 5 bytes total |
| Scan `b5` | Session, scan ID and source operating time are 32 bits each. Profile total 1..181 and start 0..180 use 8 bits each. Chunk count 1..16 uses a byte containing a direct 5-bit count. Its upper three bits are zero. A biased count could use four bits, but the existing byte header stores the direct count. Each point uses angle 0..180 degrees (8 bits) and range/invalid code (12 bits). 1..16 points per chunk, 19..56 bytes. Three-point S5 profile is 24 bytes |
| Diagnostics `b6` | 32-bit session and uptime. Six 19-bit byte counts for static RAM, free heap, largest free block, sampled minimum free heap, reserved application task stacks and measured stack usage if available. Range 0..270336, 0x7ffff unavailable. Four 32-bit wrapping counters for publish errors, rejected commands, MQTT connections and snapshot contention. 40 bytes including marker and six zero bits after the size fields |
| STATUS `a0` / `a1` | Nonzero request ID is 32 bits. Reply includes ID, boot time and session (32 each), age 0..2000 ms or 2047 unknown (11), and controller word (6) with seven reserved zero bits. Age identifies whether the snapshot is attached. Request 5 bytes, reply 16 or 62 bytes |
| STOP `a2` / `a4` | Request ID, session and boot deadline are 32 bits each. Reply combines controller word (6) and one of six results (3) in two bytes with seven reserved zero bits. Request 13 bytes, result 11 bytes |
| Resume arm `a5` / `a6` | Arm request uses the 13-byte ID/session/deadline shape. Authorization is the STATUS header plus a 32-bit nonzero one-use ticket and optional frame, 20 or 66 bytes |
| Resume commit `a7` / result `a8` | Commit replaces the request ID with the 32-bit ticket, 13 bytes. Result uses the STOP reply shape with eight result codes (3 bits), 11 bytes. Arm refusal echoes the arm ID |

The 16- and 32-bit counter/time fields preserve their specified wrap ranges.
Command leases use the same full-width Pico operating time as STATUS. Marker
bytes, reserved bits and final byte rounding are protocol framing. See the
[controller contract](CONTROLLER_COMMANDS.md) for state and result codes.

### Shared state and ownership

| Record | Packed representation and measured/requested storage |
| --- | --- |
| Main S1 snapshot | 264-bit body + 9 source timestamps x 32 bits + 32-bit sequence = **73 B**. Separate 32-bit publication time makes **77 B** on the shared side |
| Completed scan cache | 181 points x 20 bits rounded to 453 B, plus ID/time (32 bits each) and point count 0..181 (8 bits) = **462 B** |
| Resource cache | Six 19-bit values rounded to 15 B, with 32-bit publication time = **19 B**. Collector sample minimum and requested stack count each use 19 bits in a 3 B array |
| Controller publication | 32-bit boot time and 6-bit state/latch/TEST in one byte = **5 B** |
| Controller request and completion | Three 32-bit request fields plus action (1) and result (3) in one byte = **13 B**. Completion uses ID/session (32 each) plus control (6), result (3) and private command kind (1) in two bytes = **10 B**. Zero ID marks an empty slot |
| Resume authorization and owner word | Ticket/expiry (32 each) = **8 B**, plus **4 B** for the wrapping ticket counter. One owner byte packs control (6), in-progress (1) and failed (1) |
| MQTT receive and transmit | One **13 B** request slot, plus one state byte with remaining length 0..13/offline 15 (4) and collecting, pending, subscribed, connecting flags (1 each). Maximum transmit scratch buffer is **66 B**. Radio counters/timestamps are private full-width values |

The collector gathers nonblocking copies from the owning subsystems. It
updates the shared main/scan/resource publications under the S1 semaphore.
The radio takes a protected copy before encoding and applies source-specific
freshness limits. The controller protects its publication, request and reply
slots with its existing semaphore. The controller owns wheel requests and S2
owns PWM. The radio alone touches MQTT state. S1 consumes copied snapshots
without reading interrupt-owned records directly or consuming a producer
queue. Wider temporary API views serve the producer interfaces and validation.
The 736-byte scan workspace is included in the current linked build and is
owned by the collector. There are **zero S1 input/output message buffers**.

## 2. Measured RAM and message comparison

The original matched comparison isolates the JSON-to-v2 representation
change, using the same stationary flags (`NET=lwip MQTT=1 TEST=1 SMP=1
CONSOLE=usb_cdc`) and equivalent distance/IR data. The payload example is
sequence 12, 451 mm, left black, right background. Binary bytes are
`0c 00 00 00 c3 e1`. Only application bytes are counted.

| Item | Before, JSON/native | After, Packed v2 | Change |
| --- | ---: | ---: | ---: |
| Example telemetry payload | 69 B | 6 B | -63 B (91.3%) |
| Measurement object | 12 B | 6 B | -6 B |
| Line snapshot | 8 B | 5 B | -3 B |
| Input queue record | 16 B | 10 B | -6 B |
| Output queue record | 24 B | 14 B | -10 B |
| Input buffer, four slots including kernel padding | 80 B | 64 B | -16 B |
| Output buffer, four slots including kernel padding | 112 B | 80 B | -32 B |
| Total requested queue RAM | 192 B | 144 B | -48 B (25%) |
| Linker `.data` + `.bss` | 82740 B | 82740 B | 0 B |
| Linked text/read-only image | 346052 B | 346532 B | +480 B |

The queue calculation includes a 4 B length header and 4 B alignment per
record. The kernel heap supplies those buffers. Their allocator overhead and
whole-device peak heap/stack use were outside this measurement. The old text
heartbeat occupied 4..13 B depending on the counter. The binary five-byte
heartbeat preserves the full counter with fixed size. Sources are the
[before layout](logs/2026-11-06-cr1-layout-before.json),
[after layout](logs/2026-11-06-cr1-layout-after.json) and
[matched linker outputs](logs/2026-11-06-cr1-link-size-comparison.txt).

| Later build footprint | Linked text | Data | BSS | Linked static RAM | S1 queue storage |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pre-Resume v4 normal | 358148 B | 108 B | 84016 B | 84124 B | 0 B |
| Current v4 normal | 360680 B | 108 B | 84040 B | **84148 B** | **0 B** |
| Current v4 stationary | 367660 B | 108 B | 84776 B | **84884 B** | **0 B** |

Resume adds 24 B linked static RAM in the normal profile and uses the
existing tasks, semaphore and bounded slots. V4 adds sensor fields, session
and timing metadata, scans, diagnostics and commands. Its 46 B packet and
linked RAM are the expanded implementation's footprint. The matched v2
comparison above quantifies the original savings. A previous normal capture
sampled 148744 B free heap/minimum free and largest block. Those sampled
values describe that run. Peak runtime RAM and measured stack high-water
remain unavailable. See the [current layout measurement](logs/s1-batch-2026-11-18T12-51-47/current-layout.txt),
[build fingerprint](logs/2026-11-17-resume-fingerprint.json) and
[normal sample](logs/2026-11-17-normal-stop-summary.json).

## 3. Correctness and performance

- The matched v2 implementation passed 24 S1 parser/codec checks, nine queue
  checks, 27 dashboard checks and 19917 C-to-JavaScript vectors. The live
  dashboard displayed working line data and six-byte/five-byte binary
  messages. [Matched host results](logs/2026-11-06-cr1-host-final-tests.txt),
  [wire vectors](logs/2026-11-06-cr1-c-to-js-tests.txt),
  [bench observation](logs/2026-11-06-cr1-user-bench-observation.md).
- The 18 November rerun passed 11 focused groups against the verified v4
  source. These include 20099 C/JavaScript/MQTT vectors, 58 dashboard tests,
  73 MQTT port checks, 40 normal and 19 stationary controller checks, and 27
  controller/PID checks. Coverage includes range endpoints, unavailable and
  stale values, wrapping counters, binary byte preservation, malformed
  packets, command expiry/replay and STOP priority. The full previous batch,
  target builds and linked producer tests are in the fingerprint.
  [Rerun results](logs/s1-batch-2026-11-18T12-51-47/summary.json).
- The ARM static call-chain checks recorded 464/2048 B for the collector,
  552/8192 B for the radio Resume path and 312/1024 B for the S1 startup-test
  chain. The startup regression and correction remain in the test history.
  [ARM stack check](logs/2026-11-17-resume-s1_stack_frames.txt).
- On a preceding normal v4 image, the user observed both wheels stop and the
  latch persist through IR/scan changes and dashboard reconnect. A matching
  11 B STOP result and 84 subsequent stationary-feedback samples were
  captured. The user subsequently reported successful Resume on the intended
  current normal image. That report supplies functional evidence without a
  fresh packet capture or independent flash readback. [STOP observation](logs/2026-11-17-normal-user-bench-observation.md),
  [Resume observation](logs/2026-11-18-resume-user-bench-observation.md).

| Recorded v4 run | Main / heartbeats | Median main receive gap | Maximum main / heartbeat receive gap | Evidence |
| --- | ---: | ---: | ---: | --- |
| Stationary `62e5bd63`, 60 s | 149 / 15 | 402 ms | 600 / 4057 ms | [Stationary summary](logs/2026-11-17-delivery-final-summary.json) |
| Normal `9c0dbfd5`, 45 s | 111 / 12 | 400 ms | 688 / 4225 ms | [Normal baseline](logs/2026-11-17-normal-profile-live-summary.json) |
| Normal `9c0dbfd5`, 180 s | 373 / 42 | 400 ms | 5499 / 9304 ms | [Normal STOP run](logs/2026-11-17-normal-stop-summary.json) |
| Latched normal `9c0dbfd5`, 60 s | 106 / 13 | 399 ms | 4568 / 6697 ms | [Latched run](logs/2026-11-17-normal-latch-persistence-summary.json) |

The raw packet lengths and gap figures were recalculated on 18 November.
The 60-second stationary run carried real measurements, six 24 B scan
profiles and a 41 ms STATUS round trip without sequence loss or publish
errors. In the longer normal run the largest receive gap spanned adjacent
sequence numbers and a 400 ms increment in Pico frame time. Other intervals
between Pico frame timestamps reached 5140 ms. These observations show
intermittent delivery delays and varying frame publication intervals. Root cause and the isolated cost of bit packing remain unmeasured. The 2 s main
and 5 s heartbeat targets from the earlier S1 test plan were exceeded in
longer normal runs. An earlier image met the 15 s hotspot recovery goal in
5/5 trials, with 7.922..9.242 s bounds.
[Raw capture review](logs/2026-11-18-cr1-s1-evidence.json),
[recovery evidence](logs/2026-11-11-hotspot-confirmed-recovery-summary.json).

The recorded runs demonstrate working packed transport. A matched target CPU
benchmark, peak runtime RAM and final-image outage results remain unmeasured.
The observed timing misses carry over to the group's integrated validation.
See [final validation](FINAL_ACCEPTANCE.md).

## Handoff

S1's implementation and evidence are closed for the available producer
interfaces. The group still needs its own range, RAM, correctness and timing
evidence for motor/encoder and ISR data (S2), line/barcode acquisition (S3),
IMU and height estimation (S4), and range/servo/scan state (S5). Those owners
can use the S1 evidence structure and publish via the documented passive
interfaces. Include integrated timing, recovery, resource and demo results
when the team freezes its final image. [Producer handoff](PRODUCER_HANDOFF.md).

To reproduce the S1 software evidence from the repo root, run
`python testing/subsystem1/run_batch_checks.py`. Use the linked ARM build
logs and exact image fingerprint for target size evidence. The firmware
images are local artifacts that include bench network settings. Submit the
public source, Markdown report and evidence logs.
