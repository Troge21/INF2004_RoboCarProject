# Temporary progress and future plans - Subsystem 1

Last updated: **10 November 2026** (Asia/Kuala_Lumpur).

Scope: **WiFi communication, permitted supervisory commands and telemetry**
for the INF2004 group RoboCar project. This is a personal working timeline
for the assigned role, not a claim that the entire group project is complete.
Delete this file when the role is finished. Keep the durable protocol notes,
test records and code in their normal project locations.

## Timeline

Dates marked "earlier sessions" were not reconstructed. Future rows are an
ordered plan, not promised calendar deadlines.

| When / order | Milestone | Status | Evidence or completion condition |
| --- | --- | --- | --- |
| Earlier sessions | Understand project, S1 role and change requirement | Reviewed | WiFi/MQTT, telemetry, commands, recovery and compact-data evidence identified |
| Earlier sessions | Build local dashboard | Done | Overview, Sensors & motion, Communications, charts and CSV export |
| Earlier sessions | Remove demo feed, personal branding; enlarge text | Done | Real MQTT data only; group label; empty unavailable states |
| Earlier sessions | Prepare Windows Mosquitto and WSL toolchain | Set up | Windows broker/browser; Ubuntu builds Pico firmware |
| 6 Nov 2026 | Establish real ultrasonic + heartbeat connection | Observed working | Corrected broker IP to 172.20.10.2, rebuilt/flashed; received real TCP and WebSocket messages; user confirmed dashboard worked |
| 6 Nov 2026 | Add centre/right IR telemetry | Code implemented; physical trial pending | S1 reads protected S3 snapshot; three packed line bits; freshness checks; dashboard distinguishes unknown/background/black |
| 6 Nov 2026 | Validate IR code and produce new UF2 | Development checks passed | 20 dashboard tests; 20 S1 source checks plus 9 queue checks on host; WiFi/MQTT TEST firmware built |
| 6 Nov 2026 | Observe IR-capable firmware on real hardware | Live messages observed; physical pattern check pending | Fresh line_bits=4 received over MQTT; exact flashed image hash not independently confirmed |
| 6 Nov 2026 | Clarify dashboard IR names | Done | Barcode IR = GP26; Left IR = GP6 (firmware calls it centre); Right IR = GP16. Display names and CSV header updated; wiring, bits and steering unchanged |
| 6 Nov 2026 | Fix BOOTSEL-dependent microsecond clock startup and check normal operation | Fix flashed; normal laptop and power-bank readings observed | Explicit 1 MHz tick on every boot. Laptop cold boot: S1 21/21, 24/24 valid distances (162..163 mm). Later power-bank capture: 24/24 valid distances (117..147 mm), 3 heartbeats. Initial power-bank windows had no messages; recovery timing remains unmeasured. Wiring unchanged |
| Separate hardware follow-up | Review HC-SR04 voltage/interface requirements | Open; not established as the cause of boot-dependent failure | User confirms VCC on 3V3 and ECHO directly to GP5, with no resistors supplied. Standard module is specified for 5 V; never raise supply to 5 V with ECHO direct. First evaluate startup fix without changing wiring |
| Current bench follow-up | Verify IR image and test all physical patterns | User reports line_bits working; detailed evidence pending | Record all four black/background values, image identity and stale-value behavior |
| 6 Nov 2026 | Add real IMU pitch/status and hump estimates | Code/build/software checks passed; new image not flashed | V3 11-byte telemetry; corrected protected S4 publication; nominal speed assumption shown; S4 owner review and physical tilt test pending |
| Following | Integrate measured wheel speeds and distance | Waiting for S2 interface | Normal firmware currently has no encoder feedback; duty percentage is not measured speed |
| Following | Agree controller telemetry and command interfaces | Pending team integration | Move temporary bench route to agreed controller snapshots; agree allowed commands and response semantics |
| 6 Nov 2026 | Convert current S1 telemetry and queues for Change Requirement 1 | Code/build and offline checks passed; user reports live packed messages | Six-byte telemetry, five-byte heartbeat, packed queue records; 69 to 6 payload bytes and 192 to 144 queue-buffer bytes; linked static RAM unchanged |
| 6 Nov 2026 | Observe packed v2 telemetry in the dashboard | User-reported functional passes | Working line_bits and six-/five-byte messages; user confirms distance, stale readings, normal laptop restart and power-bank restart; timing/accuracy not measured |
| 10 Nov 2026 | Pull barcode/line progress, rebuild and simplify decoder | Software/build checks passed | Latest S3 changes retained; current-format decoder only; user confirms earlier apparent failure was setup, not a reproduced IMU regression |
| Following | Complete group-wide packed-data requirement | Pending | Other subsystem/controller/fault/command conversions and physical correctness/performance evidence remain |
| 10 Nov 2026 | Implement read-only STATUS request/reply | Implemented and real-Pico/browser functional checks passed | Packed five-byte request, correlated reply with uptime/source age; one pending request, timeout/disconnect handling; final captured round trip 29 ms |
| Following | Integrate permitted actuator/supervisory requests through controller | Pending | STATUS is read-only. STOP remains disabled; agree controller handoff, safety/expiry/replay semantics before actuator commands |
| 10 Nov 2026 | Prepare WiFi/MQTT recovery corrections and repeatable trials | Offline checks/build passed; live trials deferred by user | DOWN/failure rejoin, bounded MQTT retry, clean session/timer cancellation; direct-TCP recorder and separate five-trial procedures; car unavailable |
| 10 Nov 2026 | Flash recovery UF2 and run five broker restarts | User reports flash; five actual interruptions recovered | Telemetry, heartbeat and STATUS returned; uptime continuity verified using both request/reply windows; conservative bounds 3.318..5.811 s; interval targets not all met |
| 10 Nov 2026 | Run five hotspot loss/recovery trials | Recorded; earlier 15 s design target not met | Three correlated recoveries at 15.572, 18.015 and 20.484 s; two incomplete within 30 s. All original results preserved |
| 10 Nov 2026 | Bound MQTT liveness/connection timeouts after hotspot trial | Code/build/36 host checks passed; user reports flashing | MQTT keepalive 60 -> 5 s; pending TCP/MQTT connect deadline 5 s reuses retry timer. No sensor/schema changes |
| 10 Nov 2026 | Observe replacement's normal operation before retest | 60-second live baseline observed | 118 telemetry, 13 heartbeats, three matched STATUS replies; k5 confirmed in broker; no rejected packets; interval targets still not all met |
| 10 Nov 2026 | Review follow-up hotspot captures | Results recorded; running-image identity later unconfirmed | Partial run: 2/3 under 15 s. Later complete run after laptop restart: 4/5 at 6.668..9.096 s, fifth timeout. User later clarified intended UF2 had not been flashed; do not accept these as corrected-image validation |
| 10 Nov 2026 | Verify communication after explicitly confirmed flash | Normal telemetry/STATUS/USB restored | 37 telemetry, three heartbeats, matching STATUS in 81 ms; USB summary 33 passed/0 failed (may be buffered startup output) |
| Next physical follow-up | Repeat one clean hotspot series after confirmed flash | Pending | Same 9c5ac8f0... UF2; no source/rebuild change. Earlier captures preserved with image-identity caveat; no laptop/Pico restart within the new five-trial series |
| Following validation | Resolve delivery-interval misses and finish acceptance evidence | Open | One observed telemetry interval 2.111 s; heartbeat intervals up to 6.393 s. Do not mark all earlier 2 s/5 s targets passed |
| Final | Collect submission evidence and documentation | Pending | Commit identity, logs, timing results, protocol/representation tables, memory/message comparison and group demo evidence |

