# S5 sensor branch integrated with current S1/controller

13 November 2026. User-authorized adaptation of origin/sensor
**c8a789b075ba62b714f92b65a8c671cb3767e1cd** into the existing working tree.
Line following stays enabled. The source was adapted in place without a
branch switch or merge commit; later Git publication is recorded in history. The user subsequently flashed the stationary image and confirmed
servo movement and the dashboard profile; only read-only STATUS/capture was
sent by the diagnostic tool. No normal moving-wheel trial ran.

## Current image and preserved baseline

Single current UF2: **build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2**.
NET=lwip MQTT=1 TEST=1 APP_MODE=integrated; **723456 bytes**, SHA-256
**3a8b79c37a01cfa92615db69e62d2817795ab53ec9decae5e33c4faa19f3aaf6**.
Wheel motors are disabled. **The servo centres at startup and sweeps when
the front is blocked, including in TEST=1.** The user reports flashing this
image and confirms the profile and existing readings update.

The preceding working 390c5893 image/source is preserved in a local rollback
archive outside the repository; see [the backup record](logs/2026-11-13-before-sensor-integration-backup.json).
That preceding image remains the rollback baseline. New stationary scan
evidence is recorded below; normal physical arbitration is still pending.

## Ownership and behavior

S5 owns GP4/GP5 ultrasonic and GP12 servo (PWM slice 6). S2 retains PWM
slices 4/5, encoders and PID. The existing shared GPIO IRQ13 stays in place.
S5 no longer calls any S2 wheel API. It publishes time-stamped sensing and
navigation intent; app_main's controller is the sole integrated wheel requester.

| Fresh S5 intent | Normal controller behavior |
| --- | --- |
| Clear front, centre >250 mm | Existing IR line targets, unchanged |
| Scanning | Zero wheel request; Scanning obstacle state |
| Left / right escape | 0/26 or 26/0 RPM for the S5-selected 500 ms window |
| Blocked / invalid centre / no clear escape | Zero request; Stopped state |
| Missing, busy or >200 ms old intent | Zero request |
| STOP latch or controller contention | Zero before every navigation/line iteration |
| Motor/encoder fault | Preserve inhibit and report controller fault |

Normal startup still waits three seconds. Existing fresh line and measured
S2 feedback gates apply before any movement. The controller refreshes wheel
requests every 20 ms, preserving S2's 100 ms watchdog. A completed turn ends
with a stopped centre check before returning to line following.
STOP cannot be overwritten by subsequent line, scan or turn publications.
Only a Pico restart clears the latch; no remote resume was added.
Servo sensing/telemetry may continue while wheels are inhibited.

The imported obstacle policy uses 30/90/150 degree commands, 300 ms settling,
250 mm blockage and a 30 mm left preference. The integration fixes one
boundary error: a side at/below 250 mm cannot win against a clear side merely
because of the preference margin. No echo remains unknown, never clear space.
S2/S3/S4 algorithms and the previously fixed startup tests remain unchanged.

## Current telemetry, without another format version

- Centre measurements continue through subsystem5_get_range; side echoes
  never replace the front-distance tile. Centre source time is captured at
  its actual measurement, before any side scan.
- S5's strong **telemetry_get_scan** overrides the prepared unavailable
  default. It copies one immutable completed profile with ID/completion
  time and three sorted points: 30/right, 90/centre, 150/left degrees.
- Zero distance codes represent missing echoes; the dashboard plots gaps.
  Angles are commanded servo positions, not measured position feedback.
- The existing v4 scan packet for three points is **24 bytes**, on the
  existing telemetry topic. Main stays 46 bytes, heartbeat five bytes,
  resources 40 and STATUS 16/62. No decoder, subscription or selector added.
- The dashboard retains a completed scan as historical. New scans occur
  when a valid centre reading is <=250 mm, not continuously on a clear path.

S5 adds a five-byte navigation publication and a thirteen-byte scan
publication using its existing snapshot semaphore. No new task, queue or
kernel object. Linked static RAM increases 24 bytes including alignment.

## Validation and remaining physical tests

[Seventeen software groups pass](logs/2026-11-13-sensor-integration-checks.json):
eight actual S5 task/IRQ/publication/serialization checks per build mode,
22 actual controller/PID/motor checks, existing 42 dashboard tests, 51 MQTT
port checks, 23 codec plus 25 collector checks, controller owner tests,
20,089 cross-language vectors, recorder, GPIO, analog and allocator checks.
The first producer-linkage attempt was denied WSL access by the sandbox;
its authorized rerun passed. Both outputs are preserved.

Both forced ARM builds link without warnings. Four actual ARM modes pass;
symbol inspection confirms the strong scan getter and no S5 motor-request
imports. Stack guards include the 1 KiB initial task: S1 startup chain 312,
S5 startup chain 208; collector 464 and S5 sensing chain 136 of their 2048.
These are static checks, not runtime peaks or hardware observations.

| Build | text | data | BSS | linked static RAM | UF2 bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Normal TEST=0 | 356864 | 108 | 84008 | 84116 | 714240 |
| Stationary TEST=1 | 361420 | 108 | 84584 | 84692 | 723456 |

Only the stationary UF2 is retained. The normal check image's hash and both
ELF identities remain in [the build record](logs/2026-11-13-sensor-integrated-artifacts.json).
S1 queue requests remain zero. Resource high-water limitations are unchanged.

Use [the combined physical checklist](FINAL_ACCEPTANCE.md) next:
first stationary servo/profile plus existing distance/IR/pitch/STATUS,
then raised-wheel normal line/obstacle/STOP arbitration. Verify physical
left/right mounting, settling, clearances and stopping. No software check
establishes a successful floor avoidance path or calibrated scan accuracy.


## Live stationary integration check

After explicit flash confirmation, the 90-second independent capture received
**220 main frames**, all with valid distance/pitch, **26 three-point scan
packets**, **22 heartbeats**, and matching TEST=true STATUS in **30 ms**.
There were **zero packet decode errors**. The user confirms the servo looks
left/right, the 30/90/150 profile appears, and distance/IR/pitch still update.

One actual completed profile was 30/right=229 mm, 90/front=90 mm and
150/left=81 mm. These are recorded readings, not calibrated references.
Serial summaries: S1 24/24, S5 software 6/6; S4 completes calibration.
Latest resource snapshot matches 84692 linked static bytes and 150360 free/
largest/minimum-sampled heap bytes, with requested app stacks 10240.
Cumulative publication-error count is 25, not zero; no exact runtime
high-water or strict cadence acceptance is inferred.

[Live capture summary](logs/2026-11-13-sensor-integrated-live-summary.json)
and [board summary](logs/2026-11-13-sensor-integrated-board-summary.txt)
separate received data from user physical observations. The current image is
stationary: physical normal line/avoidance/STOP and floor trajectory remain
on the [combined checklist](FINAL_ACCEPTANCE.md).
