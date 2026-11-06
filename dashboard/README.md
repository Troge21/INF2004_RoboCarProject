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
   settings below. For the local bench, use `ws://127.0.0.1:9001`, select
   **Auto** or **Packed v2**, and leave broker username/password empty.

An optional PowerShell diagnostic subscriber is:

```powershell
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 172.20.10.2 -p 1883 -t "mtk3smp-pico/#" -v
```

It waits silently when no messages arrive. It is not needed by the dashboard.
Use the same WiFi address as the car-facing listener, rather than the separate
broker on `127.0.0.1:1883`. Stop each foreground server/subscriber with Ctrl+C.

## Features

- Overview: latest readings, two wheel-speed traces, obstacle-distance history, robot status, digital IR indicators, faults and activity feed.
- Sensors & motion: wheel speed, ultrasonic distance, hump height, run maximum, cumulative distance, IR readings and a servo scan profile.
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
| Decoder | Auto or Packed v2 |

These topics match `config/wifi_secrets.example.h`. Change them if your firmware overrides the client ID/topics. The dashboard does not read your secrets file. Username/password, if supplied, are held only in tab memory for reconnects; input fields are cleared when the settings close. Disconnect using **Disconnect** in the connection settings.

Build/flash the WiFi/MQTT firmware (`NET=lwip MQTT=1 TEST=1` for stationary
bench testing; normal driving is disabled). The newly built firmware publishes
**six-byte packed v2 telemetry** and **five-byte binary heartbeats** on the
same topics. Select **Auto** or **Packed v2**, rather than Legacy JSON.
For sequence 12, distance 451 mm and left line black/right background, the
actual payload is:

```text
0c 00 00 00 c3 e1
```

The dashboard decodes it into the same logical measurements previously sent
as JSON. Distance code zero means unavailable and is displayed as unavailable,
not as a measured 0 mm. IR validity is independent. Missing future measurements
remain unavailable. The default Auto decoder also accepts the previous JSON
firmware and text heartbeats, so you can update the website before flashing.

Only a successful MQTT connection **and both accepted subscriptions** count
as broker connected. Robot presence is inferred separately from fresh
messages. Retained packets are rejected. Duplicate/backward sequences are
rejected, wraparound is supported, and a decreasing heartbeat counter resets
the sequence baseline after a possible robot reboot. The decoded inspector
shows readable measurements/counters; **Raw bytes** shows hexadecimal.

The packed image has been built and tested offline. On 6 November, the user
reported working line bits and six-byte/five-byte messages in the real
dashboard. See the [bench observation](../testing/subsystem1/logs/2026-11-06-cr1-user-bench-observation.md).
Packed-image distance, normal restart, unavailable-state and timing checks
still need recorded results. Earlier captured hardware tests used JSON.

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
It occupies bits 12..14 of the final packed word. Legacy JSON represents
the same three-bit value as an integer:

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

## Integration contract

Current packed v2 carries distance and line IR only. Add later measurements
by defining their ranges/bit widths and extending the versioned binary
contract in both C and JavaScript. Do not revert the firmware to text simply
to populate more graphs. For backward compatibility, the JSON decoder still
accepts the following optional fields from older/development firmware; they
are not fields present in the new six-byte frame:

| Field | Unit / accepted representation |
| --- | --- |
| `left_mps`, `right_mps` | Finite speed in m/s, −5..5 (viewer sanity bounds; team must agree actual physical limits) |
| `distance_m` | Nonnegative finite accumulated distance, metres |
| `state` | `Ready`, `Following line`, `Turning left`, `Turning right`, `Scanning obstacle`, `Avoiding obstacle`, `Recovering line`, `Stopped`, `Fault` |
| `ir` | Legacy array `[GP26, GP6, GP16]`, historically named `[left, centre, right]`, each 0, 1 or null. The GP26 slot is ignored for line sensing; GP6 is displayed as Left IR. `line_bits`, when present, takes precedence. |
| `barcode` | `Turn left`, `Turn right`, `Go straight`, `U-turn`, or null |
| `hump_mm`, `max_hump_mm` | Nonnegative finite height in millimetres, ≤1000 (viewer sanity bound) |
| `faults` | Array of short fault descriptions; an empty array explicitly means no reported fault |
| `scan` | At most 181 `{angle, distance_mm}` entries; angle 0..180°, distance 20..4000 mm or null |

## Current compact telemetry contract

The implemented v2 frame is sequence:32, distance:12 (zero unavailable),
line flags:3 and format marker:1, totaling six bytes. It uses explicit
little-endian byte operations, not a C struct copied onto the network.

The heartbeat is `80` followed by a four-byte little-endian counter. Its
marker makes it unambiguous with old ASCII `hb N` messages; the application
counter retains all 32 bits. The browser retains the old proposed distance-only
v1 decoder for compatibility, but v1 was never this firmware's MQTT baseline.

See [Change Requirement 1 implementation and evidence](../testing/subsystem1/CHANGE_REQUIREMENT_1.md)
for bit positions, shared-data safety, RAM/message comparisons, tests and
remaining team work. Example telemetry shrank **69 to 6 bytes**. Requested
S1 queue buffers shrank **192 to 144 bytes**, preserving four slots after
kernel alignment. Linked static RAM is **82740 bytes before and after**;
this is not a whole-device heap/stack high-water measurement.

This completes the code conversion of the current telemetry path, not the
entire group requirement. Other subsystem application state, controller/fault
integration, binary supervisory commands and physical timing evidence remain.
S3 GPIO code and steering are unchanged; S5 only adapts its S1 handoff.

## Validation

```powershell
npm test
```

The Node tests cover the actual firmware JSON, no-echo handling, malformed inputs, binary golden vectors, sequence wrap/replay/reboot handling, bounded buffers, MQTT packet fragmentation, preservation of raw binary bytes, retained packets, subscription readiness and connection failures.

Bench checks still required for packed v2: complete distance/IR and unavailable-state evidence, normal cold boot/power-bank restart, broker outage/recovery, timing trials, full-team sensor integration, and the final binary firmware schema. Reconnection figures in the browser measure browser socket/subscription recovery, not the Pico's WiFi restoration requirement. Runtime heap/stack RAM remains unmeasured; linked static RAM and queue-buffer comparisons are in the implementation report.
