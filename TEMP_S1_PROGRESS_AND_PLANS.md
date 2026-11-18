# Temporary progress and future plans - Subsystem 1

Last updated: **18 November 2026** (Asia/Kuala_Lumpur).

Scope: Buddy 1 communication, telemetry and permitted supervisory requests.
Delete this temporary timeline when the role is finished; durable contracts
and evidence stay under testing/subsystem1 and dashboard/README.md.

## Current position

The sensor branch is now adapted into our current working source. S5 owns
ultrasonic/servo sensing and publishes snapshots; the existing controller
owns every wheel request, including line following and obstacle turns.
STOP has priority over all those requests. The v4 decoder and S1 transport
are unchanged; S5's strong scan getter activates their prepared scan path.

Current single UF2: **build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2**
(no _test), **NET=lwip MQTT=1 TEST=0 APP_MODE=integrated
CONSOLE=usb_cdc CRASH_REPORT=0**; **721920 bytes**, SHA-256
**2c5a4652c7c434fb1d27743a3579b6a9ff0dcf2f2f9a4ee79a813840ecd1bcbe**.
The requested **Resume** button is implemented: a fresh, one-use server ticket
allows the controller to release STOP only with stopped/fault-free wheel
feedback, fresh line data and clear navigation. Existing sensor/PID/steering
algorithms and sensor wire fields are unchanged. STOP preempts pending Resume;
no timeout or reconnect automatically resends it. Run data is preserved.

**The user reports the basic Resume test works on 18 November.** Record this
as a functional pass following the requested Stop -> Resume -> Stop check;
no independent capture, timing or flash readback was collected. Detailed
blocked/fault/expiry/path-retention checks are not inferred from this report.
[Resume user observation](testing/subsystem1/logs/2026-11-18-resume-user-bench-observation.md).

**S1 implementation and Change Requirement 1 evidence are closed for handoff.**
The report includes the current bit table, matched before/after RAM and message
sizes, focused checks and bench performance. Team validation still tracks the
previous longer-run timing misses (5499 ms telemetry, 9304 ms heartbeat),
final-image recovery, resources and integrated demo results.
[Change Requirement 1 evidence](testing/subsystem1/CHANGE_REQUIREMENT_1.md).
The preceding normal image 9c0dbfd5 passed physical STOP/persistence; its
source/image were archived before Resume. Current car power state is unknown.
[Previous bench record](testing/subsystem1/logs/2026-11-17-normal-user-bench-observation.md),
[current Resume fingerprint](testing/subsystem1/logs/2026-11-17-resume-fingerprint.json),
[rollback record](testing/subsystem1/logs/2026-11-17-before-resume-backup.json).

The preceding stationary image 62e5bd63 is archived, with its observed delivery
result preserved below. Only the normal UF2 is kept in build_make.
The preceding stationary delivery correction was **user-flashed and measured for 60 seconds**:
149 main packets, 15 heartbeats, six scans; main median 402 ms/max 600 ms;
heartbeat max 4057 ms; zero missing sequences, publish errors or decoding
errors. Matching STATUS 41 ms; one continuous MQTT session. This closes the
reproduced stationary pauses for this observed window, not worst-case or
repeated-outage acceptance.
The prior motion image now has user-confirmed website readings; read-only
captures show real counts and path, but also repeated delivery stalls.
Paired TCP/WebSocket packets arrive within 0..1 ms of each other, with up to
3.104 s gaps and dropped sequences, locating the main stall upstream of the
browser. The correction disables WiFi sleep/TCP coalescing, bounds periodic
backlog and redraws on receipt. Sensor/control algorithms and v4 are unchanged.
See [delivery evidence](testing/subsystem1/TELEMETRY_DELIVERY.md),
[motion telemetry](testing/subsystem1/MOTION_TELEMETRY.md) and
[current build identity](testing/subsystem1/logs/2026-11-17-delivery-fingerprint.json).

The preceding 3a8b79c3 stationary image was user-flashed and passed its
sensor/servo/dashboard check: 220 valid distance/pitch frames, 26 scans,
22 heartbeats, matching STATUS and zero decode errors. It remains preserved
in [the rollback archive record](testing/subsystem1/logs/2026-11-17-before-motion-feedback-backup.json).
In that preceding stationary image wheel motors were disabled. In the current
normal image the controller enables motors; **the servo centres at startup
and sweeps on an obstructed front**. Earlier hardware passes apply to their captured image.

Previous rollback hardware image is 390c5893. It restored live
distance/IR/pitch after the startup-test stack fix, and the user confirmed
the dashboard updates. It is preserved in
[the rollback archive record](testing/subsystem1/logs/2026-11-13-before-sensor-integration-backup.json).
The preceding stationary scan pass has its own evidence in
[the live summary](testing/subsystem1/logs/2026-11-13-sensor-integrated-live-summary.json).
The preceding normal two-wheel STOP and persistence functional checks passed
by user observation with packet support. Floor avoidance and STOP initiated
during an active avoidance turn remain separate open checks.

## S1 and integration coverage