## Change Requirement 1 in plain language

The professor asks us to keep the same required information while storing
and sending it with the minimum number of bits needed for its range. It
applies to application data in RAM, task/ISR sharing, queue messages and
MQTT payloads across the whole group. It also requires a representation
table, RAM and message-size comparisons, and correctness/performance evidence.

Our current stage converts the working distance/IR/heartbeat path first,
so later measurements can be added using the packed representation from
the start. It does not finish every teammate's application-state conversion
or the not-yet-integrated command path.

The initial v2 conversion used six-byte binary telemetry and five-byte
heartbeats. Its example payload fell from 69 to 6 bytes and queue requests
from 192 to 144 bytes. The user then passed the four functional distance,
staleness and power-source checks. The new v3 IMU build adds five bytes to
the frame and source metadata to the queue: 11-byte telemetry, 192-byte queue
requests and 82764 bytes linked static RAM. These additional measurements
are a feature extension, not a like-for-like RAM saving. Peak use and timing
remain unmeasured. See [the IMU integration guide](testing/subsystem1/IMU_TELEMETRY.md).

See [the implementation and evidence report](testing/subsystem1/CHANGE_REQUIREMENT_1.md)
for the field table, exact format, measurements and remaining coverage.

## What works now

- The Pico connects over WiFi to Mosquitto running on the laptop.
- Mosquitto receives car MQTT on the laptop WiFi address, port 1883, and
  serves the local browser over WebSockets at `127.0.0.1:9001`.
