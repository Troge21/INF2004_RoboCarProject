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
| Following | Implement permitted supervisory commands | Pending | Validated, bounded command routing to controller, acknowledgements, expiry/replay handling and controller-owned execution |
| Following | Test WiFi/MQTT loss and recovery | Pending | Record distinct car-side recovery and browser-side recovery; five timed trials |
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
  power-bank restart checks passed. These results do not validate v3 yet.
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

Current IMU/v3 build, rebuilt 10 November; not flashed by the agent:
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
The identical `_imu` alias, older UF2 variants and obsolete baseline ELF/map
copies were removed on 6 November at the user's request. Historical hashes,
measurements and logs remain. The single current UF2 contains v3, and its
hash was verified against the IMU build record. Refresh the dashboard after flashing; only Packed v3 is now supported.
Expect 33 S1 checks
on-device, including the hardware clock check. The new board summary has not been captured; the user reports the earlier
apparent IMU failure was a setup issue and it now works as intended. The dashboard no longer accepts earlier formats. The 10 November UF2
was rebuilt from the latest pulled barcode/line source; the user reports
the previous apparent failure was a setup issue. The rebuilt image still
needs flashing before its physical behavior can be assessed.
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
| Design communication topics | Telemetry/heartbeat topics implemented | Agree and document the actual command topic when adding that path |
| Define telemetry message structures | Implemented for current data | One 11-byte packed frame and five-byte binary heartbeat; range/validity checks in C and JavaScript |
| Implement telemetry publishing | Implemented; real bench readings reported | Distance, line IR and IMU/height-estimator fields; final integrated-run evidence still needed |
| Implement command subscription | Not implemented as a live command path | MQTT currently subscribes to its heartbeat for loopback diagnostics. STOP/STATUS parser has local tests only; dashboard buttons remain disabled. Agree allowed requests, subscribe and validate, then route through the controller |
| Implement connection recovery | Mechanisms implemented; acceptance testing incomplete | WiFi rejoin and MQTT retry exist. Record controlled AP/broker outage and restoration trials; browser recovery is a separate measurement |
| Implement heartbeat/status reporting | Heartbeat implemented; vehicle status partial | Five-second heartbeat and IMU status exist. Overall robot/controller state is not yet in the frame |

The four listed deliverables are communication API, telemetry framework,
MQTT documentation and a telemetry demonstration. The public S1 API,
packed telemetry framework, dashboard README and bench observations cover
their foundations. Command/controller integration and the final integrated
demonstration remain to complete them.

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

Next priorities: agree and implement the permitted command/controller
handoff, publish useful controller status, then complete outage/recovery
and integrated demonstration evidence. Add other real measurements when
their owning subsystems provide valid snapshots; avoid fabricated values.

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
the existing STOP/STATUS parser alone does not execute commands.

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