| Area | Current implementation |
| --- | --- |
| WiFi/MQTT/recovery | Existing owner/retry/backoff/session handling retained |
| Telemetry | Independent collection; current 46-byte v4 main, heartbeat, resources |
| Existing sensors | Protected distance/IR/IMU snapshots; measured S2 feedback in normal and passive stationary modes |
| Commands/status | Read-only STATUS; correlated STOP and explicit one-use Resume with controller-owned admission |
| Line following | Still enabled; original line targets retained when front is clear |
| Obstacle sensing | S5 servo/HC-SR04 loop imported from sensor; no direct S2 requests |
| Scan display | Strong passive S5 getter; actual three-point profile through existing v4 scan format |
| Controller arbitration | Startup/STOP/fault/freshness gates before every line or turn request |
| Missing producers | Barcode/navigation and external fault aggregate remain explicit unavailable hooks |
| Resources | Read-only heap samples and app stack reservations; true high-water unavailable |
| Validation | 20 offline groups, 58 dashboard tests, linked producer check, four ARM profiles, stack guards and both builds pass; basic Resume user-reported pass, detailed acceptance pending |

Current normal linked static RAM is **84148 bytes**, +24 from the preceding
84124-byte normal image. Resume stationary builds use 84884 bytes, including
TEST-only fixtures. No new task, queue or semaphore was added for Resume. The passive task adds a **2048-byte
dynamic stack plus kernel task overhead**; existing semaphores are reused.
S1 queue requests remain zero. Odometer state is six packed task-local bytes;
the protected path/time record is eight static bytes.
Static stack estimates: S2 544/2048, collector 464/2048, S5 136/2048;
startup test/console chains S1 312/1024 and S5 208/1024. Runtime peaks remain open.

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

- 17 November: enable passive encoder sampling in integrated TEST=1, supply
  centre-path getter and align average speed card/sparkline/CSV. Offline
  checks and both builds pass; user requests image preparation, testing later.
- 17 November: user sees motion values, reports delivery pauses. Live captures
  reproduce upstream stalls/output errors; focused WiFi/TCP/backpressure/UI
  correction built. Initial one-at-a-time gate removed drops but remained
  slow; bounded two-packet streaming then passed a user-flashed 60-second
  window, max 600 ms main/max 4057 ms heartbeat with zero dropped sequences
  or publish errors. Both attempts remain recorded.

- 17 November: user requests normal demo operation with motors enabled.
  Prepare TEST=0 integrated from identical sources, verify normal motor/S1
  symbols and existing motor/STOP/MQTT checks. User confirms flash and physical
  two-wheel STOP; matching result captured. Persistence through the requested
  IR/scan changes and dashboard reconnect passed by user observation; the car
  is unplugged. Longer normal delivery timing remains open.

- 17 November: add explicit Resume with one-use server authorization, two-second
  leases, controller readiness/fault admission, STOP priority and correlated
  completion. Normal and stationary builds pass; sensor/PID/steering source
  files match the verified rollback.
- 18 November: user reports the Resume test works after the requested
  Stop -> Resume -> Stop procedure. Basic functional pass recorded; no new
  timing, negative-case or exact flash-identity evidence was captured.
- 18 November: close S1's Change Requirement 1 evidence. Verified the local
  build/source hashes, reran 11 focused check groups and reviewed raw delivery
  captures. Carry the recorded performance limits into group validation.

## Remaining owner work

| Owner | Remaining work |
| --- | --- |
| S2/group | Physical encoder/odometer calibration and new-image hand-turn check |
| S3/controller | Passive latest barcode event and owner navigation mapping |
| S4 | Deferred one-wheel/curb logic, calibrated height and encoder speed |
| S5/controller | Physical servo orientation/settling, scan/avoidance trajectory and final mission behavior |
| Controller/fault aggregator | Real external known/active fault coverage |
| S1/group | Current-image cadence/recovery/resources and integrated demonstration |

## Team handoff

The next unfinished producer interface is S3's barcode capture and latest
result on GP28. S1 already has the binary fields and display. The S3 owner
can connect the existing Code 39 decoder to the acquisition path and expose
a protected latest-result getter once the sensor path is ready.
[Producer handoff](testing/subsystem1/PRODUCER_HANDOFF.md).

## Remaining physical session

Use [normal mode instructions](testing/subsystem1/MOTOR_STOP_TEST.md) and
[the combined checklist](testing/subsystem1/FINAL_ACCEPTANCE.md). Normal
line/scan/turn/STOP/Resume checks require both drive wheels securely raised. Passive
hand-turn checks require a separately rebuilt TEST=1 stationary profile. No separate battery
switch exists; USB/power bank is the whole-car supply. Normal restart clears
the latch and can start motion after three seconds. Explicit Resume can also
restart autonomous motion without reboot; test only with both wheels raised.

[Integration behavior and evidence](testing/subsystem1/SENSOR_INTEGRATION.md),
[producer contract](testing/subsystem1/PRODUCER_HANDOFF.md),
[dashboard/setup](dashboard/README.md),
[test history](testing/subsystem1/TEST_LOG.md).

Earlier 2 s / 5 s / 15 s targets are design/test-plan criteria, not numbers
stated by the professor's PDF. Physical accuracy, repeated recovery and
runtime high-water are not established by host checks. S2/S3/S4 algorithms
are hash-verified unchanged for this Resume update; the S1 command,
dashboard and controller admission paths were deliberately extended.
Git publication is recorded in repository history and does not change the
hardware evidence above. No broker interruption occurred. Live diagnostics sent
read-only STATUS; the user performed the flash and target/servo check. Credentials remain in
ignored wifi_secrets.h and are excluded from reports and backups.