- The local website is served at `http://127.0.0.1:4173`.
- Real ultrasonic distance and heartbeats were observed end to end. The user
  confirms the packed-v2 distance, stale-data, normal laptop restart and
  power-bank restart checks passed. Current v3 telemetry and STATUS delivery
  have also been observed; controlled accuracy and recovery timing remain separate.
- The dashboard rejects retained/invalid/replayed telemetry, tracks freshness,
  preserves bounded received history and exports real data.
- The user reports that line bits work as intended and recent messages have
  the expected six-/five-byte sizes. The exact four-pattern observations
  and running-image identity have not been independently captured; see the
  [packed bench observation](testing/subsystem1/logs/2026-11-06-cr1-user-bench-observation.md).
- In the initial power-source comparison, communication worked on both sources,
  but every observed ultrasonic reading was invalid. Later startup-code work
  restored valid readings; preserve the original observations in the
  [power-source observation](testing/subsystem1/logs/2026-11-06-power-bank-observation.md).
- The later clarification is that BOOTSEL flashing temporarily restores
  readings. A missing normal-boot hardware tick initialization was found in
  platform startup and fixed locally. The flashed fix passed all 21 S1
  checks on normal laptop cold boot and produced 24/24 valid distances.
  A later power-bank capture also had 24/24 valid distances and three
  heartbeats, without a wiring change. Initial power-bank captures were
  silent; association/recovery timing and repeated cold boots still need
  controlled trials.

Current v3/STATUS/recovery build, **user reports flashing after hotspot correction**:
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
Only this UF2 is kept. Older aliases/baselines were removed at the user's
request; their historical measurements remain. The recovery build supersedes
the earlier recovery image at this filename. Five broker and five hotspot
trials exercised the previous aaf0e39836b759904add31509ca4fef3a06a6892f6e58589b3cbf4342226fa77
image. The current
replacement hash is **9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0**.
The broker now advertises k5, and a one-minute normal-operation capture
received telemetry, heartbeat and STATUS. Hotspot recovery retesting remains
pending; no flash readback was performed.
Later clarification: the user said the intended UF2 had not been flashed for
the preceding follow-up runs, then explicitly reported flashing it. Their
local-file hashes and k5 observation do not establish exact earlier on-device
image identity. The latest confirmed-flash check is described below.

For the preceding STATUS image, the user refreshed/reconnected and confirmed its Request
status button works. A separate real-Pico capture matched the reply in
29 ms and continued receiving five telemetry samples plus a heartbeat.
Expected on-device S1 summary remains 33 checks; serial output for this
image has not been captured. This does not establish height accuracy,
repeated recovery performance or exact physical IR patterns.
Build flags: `NET=lwip MQTT=1 TEST=1`. With the current configuration,
TEST disables normal driving and encoder calibration remains off.

## Current IR milestone and dashboard names

