# Temporary progress and future plans - Subsystem 1

Last updated: **6 November 2026** (Asia/Kuala_Lumpur).

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
| 6 Nov 2026 | Fix BOOTSEL-dependent microsecond clock startup and check normal operation | Fix flashed; normal laptop and power-bank readings observed | Explicit 1 MHz tick on every boot. Laptop cold boot: S1 21/21, 24/24 valid distances (162..163 mm). Later power-bank capture: 24/24 valid distances (117..147 mm), 3 heartbeats. Initial power-bank windows had no messages; recovery timing remains unmeasured. Wiring unchanged |
| Separate hardware follow-up | Review HC-SR04 voltage/interface requirements | Open; not established as the cause of boot-dependent failure | User confirms VCC on 3V3 and ECHO directly to GP5, with no resistors supplied. Standard module is specified for 5 V; never raise supply to 5 V with ECHO direct. First evaluate startup fix without changing wiring |
| Next | Verify IR image and test all physical patterns | Pending | Confirm image revision; centre/right states agree with black tape and white surface; unplug car and observe unavailable values |
| Following | Validate and integrate IMU/hump telemetry | Pending | Confirm getter correctness, estimator validity and encoder speed dependency with S4 owner |
| Following | Integrate measured wheel speeds and distance | Waiting for S2 interface | Normal firmware currently has no encoder feedback; duty percentage is not measured speed |
| Following | Agree controller telemetry and command interfaces | Pending team integration | Move temporary bench route to agreed controller snapshots; agree allowed commands and response semantics |
| Following | Complete packed-data change requirement | Pending | Agreed bit widths/units/validity/version, synchronized state, binary firmware/dashboard agreement and before/after evidence |
| Following | Implement permitted supervisory commands | Pending | Validated, bounded command routing to controller, acknowledgements, expiry/replay handling and controller-owned execution |
| Following | Test WiFi/MQTT loss and recovery | Pending | Record distinct car-side recovery and browser-side recovery; five timed trials |
| Final | Collect submission evidence and documentation | Pending | Commit identity, logs, timing results, protocol/representation tables, memory/message comparison and group demo evidence |

## What works now

- The Pico connects over WiFi to Mosquitto running on the laptop.
- Mosquitto receives car MQTT on the laptop WiFi address, port 1883, and
  serves the local browser over WebSockets at `127.0.0.1:9001`.
- The local website is served at `http://127.0.0.1:4173`.
- Real ultrasonic distance and heartbeats were observed end to end.
- The dashboard rejects retained/invalid/replayed telemetry, tracks freshness,
  preserves bounded received history and exports real data.
- IR-capable firmware is now observed sending fresh centre/right snapshots
  (`line_bits:4`) from the real Pico. **The four physical IR patterns still
  need testing, and the exact flashed image hash has not been confirmed.**
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

Current image: `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
Build flags: `NET=lwip MQTT=1 TEST=1`. With the current configuration,
TEST disables normal driving and encoder calibration remains off.

## Current IR milestone

Data path:

```text
S5 distance sample -> S1 input queue
                       + S3 protected centre/right snapshot
                    -> S1 output queue (raw snapshots)
                    -> freshness check + JSON formatting in network poll
                    -> existing MQTT service -> Mosquitto -> dashboard
```

Example updated payload:

```json
{"sequence":12,"obstacle_mm":451,"obstacle_valid":true,"line_bits":6}
```

| `line_bits` | Centre | Right |
| --- | --- | --- |
| 0 | Unavailable | Unavailable |
| 4 | Background | Background |
| 5 | Background | Black |
| 6 | Black | Background |
| 7 | Black | Black |

Bit 2 is validity; bit 1 is centre; bit 0 is right. Bits are stored in one
unsigned byte locally and represented as a number in JSON. The source
timestamp remains local metadata. S1 clears IR validity if a snapshot is
older than 100 ms when collected or serialized. All browser current values
expire after 2 seconds without fresh telemetry or on disconnect. The left
GP26 input remains barcode-only. Validity means a successful recent snapshot;
it cannot establish that an individual digital sensor is correctly wired.

The final change requirement remains open: most fields and MQTT messages
have not yet been converted to the agreed minimum-bit binary representation.

## Physical check to run next

- [ ] Start the same hotspot (iPhone Maximize Compatibility enabled).
- [ ] Confirm the laptop WiFi IPv4 matches `wifi_secrets.h` and
      `dashboard/mosquitto.local.conf`.
- [ ] Start the custom Mosquitto broker and local dashboard server.
- [x] Observe IR-capable firmware sending real `line_bits` messages.
- [ ] Confirm the board uses the current built IR UF2; reflash if needed.
- [x] Confirm the current on-device S1 summary: **21 passed, 0 failed**,
      including hardware timer check S1-U43 after a normal power-on.
- [ ] With both line sensors on white/background, observe `line_bits:4`.
- [ ] Centre on black only: observe `line_bits:6` and centre indicator filled.
- [ ] Right on black only: observe `line_bits:5` and right indicator filled.
- [ ] Both on black: observe `line_bits:7` and both indicators filled.
- [ ] Confirm the left tile says "Reserved for barcode".
- [ ] Disconnect the car; within about 2 seconds plus the display refresh,
      observe unavailable line readings rather than false background.
- [ ] Reconnect, confirm fresh readings return, and export CSV with C/R values.
- [ ] Save serial/MQTT observations and append actual results to the test log.

Do not infer hardware success from host unit tests. Use a stationary bench
check first. Do not change another subsystem's GPIO or driver ownership.

## Dependencies and decisions before later milestones

| Area | Work needed |
| --- | --- |
| S2 motor/encoder owner | Provide actual measured left/right speed and cumulative distance with validity/timestamps. PWM duty is not speed. |
| S3 IR/barcode owner + controller | Confirm physical black level and mounting; agree barcode character-to-navigation mapping. The existing barcode queue has a single consumer; S1 must not steal controller events. |
| S4 IMU owner | Review snapshot synchronization and validity. Current getter assigns the baseline to `pitch_deg_x10`; verify live pitch before exposing it. Height estimation currently defaults to nominal speed until encoder speed is supplied. |
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
host is unavailable; no screenshot or physical IR pass has been claimed.

The subsequent timer-startup patch adds one Pico-only check beyond the earlier
20 S1 host checks. It built successfully, and the 29 host checks still pass.
See [timer build output](testing/subsystem1/logs/2026-11-06-timer-startup-build.txt).
It is a shared board-startup change, separate from the S1 telemetry feature,
and needs team review. Normal laptop cold-boot evidence and later power-bank
reading evidence are saved in the S1 log. This is development validation,
not a commit-pinned recovery-performance pass.

No ChatGPT hosting is used. WiFi credentials stay in ignored
`config/wifi_secrets.h`; do not include them in reports or logs.
