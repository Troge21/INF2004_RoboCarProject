# RoboCar Mission Control

A browser dashboard for the INF2004 RoboCar project. It has three views: Overview, Sensors & motion, and Communications. It runs without npm dependencies, a cloud account, or a build step.

## Run on your laptop

Install Node.js if it is not already available. From the project root, run:

```powershell
cd dashboard
npm start
```

Open **http://127.0.0.1:4173**. The dashboard starts disconnected, with empty charts and unavailable measurements. It displays only data received from the configured RoboCar MQTT topics. All assets are local. Stop the server with Ctrl+C.

`node server.mjs` is equivalent to `npm start` if PowerShell prevents running npm.ps1.

## Restart the real-car bench on Windows

The flashed firmware remains on the Pico after power-off. Rebuild/reflash only
after changing firmware or its WiFi/broker settings.

1. Connect the laptop to the car's configured WiFi. For the iPhone hotspot,
   keep **Maximize Compatibility** enabled. Run `ipconfig`: the WiFi IPv4
   must match `MQTT_BROKER_IP` in `config/wifi_secrets.h` and the car-facing
   listener address in `dashboard/mosquitto.local.conf` (currently `172.20.10.2`).
2. In a Windows PowerShell terminal at the **repository root**, run:

   ```powershell
   & "C:\Program Files\mosquitto\mosquitto.exe" -c ".\dashboard\mosquitto.local.conf" -v
   ```

   Leave it open. This broker listens on the WiFi address at port 1883 and on
   `127.0.0.1:9001` for the browser. The separate default Windows service on
   `127.0.0.1:1883` is not the bench broker.
3. In a second terminal at the repository root, run `cd dashboard`, then
   `npm start` (or `node server.mjs`). Skip this if the dashboard server is
   already running. Leave it open.
4. Power the Pico normally, **without BOOTSEL**. Open
   **http://127.0.0.1:4173**, click **Connect robot**, and use the connection
   settings below. For the local bench, use `ws://127.0.0.1:9001` and leave
   broker username/password empty. Packed v3 is fixed automatically; there
   is no decoder selector.

An optional PowerShell diagnostic subscriber is:

```powershell
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 172.20.10.2 -p 1883 -t "mtk3smp-pico/#" -v
```

It waits silently when no messages arrive. It is not needed by the dashboard.
Use the same WiFi address as the car-facing listener, rather than the separate
broker on `127.0.0.1:1883`. Stop each foreground server/subscriber with Ctrl+C.

## Rebuild firmware after pulling changes

