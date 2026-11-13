# S1 completion batch — 13 November 2026

**Current follow-up:** [Sensor integration](SENSOR_INTEGRATION.md) now connects
S5's strong scan getter and controller-owned avoidance, with the same v4
format. Its image/build/resource figures supersede the original batch below.
The corrected startup stack and prior failure records are retained.


**13 November regression resolved:** the new TEST=1 startup tests exceeded
the 1024-byte initial-task stack (known chain 1040 bytes). Their large fixtures
now use static TEST-only storage; known chain is 312 bytes. Collector scratch
usage is also reduced and heap reads use SMP exclusion. After the user flashed
390c5893, live readings returned: 86 valid distance/pitch samples, IR codes
4/5/6/7, nine heartbeats, seven diagnostics and matching STATUS in 41 ms,
with zero decode errors. The user confirms the dashboard updates again.
S2/S3/S4 source hashes match the preceding working image. Current stationary
UF2: 719360 bytes, static RAM 84668. Earlier failed attempts remain preserved.
Final physical accuracy/STOP/recovery/cadence acceptance is still pending.


User direction: finish the S1 implementation together, prepare missing
producer interfaces, then test the complete batch at the end. Implementation,
consolidated offline checks and both initial ARM builds are complete. The initial implementation stage was offline; the later user flash exposed a
startup-stack regression, now fixed with live stationary readings restored.

The single current image is the stationary TEST=1 integrated UF2:
719360 bytes, SHA-256
390c589344927280ab77175a1cee2d04a07beacad2d522d63760146ad30f6f55.
Its static RAM is 84668 bytes. The normal check image linked with 83356
bytes static RAM; its hash is retained in the fingerprint before removing
the extra UF2. Earlier v3 physical STOP/recovery passes remain historical.

## Requirements and scope

Re-read the professor's supplied Project Write-up.pdf (pages 1–5, 10–11)
using a temporary hash-verified pypdf wheel, not a project dependency.
Page 5 lists WiFi, MQTT/topics, telemetry structures/publication, command
subscription, recovery and heartbeat/status. Its eight telemetry fields
are examples; their complete S1-side transport is prepared in this batch.
Navigation remains autonomous (page 1). STATUS/STOP remain supervisory;
no remote steering/START/RESUME or S4 curb algorithm is introduced.

## Implemented and checked offline

- One Packed v4 binary schema; old live decoders/beacons removed.
- Independent 40 ms producer collection and 400 ms telemetry publication.
  S5 exposes a protected passive range snapshot; no distance-triggered
  S1 input/output queues remain.
- Real S2 wheel velocity, counts, net travel, motor mode/reason and faults.
- Existing protected S3 line and S4 IMU/retained hump sources.
- Nonblocking optional weak getters in app_program/telemetry_sources.h:
  barcode (S3), actual path distance (S2), scan (S5), external fault aggregate
  (controller), resources (platform). Strong owner getters activate the
  existing transport/display automatically. No result-queue consumption.
- Bounded 181-point scan cache, 16-point chunks, backpressure retry.
- Per-source freshness, session and boot-clock metadata; browser STATUS
  clock sync and delayed-frame rejection.
- Controller vocabulary widened to 14 values, still actual owner publication.
  Current steering algorithm is unchanged. STOP latch flags/replies use
  the matching new bit positions.
- Read-only Kmeminfo/free-largest-block introspection. Sampled heap minimum
  and task stack capacities are distinct from true stack high-water,
  which stays unavailable without an instrumented producer.
- Dashboard motion/resources/scan views, telemetry/scan CSV and evidence JSON.
- Read-only observers updated for typed telemetry and current framing.
- Heartbeat 4000 ms (timing margin); retry/keepalive/connection bounds retained.

## Wire map

Main packet 46 bytes: b4, sequence32 LE, generation boot ms32, session32,
then 33 bytes of packed fields. Bit offsets below are within that body.