The dashboard uses role-based labels. Firmware and earlier test records keep
their existing names, so use this mapping when reading code:

| Dashboard name | GPIO | Firmware terminology |
| --- | --- | --- |
| Barcode IR | GP26 | Left/barcode input |
| Left IR | GP6 | Centre line input |
| Right IR | GP16 | Right line input |

Only the names displayed in the dashboard and the CSV header change.
`line_bits`, wire data positions, GPIO configuration and steering logic stay
the same. CSV `ir_left` now names the GP6 reading formerly exported as
`ir_centre`. The left line reading is not the GP26 barcode input.

Data path:

```text
S5 distance sample -> S1 input queue
                       + S3 protected centre/right snapshot
                       + S4 protected IMU publication
                    -> S1 output queue (raw snapshots)
                    -> freshness check + 11-byte v3 serialization in network poll
                    -> existing MQTT service -> Mosquitto -> dashboard
```

The unchanged six-byte distance/IR prefix example (sequence 12, 451 mm, left black/right background); v3 appends the five-byte IMU block:

```text
0c 00 00 00 c3 e1
```

The dashboard still presents decoded measurements; it does not show zero
sentinels as real distance readings. Historical JSON examples remain in the
older test records.

| `line_bits` | Left line IR (GP6) | Right line IR (GP16) |
| --- | --- | --- |
| 0 | Unavailable | Unavailable |
| 4 | Background | Background |
| 5 | Background | Black |
| 6 | Black | Background |
| 7 | Black | Black |

Bit 2 is validity; bit 1 is left line IR (GP6, firmware centre); bit 0 is
right line IR (GP16). The three line bits are stored within the binary frame; a local line snapshot
uses five bytes including its 32-bit timestamp. The decoded inspector shows
`line_bits` as a number for readability. The source
timestamp remains local metadata. S1 clears IR validity if a snapshot is
older than 100 ms when collected or serialized. All browser current values
expire after 2 seconds without fresh telemetry or on disconnect. The left
GP26 input remains barcode-only. Validity means a successful recent snapshot;
it cannot establish that an individual digital sensor is correctly wired.

The group-wide change requirement remains open. The current S1 telemetry/heartbeat
path and its queues are packed; other application fields and commands still
need conversion and real hardware performance evidence.

## Physical check to run next

- [ ] Start the same hotspot (iPhone Maximize Compatibility enabled).
- [ ] Confirm the laptop WiFi IPv4 matches `wifi_secrets.h` and
      `dashboard/mosquitto.local.conf`.
- [ ] Start the custom Mosquitto broker and local dashboard server.
- [x] Observe IR-capable firmware sending real `line_bits` messages.
- [x] Observe live packed message sizes and functional line_bits (user-reported;
      exact running-image identity not independently captured).
- [ ] Confirm the packed image on-device S1 summary: **33 passed, 0 failed** for the new IMU image,
      including hardware timer check S1-U43 after a normal power-on. The earlier
      JSON image passed 21 checks; that is historical evidence, not a packed-image pass.
- [ ] With both line sensors on white/background, observe `line_bits:4`.
- [ ] Left line IR (GP6) on black only: observe `line_bits:6` and left indicator filled.
- [ ] Right on black only: observe `line_bits:5` and right indicator filled.
- [ ] Both on black: observe `line_bits:7` and both indicators filled.
- [ ] Confirm the Barcode IR tile says "Dedicated barcode input".
- [x] User reports v2 stale-reading check passed after unplugging.
- [x] User reports v2 distance, normal laptop restart and power-bank restart passed.
- [ ] Repeat these checks on the new v3 image and observe real pitch changes.
- [ ] Reconnect, confirm fresh readings return, and export CSV with L/R line values.
- [ ] Save serial/MQTT observations and append actual results to the test log.

Do not infer hardware success from host unit tests. Use a stationary bench
check first. Do not change another subsystem's GPIO or driver ownership.

## Professor's Buddy 1 requirements assessment - 10 November 2026

