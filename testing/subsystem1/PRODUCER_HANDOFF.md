# Producer handoff for the completed S1 transport

The public contract is [telemetry_sources.h](../../app_program/telemetry_sources.h).
S1 already understands the fields on the wire and in the dashboard. An owner
implements the corresponding function once in its own C module. A strong
definition replaces S1's weak unavailable default at link time; no edit to
the S1 serializer, topic subscriptions or browser decoder is required.

Getters copy a protected latest snapshot using TMO_POL. They do not sample
hardware, sleep, print, change actuators or remove an item from an event
queue. Return E_OK only after copying the whole coherent view. Return
E_OBJ/E_TMOUT/E_NOSPT when no view is available. Leave the caller's output
unchanged on error. Publication timestamps are actual tk_get_tim().lo
values recorded by the producer, never timestamps invented in the getter.
S1 rechecks them at serialization.

| Owner | Interface | Required data |
| --- | --- | --- |
| S2 (already connected) | subsystem2_get_motion | Protected current wheel feedback, faults, counts, net distance; existing API |
| S3 (already connected) | subsystem3_get_line | Protected GP27/GP26 pattern and validity; existing API |
| S4 (already connected) | subsystem4_get_telemetry | Actual pitch/state/retained hump estimates; existing API |
| S5 (already connected) | subsystem5_get_range | Protected distance/invalid echo snapshot; added in this batch |
| S3 barcode owner | telemetry_get_barcode | Valid S3 Code39 byte, uint16 event sequence, publication timestamp; optional navigation enum |
| S2 odometer owner | telemetry_get_travel | Actual nonnegative path length in integer mm, publication timestamp |
| S5 (now connected) | telemetry_get_scan | Protected completed three-point profile (30/90/150), stable ID/completion time; generic capacity remains 181 |
| Controller / fault aggregator | telemetry_get_faults | Known/active external source flags and publication timestamp |
| Platform / resource instrumentation | telemetry_get_resources | Allocator/stack values that can actually be measured; unknown values UINT32_MAX |

## Barcode

Keep the existing controller event queue single-consumer. Publish a separate
latest-event snapshot. The S3 byte uses valid 0x80, reversed 0x40 and alphabet
index 0..42. Zero means no decoded barcode. The uint16 event sequence
increments per new event and may wrap. Refresh the publication timestamp
only while the barcode acquisition task is healthy; an old held event must
not hide a stalled producer.

Navigation is supplied by the owner: 0 unmapped, 1 left, 2 right, 3 straight,
4 U-turn. The professor's A/B/C/D mapping is an example; S1 does not infer
actuator behavior from a character. The controller executes navigation.

## Distance and wheel speed

S2's signed centre distance is exported as net_distance_m. It can decrease
in reverse and is not an odometer. Until telemetry_get_travel supplies a real
path measurement, distance_m remains unavailable. Never substitute PWM duty,
nominal speed or abs(net distance) for measured speed/path distance.

S1 converts the existing measured RPM using S2's own tyre diameter and
355/113 circumference convention. Wire resolution is 0.1 mm/s, supported
range ±500.0 mm/s. Counts and signed net 0.1 mm distance retain int32 values
except INT32_MIN, which means unavailable. Out-of-range producer values
become unavailable rather than clamped or wrapped.

## Scans

Implemented by S5 after the user-authorized sensor integration. The current
strong getter supplies right 30, centre 90 and left 150 degrees after an
obstacle-triggered scan. No S1/decoder change was necessary. Invalid echoes
are zero/null; angles are commanded positions. See [the integration](SENSOR_INTEGRATION.md).
The following general contract also permits a future denser profile.


Return a complete profile, not live servo driver state. Count is 1..181,
angles are strictly increasing integers 0..180. Distance is 20..4000 mm;
zero means an invalid/missing echo. Increment the full uint32 profile ID
whenever any profile data changes; an ID denotes immutable points and time.
S1 caches one profile, packs each angle/distance in 20 bits, transmits at
most 16 points per chunk and retries local output backpressure at 100 ms.
Profiles older than 10 seconds are not newly transmitted. The dashboard
labels a completed profile as historical and records its source boot time.

## Faults

Each known bit says its owner has a fresh definite report. An active bit
must also be known. Zero active bits never mean all subsystems are healthy
when coverage is incomplete.

Bits 0..5 are owned by the existing S2 fault API (encoder, timing, left/right
stall, left/right direction), bit 6 by S4 sensor status, bit 7 by the actual
controller state. The external aggregate getter owns only bit 8 ultrasonic,
9 barcode acquisition and 10 scanning. There is one aggregator definition,
not competing strong functions in several drivers. No echo/unknown input
is a missing measurement, not automatically a hardware fault.

## Resource reporting

The default getter reads Kmeminfo without changing allocation behavior.
The minimum free heap is the minimum S1 samples, not an exact allocation-event
peak. Largest block is reported separately. Requested application stacks
come from successful app_start_task/S1 registrations; kernel/radio stacks
and kernel object overhead are excluded. Stack high-water stays unavailable
unless a platform owner replaces the getter with actual instrumentation.
Do not add stack capacities to heap usage: stacks are allocations in that heap.

All source algorithms, GPIO assignments and motor/servo ownership stay with
their respective owners. This handoff prepares transport, not their missing
missing barcode/odometry algorithms or the deferred one-wheel IMU accuracy case.