| Offset | Bits | Field / unavailable |
| ---: | ---: | --- |
| 0 | 12 | ultrasonic mm, 20..4000, 0 unavailable |
| 12 | 3 | line bits, 0 or 4..7 |
| 15 | 34 | pitch11 / height7 / max7 / IMU state3 / flags6 |
| 49 / 63 | 14 each | wheel speed 0.1 mm/s, -5000..5000 encoded +5000; 16383 unavailable |
| 77 / 109 | 32 each | signed encoder counts; INT32_MIN unavailable |
| 141 | 32 | signed net travel 0.1 mm; INT32_MIN unavailable |
| 173 | 32 | actual nonnegative path mm; UINT32_MAX unavailable |
| 205 / 216 | 11 each | known / active faults; active subset of known |
| 227 | 7 | Code39 index+1 / reversed; 0 no decoded barcode |
| 234 | 16 | producer barcode event counter |
| 250 | 6 | controller state4/latch1/TEST1 |
| 256 / 258 | 2 / 3 | motor mode / stop reason; 3 / 7 unavailable |
| 261 | 3 | barcode navigation: 0 unknown, 1 left, 2 right, 3 straight, 4 U-turn |

Scan b5: session32, scan ID32, source boot ms32, total8/start8/count8,
then count ×20 bits (angle8/distance12), at most 16 points / 56 bytes.
Resources b6: session32, boot32, six 19-bit byte values (unknown 0x7ffff),
then four uint32 counters; 40 bytes. Same telemetry topic, no extra subscription.
Heartbeat remains 80 + counter32 (5 bytes).
STATUS a0 + ID32 (5); a1 + ID32 + boot32 + age11/control6/reserved7 +
session32 + optional main frame (16 or 62).
STOP a2 + ID32 + session32 + deadline32 (13); a4 + ID32 + session32 +
control6/result3/reserved7 (11).

Source ages: IR/pitch 100 ms, motion/speed 500, IMU status/heights 1000,
range/travel/barcode/external faults 2000, completed scan 10000.
Code39-to-navigation mapping is supplied by the owner, not guessed by S1.
S2 net travel is not presented as total travelled path length.

## Consolidated checks and resources

All 15 offline check groups passed across the preserved initial run and
focused correction runs. See [the consolidated results](logs/2026-11-13-s1-batch-summary.json)
and [source/build fingerprint](logs/2026-11-13-s1-batch-fingerprint.json).
The initial failures and build attempts remain in the logs.

- 42 dashboard tests; 51 actual MQTT-port checks.
- 23 portable codec checks plus 25 actual collector/cache checks.
- 17 controller checks in each mode; 13 actual normal controller/S2 checks.
- Eight recorder tests and 20,089 cross-language C/JavaScript/MQTT vectors.
- Actual weak-default/strong-producer linking, read-only allocator queue
  inspection, shared GPIO dispatch, analog policy and 16 barcode test groups.
- Warning-free normal and stationary ARM links; four actual ARM build modes.
- Corrected static collector frame: 464/2048 bytes (originally 1680).
- Corrected startup-test/console chain: 312/1024 (originally 1040, overflowing).
  New ARM guards cover both. These are not runtime high-water measurements.

Main packed publication is 73 bytes, plus its four-byte publication time.
One completed scan uses 453 bytes packed points plus nine bytes metadata
(462 total). Resource publication is 19 bytes, sampled heap minimum and
requested stack capacities three bytes each. S1 input/output message-buffer
requests are now zero, compared with the previous 192-byte queue requests.
New caches and features have costs; this is not a like-for-like whole-device
RAM saving. Corrected stationary static RAM is 84668 bytes, including
736-byte collector workspace and 736-byte TEST-only fixture storage added
during diagnosis. The initial normal image used 83356 bytes and must be
rebuilt before reuse; it has no TEST fixtures. Heap/stack peaks remain open.

## Remaining acceptance and owner work

Use [one combined final hardware session](FINAL_ACCEPTANCE.md), not repeated
per-feature flashing now. Current-image sensor validity, cadence, recovery,
physical integration and resource observations remain deferred. Expected
on-board S1 summary is 24 checks including the hardware clock check;
24/24 were observed in the corrected startup capture. No commit, push, service interruption or car operation occurred.

Missing barcode/path/scan/external fault producers implement the stable
[passive getter contract](PRODUCER_HANDOFF.md) in their own modules. Their
strong getters activate the existing S1 transport/UI within documented
ranges. S1 does not implement their missing algorithms or fabricate data.
S4 curb/one-wheel logic and calibrated heights remain with Buddy 4.

Do not claim hardware acceptance, source flash readback, runtime high-water,
calibrated heights, or current-image recovery from these offline checks.