Reviewed the user-provided `C:/Users/wilso/Downloads/Project Write-up.pdf`,
all 11 pages, especially **Team Structure / Buddy 1: WiFi Communication,
Command, and Telemetry** on page 5. This source defines project requirements;
reviewing it does not authorize unrelated changes to other subsystems.

The communication foundation is largely implemented, but the assigned role
is not complete. Code and software tests are distinct from an integrated
robot demonstration and measured hardware acceptance.

| Professor's task | Current status | Evidence / remaining work |
| --- | --- | --- |
| Establish WiFi connectivity | Implemented; bench operation reported | Pico joins the configured hotspot; laptop/power-bank operation observed |
| Implement MQTT communication | Implemented | Radio owner publishes to Mosquitto; dashboard subscribes over WebSocket |
| Design communication topics | Implemented for current S1 scope | Telemetry, heartbeat, command and status-reply topics documented; no duplicate legacy formats |
| Define telemetry message structures | Implemented for current data | One 11-byte packed frame and five-byte binary heartbeat; range/validity checks in C and JavaScript |
| Implement telemetry publishing | Implemented; real bench readings reported | Distance, line IR and IMU/height-estimator fields; final integrated-run evidence still needed |
| Implement command subscription | Read-only STATUS implemented and functionally tested | Real command subscription replaces heartbeat loopback. Binary validation, correlated reply, bounded processing and browser timeout work. Controller-owned STOP/other permitted actions remain separate pending integration |
| Implement connection recovery | Communication restored after confirmed flash; recovery acceptance open | Earlier hotspot runs retained, but latest user clarification leaves their running-image identity uncertain. Confirmed-flash telemetry/STATUS/USB work; a clean hotspot series and strict interval evidence remain open |
| Implement heartbeat/status reporting | Heartbeat and read-only communication status implemented | Reply reports actual boot uptime, build mode and fresh available sensor snapshot. Overall controller state still awaits its interface |

The four listed deliverables are communication API, telemetry framework,
MQTT documentation and a telemetry demonstration. The public S1 API,
packed telemetry framework, dashboard README and real STATUS/telemetry
checks cover their foundations. Controller-action integration and the final
integrated demonstration remain to complete them.

The PDF labels robot state, speed, encoder counts, line status, barcode,
hump, obstacle scans and travelled distance as **Telemetry Examples**.
Agree the final field set with the team as real producer interfaces become
available; the examples are not eight separately stated mandatory S1 tasks.
The overall mission does require reporting the highest hump peak. Current
height estimates still need accuracy validation with S4/encoder inputs.

The write-up does not specify the earlier 2-second telemetry, 5-second
heartbeat or 15-second restoration acceptance thresholds. Those are earlier
design/test-plan targets and must not be attributed to this PDF. Change
Requirement 1 is an additional source: its packed-data and evidence work
still applies, including outstanding physical performance/RAM evidence.

Read-only STATUS is now implemented. Next priorities: agree and implement
the permitted actuator/controller handoff and publish useful controller status.
While that interface remains undefined, recovery code/tests have been prepared
and five broker plus five hotspot trials recorded. Broker recovery passed its
earlier design target; hotspot timing did not. Retest the now-flashed MQTT
timeout correction and complete integrated evidence. Add other real measurements when
their owning subsystems provide valid snapshots; avoid fabricated values.

## Current STATUS milestone and deferred IMU case

Read-only request/reply works from the website through MQTT to the real
Pico. Normal telemetry stays at 11 bytes and heartbeats at five bytes. A
STATUS request is five bytes; its response is 22 bytes with a fresh snapshot
or 11 bytes without one. The final captured matching reply took 29 ms;
the user also confirmed the website's Request status button shows success.
This is not a repeated performance/recovery acceptance pass.

An initial live check exposed calendar time being labelled as uptime. Only
S1 reporting was corrected to `tk_get_otm`; sensor age comparisons retain
the existing producer clock domain. The first capture is preserved as the
transport check with the incorrect uptime; the final capture verifies the
corrected operating-time field. No IMU algorithm was changed in this stage.