From an Ubuntu/WSL terminal at the repository root (the SDK path below is
this laptop's existing installation):

```sh
make -B -C build_make PICO_SDK_PATH=/opt/robocar/pico-sdk-2.2.0 NET=lwip MQTT=1 TEST=1 -j8
```

Flash only `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
`-B` recompiles all objects, including newly pulled files, and avoids reusing
objects from another build profile. `TEST=1` is the current stationary
sensor/communication build: it deliberately disables automatic driving.
Pulling Git source alone does not update the Pico or the existing UF2.

## Features

- Overview: latest readings, two wheel-speed traces, obstacle-distance history, robot status, digital IR indicators, faults and activity feed.
- Sensors & motion: wheel speed, ultrasonic distance, live IMU pitch/status, hump estimates and their speed assumption, run maximum, cumulative distance, IR readings and a servo scan profile.
- Communications: delivery intervals, heartbeat age, subscriptions, payload sizes, sequence gaps, rejected messages, decoded/raw packet inspector and recent messages.
- Last 30 / 60 / 120 seconds, chart hover values, pause display while continuing collection, and CSV export of received telemetry with `source=mqtt`.
- **Command buttons stay disabled** until firmware integration: the current firmware does not route received commands to the controller. The dashboard sends no motor commands.
- Missing fields show unavailable. Invalid ultrasonic readings produce gaps, rather than zero or clear space. Live current values become unavailable after 2 seconds without telemetry; historical charts remain visible.
- Memory is bounded to 600 telemetry samples, 30 packets and 80 events. Nothing is saved to browser storage. Exports contain telemetry, not broker credentials.

## Connect to the current firmware

The Pico firmware uses MQTT over TCP. A browser uses MQTT over **WebSockets**, so the broker needs an additional WebSocket listener. Both listeners belong to the same broker; the Pico continues using its existing TCP port.

For an isolated local bench broker, a Mosquitto configuration can contain the following. Adapt binding addresses and your existing authentication configuration to your setup; do not replace a working broker configuration blindly.

```conf
# Existing Pico-facing listener; use your laptop's actual LAN address.
listener 1883 <LAPTOP_LAN_IP>
protocol mqtt

# Browser on this same laptop only.
listener 9001 127.0.0.1
protocol websockets

# Local development only. Keep this bench broker on a trusted network.
allow_anonymous true
```

Use the locally served dashboard, click **Connect robot**, and enter:

| Setting | Default |
| --- | --- |
| Broker URL | `ws://localhost:9001` |
| Telemetry topic | `mtk3smp-pico/telemetry` |
| Heartbeat topic | `mtk3smp-pico/heartbeat` |
| Telemetry format | Packed v3, fixed automatically (11 bytes) |

These topics match `config/wifi_secrets.example.h`. Change them if your firmware overrides the client ID/topics. The dashboard does not read your secrets file. Username/password, if supplied, are held only in tab memory for reconnects; input fields are cleared when the settings close. Disconnect using **Disconnect** in the connection settings.

Build/flash the WiFi/MQTT firmware (`NET=lwip MQTT=1 TEST=1` for stationary
bench testing; normal driving is disabled). The new IMU build publishes
**11-byte packed v3 telemetry** and **five-byte binary heartbeats** on the
same topics. Flash `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
This is the single current UF2; the identical `_imu` alias and obsolete
firmware variants were removed on 6 November at the user's request.
The dashboard now accepts only the current 11-byte telemetry and five-byte
binary heartbeats. There is no decoder selector. Reflash the current UF2
before using this dashboard if the car still runs older six-byte/JSON firmware.
After pulling website changes, refresh the browser so it loads the new scripts.

V3 adds live pitch, IMU state, last accepted hump and run maximum to the
unchanged distance/IR prefix. The dashboard shows a Vehicle pitch graph and
IMU & height status under Sensors & motion. Heights use nominal 150 mm/s
until encoder speed is supplied; their accuracy remains unverified. Zero
height codes mean no accepted hump yet, not a measured zero. Missing/faulted
or stale pitch is unavailable. See the [IMU integration and test guide](../testing/subsystem1/IMU_TELEMETRY.md).

For sequence 1, 451 mm, left black/right background, pitch +15.0 degrees,
last accepted height 12 mm, maximum 34 mm and monitoring at nominal speed:

```text
01 00 00 00 c3 e1 1b 64 88 14 00
```

Distance code zero still means unavailable, not a measured 0 mm. IR validity
is independent. Wheel speed and other future measurements remain unavailable.
Obsolete v1/v2/JSON telemetry and text heartbeats are rejected with a clear
current-firmware message; no compatibility decoder remains.

Only a successful MQTT connection **and both accepted subscriptions** count
as broker connected. Robot presence is inferred separately from fresh
messages. Retained packets are rejected. Duplicate/backward sequences are
rejected, wraparound is supported, and a decreasing heartbeat counter resets
the sequence baseline after a possible robot reboot. The decoded inspector
shows readable measurements/counters; **Raw bytes** shows hexadecimal.

The user reported functional v2 passes for distance/IR, stale readings,
normal laptop restart and power-bank restart. See the [bench observation](../testing/subsystem1/logs/2026-11-06-cr1-user-bench-observation.md).
On 10 November the user confirmed the apparent failure was a setup issue
and the system works as intended. The UF2 was then rebuilt from the pulled
barcode/line updates plus the current IMU code. The new rebuild has not been
flashed by the agent. Hardware accuracy and the exact running revision were
not independently captured.
Packet timing, outage recovery and height accuracy remain unmeasured.

### Left/right line sensor labels and field

The dashboard names the inputs by their current roles:

| Dashboard name | Pico GPIO | Existing firmware name |
| --- | --- | --- |
| Barcode IR | GP26 | Left/barcode input |
| Left IR | GP6 | Centre line input (`S3_IR_CENTRE_PIN`) |
| Right IR | GP16 | Right line input (`S3_IR_RIGHT_PIN`) |

This is a display-name mapping. GPIO assignments, steering behavior and
MQTT bits are unchanged. Existing firmware/test references to centre/right
still refer to the GP6/GP16 pair, displayed as left/right on the dashboard.

`line_bits` packs three logical bits: bit 2 = snapshot valid, bit 1 = left
line IR (GP6) detects black, bit 0 = right line IR (GP16) detects black.
It occupies bits 12..14 of the distance/IR word. In the decoded message
inspector it appears as the following readable integer:

| Value | Left IR (GP6) | Right IR (GP16) | Meaning |
| --- | --- | --- | --- |
| `0` | Unavailable | Unavailable | Missing, busy, invalid or stale snapshot |
| `4` | Background | Background | Valid all-background reading |
| `5` | Background | Black line | Right detects black |
| `6` | Black line | Background | Left line IR detects black |
| `7` | Black line | Black line | Both detect black |

Other values are rejected. S1 reads `subsystem3_get_line()` without waiting;
S3 retains ownership of the GPIO pins and its snapshot semaphore. S1 checks
the source timestamp against `APP_IR_MAX_AGE_MS` (100 ms) when gathering and
again when serializing for MQTT. Queued IR can expire without discarding a
still-fresh ultrasonic reading. IR timestamps are local freshness metadata,
not synchronized laptop timestamps. The dashboard displays sampled IR states
at the telemetry rate, approximately twice a second, rather than every 20 ms
sensor update. All current readings become unavailable on disconnect or after
2 seconds without fresh telemetry; received history remains available.

The **GP26 input is reserved for barcode capture**, not included in line
bits. Overview shows L/R only; Sensors & motion labels the inputs Barcode IR,
Left IR and Right IR. Unknown readings display `?`, distinct from measured
background. CSV exports include `ir_left` (GP6, formerly named `ir_centre`)
and `ir_right` (GP16); unavailable values are blank. Snapshot validity does
not independently detect a disconnected or miswired digital sensor.

For the physical bench check, flash the updated UF2 and test all four
left/right line IR black/background combinations against values 4, 5, 6, 7 above.
Then disconnect the car and verify both live indicators become unavailable.
Do not mark this test passed until the real hardware behavior is observed.

## Current measurement fields

| Decoded field | Meaning |
| --- | --- |
| `sequence` | 32-bit sample counter |
| `obstacle_mm`, `obstacle_valid` | 20..4000 mm; unavailable is null/false |
| `line_bits`, `ir` | Left GP6/right GP16 line states; GP26 remains barcode-only |
| `pitch_deg` | Fresh calibrated IMU angle, degrees; null if unavailable |
| `imu_state`, `imu_flags` | Sensor/calibration/hump status, separate from vehicle state |
| `imu_speed_source` | Nominal 150 mm/s or externally supplied speed |
| `hump_mm`, `max_hump_mm` | Last accepted estimate and run maximum; 6..80 mm or null |

Wheel speed, travelled distance, vehicle state, barcode navigation and servo
scan panels remain unavailable until real data is integrated. The latest
barcode improvements are included in the firmware, but barcode results are
still reported through the existing S3/controller interface and console;
S1 does not consume another task's result queue to fill a graph.

Add future fields by agreeing their ranges and updating this one binary
contract in firmware and JavaScript together. There is no JSON fallback.

## Current compact telemetry contract

The implemented v3 frame preserves the six-byte v2 prefix: sequence:32,
distance:12 (zero unavailable), line:3 and marker:1. It appends five bytes
with pitch:11, last height:7, maximum:7, S4 state:3, flags:6 and six reserved
zero bits. Total: **11 bytes**. Explicit little-endian operations define the
layout. See [the current bit table, storage comparison and test evidence](../testing/subsystem1/IMU_TELEMETRY.md).

The heartbeat remains `80` followed by a four-byte little-endian counter.
Its marker distinguishes binary from old ASCII `hb N`. The dashboard accepts only this current contract. Earlier decoder functions
and format-selection controls have been removed.

The [original Change Requirement 1 report](../testing/subsystem1/CHANGE_REQUIREMENT_1.md)
records the earlier like-for-like JSON-to-v2 conversion (example 69 to 6
bytes, queue storage 192 to 144 bytes). The added IMU fields/source timestamps
bring the current queue request to 192 bytes and linked static RAM to 82764
bytes. These are feature costs; peak heap/stack use is still unmeasured.

Other subsystem/controller data, binary commands and physical timing evidence
remain group work. S4 retains sensor ownership; the new snapshot handoff and
API changes need S4 owner review before team merge.

## Validation

```powershell
npm test
```

The Node tests cover the current binary contract, no-echo handling, malformed/obsolete formats, independent golden vectors, IMU fault isolation, sequence wrap/replay/reboot handling, bounded buffers, MQTT fragmentation, retained packets and subscription readiness. See [the 10 November update record](../testing/subsystem1/TEST_LOG.md).

Bench checks still required for packed v3: live pitch and sensor-state behavior, repeat distance/IR and unavailable-state checks, normal cold boot/power-bank restart, broker outage/recovery, timing trials, full-team sensor integration, and the final binary firmware schema. Reconnection figures in the browser measure browser socket/subscription recovery, not the Pico's WiFi restoration requirement. Runtime heap/stack RAM remains unmeasured; linked static RAM and queue-buffer comparisons are in the implementation report.
