# S1 integration: real IMU pitch and hump estimates

6 November 2026. This stage follows the user's functional passes for the
six-byte distance/IR image: target movement, stale readings, normal laptop
restart and power-bank restart. See the [user bench record](logs/2026-11-06-cr1-user-bench-observation.md).

## What the new build provides

The current build sends **11-byte packed v3 telemetry** on the existing
telemetry topic. Five-byte binary heartbeats are unchanged. The first six
bytes preserve the v2 sequence/distance/line-IR contract. The added five
bytes carry actual S4 pitch, state, last accepted hump, run maximum and
estimator status. No simulated feed is used. The normal TEST build still
disables driving and commands remain disabled.

Updated 10 November: the dashboard accepts **only the current 11-byte v3
telemetry and five-byte binary heartbeats**. Old decoder implementations
and the selector have been removed. Refresh the browser after pulling code;
flash the current UF2 if the car still has an older format. See the
[latest update/build record](TEST_LOG.md).

## Firmware handoff and S4 owner review

Review the changes to `app_program/subsystem4/subsystem4.c` and `.h` with
the S4 owner before the team merge. They repair the telemetry interface and
its synchronization; they are not approval or validation of the S4 estimator.
S4 retains I2C1 ownership. S1 never calls the sensor driver.

- The old getter returned the calibration baseline as current pitch. The
  owner task now publishes the last successful actual sample separately.
- The old getter generated a new timestamp on every read. It now returns
  source publication and acceleration-sample times. Pitch becomes invalid
  after 100 ms; S1 rechecks that age after queueing. Overall S4 publications
  expire after 1000 ms, without discarding fresh distance/IR.
- Getters copy an **18-byte packed shared snapshot** under the existing
  semaphore with `TMO_POL`. No I2C or task delay occurs while locked. The
  estimator's intermediate variables are private to its task.
- Reset and speed requests use two bits in one protected request mask.
  The owning task applies the requests. A pending explicit new-run reset
  is immediately visible to readers; ordinary stops/recovery preserve max.
- A measured speed of zero is accepted. The speed-supplied flag remains
  clear until the speed setter is called. Fault retry uses a task sleep
  rather than a 500 ms CPU busy-wait.
- The new `pitch_timestamp_ms` member extends the logical getter interface.
  Consumers must rebuild together. This logical API struct is decoded from
  packed storage and is not copied onto the wire.

The internal S4 estimator, native local state, integration mathematics,
speed freshness/physical bounds and its complete CR1 audit remain the
S4 owner's work. In particular, no S2 encoder feedback is connected here.

## Accuracy and what the display means

Current S4 height integration still uses **nominal 150 mm/s** until measured
encoder speed is supplied. The dashboard states this beside the height
readings. Those are estimates with unverified accuracy, even when they pass
the existing estimator's 6..80 mm plausibility gate. Hand tilting can trigger
the estimator; it does not validate hump height.

`pitch_deg` is the mounted vehicle angle, not the calibration baseline or a
fabricated zero. Calibration supplies the baseline used internally for hump
detection. A resting angle need not be exactly zero. Front-up should increase
pitch. A missing/faulted/stale angle is null. A zero height code means no
accepted hump yet, displayed as unavailable/no accepted hump rather than a
measured zero. Retained accepted heights can remain visible during a fresh
sensor-fault status; all live fields become unavailable if telemetry stops.

## Wire contract

Bytes 0..5: unchanged packed v2 prefix (sequence:32, distance:12, line:3,
format marker:1). Bytes 6..10: this little-endian 40-bit block:

| Block bits | Field | Valid information | Logical bits |
| --- | --- | --- | ---: |
| 0..10 | Pitch code | 0 unavailable; 1..1801 maps to -900..900 tenths of a degree | 11 |
| 11..17 | Last accepted height | 0 none yet, or 6..80 mm | 7 |
| 18..24 | Run maximum | 0 none yet, or 6..80 mm; at least last height | 7 |
| 25..27 | S4 state | 0 initializing, 1 calibrating, 2 monitoring, 3 ascent, 4 descent, 5 validate, 6 fault, 7 unavailable | 3 |
| 28..33 | Flags | Sensor OK, hump active, new event, max updated, external speed supplied, fault | 6 |
| 34..39 | Padding | Reserved zero; rejected if nonzero | 0 application bits |

Unavailable is canonical: state 7 and all other block bits zero. Sensor OK
and fault cannot both be set. A valid pitch requires sensor OK and states
2..5. The public `imu_flags` reconstructs S4's original sparse bit positions,
including derived pitch-valid bit 4 and speed-supplied bit 5. `imu_state` is
separate from the vehicle/controller state. Its event/flags are sampled
status; this is not a reliable event-delivery/acknowledgement channel.