The old unconnected ASCII STOP/STATUS parser was removed. STOP remains
unimplemented and disabled. Current communication status does not claim
that an unknown vehicle/controller state is Ready, stopped or fault-free.

The user wants to finish Buddy 1 first. Defer the professor's reported
one-wheel/curb-like IMU test case to Buddy 4's algorithm review. S1 continues
transporting actual provided values, validity and status. Do not infer height
accuracy from a hand tilt or the nominal-speed estimate.

## Recovery preparation - 10 November 2026

See [the recovery test guide](testing/subsystem1/RECOVERY_TESTS.md) for the
exact later procedure. No broker or hotspot was stopped, no UF2 was flashed
and no live recovery pass was recorded in this stage. The initial baseline
subscriber connected to the broker but saw no telemetry; the user confirmed
the car was unavailable, so this is not evidence of a firmware failure.

WiFi DOWN (0) and failure states now retry after five seconds without needing
a status change. Initial join-request rejection no longer parks a healthy
driver. MQTT closes stale TCP and cancels pending requests/timers on WiFi/IP
loss; immediate connect/allocation/address errors back off three seconds
instead of retrying every network poll. Positive JOIN/DHCP progress is left
to the driver. Wire formats and IMU/controller behavior are unchanged.

Offline checks passed: 13 retry-policy checks, 30 actual MQTT-port checks,
six recorder checks, 32 S1 codec checks plus 20 queue/cache checks, 35 dashboard
tests and cross-language golden-frame checks. ARM build passed. These do not
prove radio recovery, sensor accuracy or the 15-second design target.

STOP still needs a latched controller-owned interface: the controller updates
motor duties continuously, so a one-time zero-duty call would be overwritten.
No actuator path was enabled while the team interface remains undefined.

Initial recovery UF2 used in the broker/hotspot trials: **707584 bytes**, SHA-256
`aaf0e39836b759904add31509ca4fef3a06a6892f6e58589b3cbf4342226fa77`.
Linked static RAM is **82612 bytes** (eight more than the preceding STATUS
build); requested S1 queue storage remains 192 bytes. Physical trials were
pending at preparation; the later broker results below supersede that status.
The [S1 test log](testing/subsystem1/TEST_LOG.md) has current evidence.

## Live broker follow-up - 10 November 2026

The user connected the car and reported flashing the current recovery UF2.
Fresh telemetry and a real correlated stationary-test STATUS reply were
observed before any interruption. Only the verified custom Mosquitto process
was restarted; the default Windows service, Pico power and hotspot stayed on.
The custom broker and local dashboard were left running afterward.

Five actual restarts recovered with bounds **4.415, 3.552, 3.318, 5.641 and
5.811 seconds** from a marker before broker restoration. All five returned
fresh telemetry, two heartbeats and STATUS with compatible Pico uptime.
This meets the earlier 15-second recovery design target for these broker
trials. It does not establish hotspot/AP recovery or exact Pico-only timing.

Observed post-recovery intervals were not all within the separate targets:
telemetry reached **2.111 s** in one trial; heartbeat reached **6.393 s**.
Only four/five short telemetry windows and two/five heartbeat windows met
the strict earlier 2 s/5 s targets. Keep those acceptance items open.

The recorder initially omitted baseline request/reply latency from uptime
continuity and could miss outbound MQTT keepalives while receiving telemetry.
Both recorder issues were corrected; eight offline tests passed. Original
captures/results are preserved. Trial 2's apparent uptime failure was
reassessed using both request/reply windows; no firmware change was involved.
One prepared baseline with no actual interruption is explicitly recorded
separately, rather than counted as a physical restart.

See [the five-trial summary](testing/subsystem1/logs/2026-11-10-broker-recovery-summary.json).
The later hotspot results below supersede the earlier pending status.

## Hotspot results and MQTT timeout correction - 10 November 2026

