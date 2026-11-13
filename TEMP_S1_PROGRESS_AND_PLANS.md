# Temporary progress and future plans - Subsystem 1

Last updated: **13 November 2026** (Asia/Kuala_Lumpur).

Scope: Buddy 1 communication, telemetry and permitted supervisory requests.
Delete this temporary timeline when the role is finished; durable contracts
and evidence stay under testing/subsystem1 and dashboard/README.md.

## Current position

The sensor branch is now adapted into our current working source. S5 owns
ultrasonic/servo sensing and publishes snapshots; the existing controller
owns every wheel request, including line following and obstacle turns.
STOP has priority over all those requests. The v4 decoder and S1 transport
are unchanged; S5's strong scan getter activates their prepared scan path.

Current single UF2: **build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2**.
NET=lwip MQTT=1 TEST=1 APP_MODE=integrated; **723456 bytes**, SHA-256
**3a8b79c37a01cfa92615db69e62d2817795ab53ec9decae5e33c4faa19f3aaf6**.
Built and **user-flashed**. Stationary sensor/servo/dashboard check passed:
the user confirms sweeping, profile display and existing readings; capture
has 220 valid distance/pitch frames, 26 scans, 22 heartbeats, matching STATUS
and zero decode errors. Wheel motors are disabled; **the servo centres at
startup and sweeps on a blocked front**.

Previous rollback hardware image is 390c5893. It restored live
distance/IR/pitch after the startup-test stack fix, and the user confirmed
the dashboard updates. It is preserved in
[the rollback archive record](testing/subsystem1/logs/2026-11-13-before-sensor-integration-backup.json).
The current stationary scan pass has its own evidence in
[the live summary](testing/subsystem1/logs/2026-11-13-sensor-integrated-live-summary.json).
Normal physical avoidance/STOP is still pending.

## S1 and integration coverage

| Area | Current implementation |
| --- | --- |
| WiFi/MQTT/recovery | Existing owner/retry/backoff/session handling retained |
| Telemetry | Independent collection; current 46-byte v4 main, heartbeat, resources |
| Existing sensors | Protected distance/IR/IMU snapshots; measured S2 feedback in normal mode |
| Commands/status | Read-only STATUS; session/lease/correlated controller-owned STOP latch |
| Line following | Still enabled; original line targets retained when front is clear |
| Obstacle sensing | S5 servo/HC-SR04 loop imported from sensor; no direct S2 requests |
| Scan display | Strong passive S5 getter; actual three-point profile through existing v4 scan format |
| Controller arbitration | Startup/STOP/fault/freshness gates before every line or turn request |
| Missing producers | Barcode/navigation, true travelled path and external fault aggregate remain explicit unavailable hooks |
| Resources | Read-only heap samples and app stack reservations; true high-water unavailable |
| Validation | 17 software groups, four ARM modes and both full builds pass; new hardware checks pending |

Static RAM: stationary **84692 bytes**, normal **84116 bytes**. Additional
S5 publications use 18 bytes plus alignment (24-byte linked increase).
No new task, semaphore or queue. S1 queue requests remain zero.
Static stack guards: collector 464/2048, S5 sensing 136/2048;
startup test chains S1 312/1024 and S5 208/1024. Runtime peaks remain open.

## Milestones retained

- 6 November: real distance/IR/heartbeat; normal-boot microsecond clock fixed.
- 10-11 November: STATUS and recovery corrections; original failed trials and
  confirmed 5/5 hotspot series preserved for their own image.
- 12-13 November: integrate pulled AO/encoder wiring; user-observed normal
  physical STOP and persistence through all IR changes/dashboard reconnect.
- 13 November: prepare all S1 producer interfaces/current v4 in one batch.
- 13 November: new on-board tests overflowed the 1 KiB initial stack;
  large fixtures moved to static TEST-only storage; ARM regression added.
  Current transport then received 86 valid distance/pitch samples, all IR
  codes and resources; user confirmed the dashboard was restored.
- 13 November: fetch/review sensor c8a789b; then user authorizes integration.
  Retain controller ownership/STOP and new passive handoff; fix blocked-side
  margin selection; software/build validation complete. User-flashed; stationary servo/profile and other readings confirmed.

## Remaining owner work

| Owner | Remaining work |
| --- | --- |
| S2 | Passive actual path-length getter, distinct from signed net travel |
| S3/controller | Passive latest barcode event and owner navigation mapping |
| S4 | Deferred one-wheel/curb logic, calibrated height and encoder speed |
| S5/controller | Physical servo orientation/settling, scan/avoidance trajectory and final mission behavior |
| Controller/fault aggregator | Real external known/active fault coverage |
| S1/group | Current-image cadence/recovery/resources and integrated demonstration |

## Next physical session

Use [the combined checklist](testing/subsystem1/FINAL_ACCEPTANCE.md): stationary
servo/profile passed with existing distance/IR/pitch/STATUS; normal line/scan/
turn/STOP only with both drive wheels securely raised. No separate battery
switch exists; USB/power bank is the whole-car supply. Normal restart clears
the latch and can start motion after three seconds.

[Integration behavior and evidence](testing/subsystem1/SENSOR_INTEGRATION.md),
[producer contract](testing/subsystem1/PRODUCER_HANDOFF.md),
[dashboard/setup](dashboard/README.md),
[test history](testing/subsystem1/TEST_LOG.md).

Earlier 2 s / 5 s / 15 s targets are design/test-plan criteria, not numbers
stated by the professor's PDF. Physical accuracy, repeated recovery and
runtime high-water are not established by host checks. S2/S3/S4 algorithms
and the working S1/decoder/latch sources were hash-verified unchanged.
Git publication is recorded in repository history and does not change the
hardware evidence above. No broker interruption occurred. Live diagnostics sent
read-only STATUS; the user performed the flash and target/servo check. Credentials remain in
ignored wifi_secrets.h and are excluded from reports and backups.