The block has 34 logical bits and rounds to five bytes. Together with the
48-bit prefix, the frame is 11 bytes. The current decoder requires exactly
11 bytes; shorter historical frames are rejected.
Both C and JavaScript validate the same reserved values and ranges.

Golden vector: sequence 1, 451 mm, left black/right background, pitch +15.0
degrees, last height 12 mm, max 34 mm, monitoring, nominal speed:

```text
01 00 00 00 c3 e1 1b 64 88 14 00
```

## Storage and build comparison

Same flags: `NET=lwip MQTT=1 TEST=1 SMP=1 CONSOLE=usb_cdc`. Before means the
working packed-v2 build, not JSON. More fields are now provided; these are
feature costs, not a claim of a like-for-like memory reduction.

| Item | Packed v2 | With IMU/v3 |
| --- | ---: | ---: |
| Telemetry payload | 6 B | 11 B |
| Heartbeat payload | 5 B | 5 B |
| Input queue record | 10 B | 10 B |
| Output queue record | 14 B | 27 B |
| Four-slot input buffer including kernel rounding/headers | 64 B | 64 B |
| Four-slot output buffer including kernel rounding/headers | 80 B | 128 B |
| Total requested S1 queue storage | 144 B | 192 B |
| Linked static RAM (`data+bss`) | 82740 B | 82764 B |
| Linked text/read-only image | 346532 B | 351116 B |

An IMU queue snapshot uses 13 bytes: five field bytes plus two full 32-bit
source timestamps. S4's published buffer uses 141 logical bits rounded to
18 bytes: pitch 11, baseline 11, heights 7+7, state 3, flags 6, and three
32-bit values (traversal duration, publication time, pitch sample time).
The speed request retains the existing full uint16 range. Request flags use
one bit each in a shared byte. Heap/stack peak use and actual timing remain
unmeasured. [Evidence metadata](logs/2026-11-06-imu-evidence.json).

## Validation and next hardware test

- S1: **32 codec/parser + 15 queue checks passed**. [Log](logs/2026-11-06-imu-s1-host-tests.txt).
- Actual S4 source with stub kernel/driver: **16 snapshot/owner-loop checks passed**. No physical I2C or trigonometric accuracy is claimed. [Log](logs/2026-11-06-imu-snapshot-tests.txt).
- Dashboard: **32 tests passed**. [Log](logs/2026-11-06-imu-dashboard-tests.txt).
- Actual C bytes decoded in JS and MQTT-framed byte-for-byte: **19914 v2 + 4083 v3 vectors, plus 3 heartbeats passed**. V3 includes every signed pitch value, every accepted height, valid status combinations, and expiry boundaries. [Log](logs/2026-11-06-imu-c-to-js-tests.txt).
- ARM WiFi/MQTT TEST build succeeded. [Build log](logs/2026-11-06-imu-firmware-build.txt).
- Browser visual QA unavailable because the browser access host is unavailable.

New image, **built but not flashed by the agent**:
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
Cleanup on 6 November: verified this standard output has the same SHA-256
as the IMU image in the evidence record, then kept it as the single current
UF2. Removed the identical `_imu` alias, obsolete firmware variants and
old baseline ELF/map copies at the user's request. Current ELF/map build
outputs and historical comparison logs/hashes remain. Historical artifact
paths in the evidence JSON describe the files at capture time; removed
variants are no longer available locally.
Binaries include local network settings and remain ignored by Git.

1. Keep the car stationary and sensor wiring unchanged. Flash the new image.
2. Leave it level/still for about two seconds while S4 calibrates. Refresh
   the local dashboard and connect. The current format is automatic.
3. In Sensors & motion, watch Vehicle pitch and IMU & height status. Raise
   the front gently, then lower it: pitch should increase/decrease. Return
   to level before holding a tilt long enough to trigger hump timeout.
4. Confirm 11-byte accepted telemetry and five-byte heartbeats. Distance
   and line IR must still respond. Wheel speed remains unavailable.
5. Confirm pitch/live readings become unavailable after disconnect and
   return after normal laptop and power-bank startup/calibration.
6. If capturing serial output, expect **33 passed, 0 failed** for S1 (32
   software checks plus S1-U43 hardware clock check). Not observed yet on
   this image. Record IMU calibration/fault messages and actual results.

Validate height later on a measured hump with actual encoder speed and the
S4 owner. Do not change sensor wiring or move actuators from the dashboard.