The user completed all five local-terminal trials. Three returned a matching
fresh STATUS without an indicated reboot, at **15.572, 18.015 and 20.484 s**
from the restoration marker. Trials 3 and 5 did not complete within the
30-second window. **None met the earlier 15-second design target.** These
are end-to-end observations, not isolated Pico/AP timing failures.

Broker logs later show telemetry for the two timed-out trials at roughly
32..33 s and 74..75 s after their markers. Keep those trials incomplete;
later receipt does not replace the missing within-window STATUS evidence.
Some heartbeats arrived together in a burst. Their tiny arrival intervals
do not prove the normal heartbeat cadence or freshness of queued telemetry.

The old Pico TCP session persisted during the outages, and the broker finally
expired it before accepting a new session. Firmware advertised a 60-second
MQTT keepalive. AP-on time, laptop association time and a Pico serial trace
were not captured, so the entire delay cannot be assigned to one component.

A focused port correction is now built: MQTT keepalive **5 s**, pending
TCP/MQTT connection deadline **5 s**, existing error backoff **3 s**. The
deadline reuses the existing retry timer and never closes a healthy accepted
session. Thirty-six actual-port host checks and the ARM build passed. This
does not prove the new image meets the physical timing target yet.

Current replacement UF2 remains the same filename and **707584 bytes**,
SHA-256 **9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0**.
Linked static RAM remains **82612 bytes**, with no new task/queue/state flag;
code grew 64 bytes. Sensor logic, binary schema and dashboard decoder are
unchanged. At preparation it was not flashed; the following baseline now
supersedes that status.

See [the hotspot summary](testing/subsystem1/logs/2026-11-10-hotspot-recovery-summary.json)
and [S1 test log](testing/subsystem1/TEST_LOG.md).

### Replacement live baseline before hotspot retest

The user reported ready after the requested flash. Broker logs show a new
Pico MQTT session with **k5**, consistent with the replacement configuration.
A 60-second capture through the actual dashboard WebSocket adapter received
**118 telemetry messages, 13 heartbeats and three matching STATUS replies**
(103, 207 and 1505 ms). All replies reported TEST mode and a fresh snapshot;
zero packets were rejected, with one sequence gap observed. The Pico MQTT
session did not restart during the capture.

Maximum arrival intervals were **2.007 s telemetry and 6.362 s heartbeat**.
Normal communication is established, but strict earlier interval targets
remain open. This is a baseline check, not a hotspot recovery pass.

An allowlisted USB serial/laptop-interface capture was started for up to
20 minutes for the next retest. It records link/MQTT events and laptop
interface changes without SSIDs/passwords, and performs no reset/network
changes. Its timestamps remain observation proxies, not AP-beacon times.
The initial serial drain included **subsystem1 SUMMARY 33 passed, 0 failed**;
it can be buffered boot output, so this is not a new controlled cold-boot test.
Next: repeat the five local-terminal hotspot trials with this image.

### Follow-up review and firmware-identity correction

Three additional recorder files were found. An earlier partial run recorded
two passes and one timeout; one startup-only file has no saved trials. A
complete series after the user's laptop restart recorded **four of five**
under 15 s: **7.532, 6.668, 7.170 and 9.096 s**, with trial 5 incomplete at
30 s. All eight recorded trials are retained (six passes, two timeouts),
across different/restarted conditions. Do not present only the better run
as proof of uniform reliability.

The laptop restart terminated the extra diagnostic capture. It contains
laptop-interface transitions for the earlier partial run, but no Pico serial
events during those outages and no trace for the latest complete series.
Its missing end marker/coverage is documented, not treated as a full trace.

After the final timeout, a read-only check reached the broker but received
no car messages; COM5 was present/OK in Windows yet could not be opened.
The user confirmed laptop USB power and hotspot connectivity. Before any
new code change, the user clarified that the intended UF2 had not been
flashed, then explicitly reported flashing it. Therefore the exact running
image in the preceding tests is **unconfirmed**, despite local-file hashes
and the previously observed k5 configuration. Raw results are unchanged.

After that confirmed flash/restart, a new 20-second check observed **37
telemetry messages, three heartbeats and matching STATUS in 81 ms**. The USB
console opened again and yielded **33 passed, 0 failed**, plus WiFi/MQTT-up
messages. Initial console output can be buffered; this is not independent
flash readback or a controlled cold-boot timing test. Recovery after flashing
cannot distinguish a firmware effect from the reset itself.

No firmware/source change or new UF2 was made during this review. Current
file is still **9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0**.
Normal communication now works; one clean hotspot series after this confirmed
flash is still required. See [the follow-up summary](testing/subsystem1/logs/2026-11-10-hotspot-corrected-retest-summary.json)
and [post-flash check](testing/subsystem1/logs/2026-11-10-hotspot-post-confirmed-flash-check.json).

## Dependencies and decisions before later milestones

| Area | Work needed |
| --- | --- |
| S2 motor/encoder owner | Provide actual measured left/right speed and cumulative distance with validity/timestamps. PWM duty is not speed. |
| S3 IR/barcode owner + controller | Confirm physical black level and mounting; agree barcode character-to-navigation mapping. The existing barcode queue has a single consumer; S1 must not steal controller events. |
| S4 IMU owner | Review the snapshot/API fixes in this integration. Live pitch, source timestamps, protected publication and speed provenance are now implemented; physical pitch/height validation and encoder-speed integration remain. |
| S5 ultrasonic/servo owner | Distance works through temporary bench route; servo sweep/profile and controller routing remain separate work. |
| Controller integration | Define a coherent snapshot, run state, faults and permitted command handling. MQTT task keeps radio ownership; motor/servo tasks keep actuator ownership. |

## Acceptance targets and evidence still required

Previously identified project/design targets: telemetry interval at most
2 seconds, heartbeat interval at most 5 seconds, and restoration within
15 seconds in at least four of five trials. Verify these against the final
agreed group requirements and measure them under the intended conditions.

Browser reconnection timing is **not** a measurement of Pico WiFi recovery.
Record AP loss, broker loss, invalid commands, malformed/stale packets and
recovery separately. Agree permitted command vocabulary with the controller;
the implemented STATUS path is read-only; controller-owned actuator commands
remain to be agreed and integrated.

For the change requirement, record the field/bit/units/validity table,
boundary and wrap tests, equivalent JSON/binary application payload bytes,
and measured before/after firmware RAM with consistent build flags. This
IR update replaces preformatted outbound buffers with smaller snapshots:
requested output message-buffer storage drops from 544 to 112 bytes for
the current 32-bit layout (432 bytes less). This is a buffer-size calculation,
**not** a measured whole-firmware RAM result. Kernel object overhead and
other dynamic allocations are outside that comparison.

## Development validation and restart reference

- [Dashboard README](dashboard/README.md): startup, current schema and IR field.
- [S1 test log](testing/subsystem1/TEST_LOG.md): cases, development evidence
  and remaining physical trials.
- [Host source checks](testing/subsystem1/logs/2026-11-06-ir-host-tests.txt).
- [Dashboard checks](testing/subsystem1/logs/2026-11-06-ir-dashboard-tests.txt).
- [Firmware build](testing/subsystem1/logs/2026-11-06-ir-firmware-build.txt).

These are local development checks against a working copy. Preserve a source
fingerprint, then rerun the formal checks after committing for submission.
Browser visual QA is unavailable in this session because the browser-control
host is unavailable; no screenshot was captured. The functional v2 IR/distance/restart passes are user-reported, not independently timed.

The subsequent timer-startup patch adds one Pico-only check beyond the earlier
20 S1 host checks. It built successfully, and the 29 host checks still pass.
See [timer build output](testing/subsystem1/logs/2026-11-06-timer-startup-build.txt).
It is a shared board-startup change, separate from the S1 telemetry feature,
and needs team review. Normal laptop cold-boot evidence and later power-bank
reading evidence are saved in the S1 log. This is development validation,
not a commit-pinned recovery-performance pass.

No ChatGPT hosting is used. WiFi credentials stay in ignored
`config/wifi_secrets.h`; do not include them in reports or logs.
