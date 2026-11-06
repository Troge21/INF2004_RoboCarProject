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
   **Current firmware JSON**, and leave broker username/password empty.

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
| Decoder | Auto or Current firmware JSON |

These topics match `config/wifi_secrets.example.h`. Change them if your firmware overrides the client ID/topics. The dashboard does not read your secrets file. Username/password, if supplied, are held only in tab memory for reconnects; input fields are cleared when the settings close. Disconnect using **Disconnect** in the connection settings.

Build/flash the WiFi/MQTT firmware (`NET=lwip MQTT=1 TEST=1` for stationary
bench testing; normal driving is disabled). The updated bench route sends:

```json
{"sequence":12,"obstacle_mm":451,"obstacle_valid":true,"line_bits":6}
```

Failed ultrasonic readings use `obstacle_mm:null, obstacle_valid:false`;
IR validity is independent. Older distance-only JSON still works, with IR
unavailable. Heartbeats use `hb 0`, `hb 1`, etc. Only a successful MQTT
connection **and both accepted subscriptions** count as broker connected.
Robot presence is inferred separately from fresh messages. Retained messages
are rejected. Duplicate/backward sequences are rejected; wraparound is
supported. A decreasing heartbeat counter resets the sequence baseline after
a possible robot reboot.

### Centre/right line sensor field

`line_bits` packs three logical bits: bit 2 = snapshot valid, bit 1 = centre
detects black, bit 0 = right detects black. It is an integer in JSON:

| Value | Centre | Right | Meaning |
| --- | --- | --- | --- |
| `0` | Unavailable | Unavailable | Missing, busy, invalid or stale snapshot |
| `4` | Background | Background | Valid all-background reading |
| `5` | Background | Black line | Right detects black |
| `6` | Black line | Background | Centre detects black |
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

The **left GP26 input is reserved for barcode capture**, not included in
centre/right line bits. Overview shows C/R only; Sensors & motion labels the
left input accordingly. Unknown readings display `?`, distinct from measured
background. CSV exports include `ir_centre` and `ir_right`; unavailable values
are blank. Snapshot validity does not independently detect a disconnected or
miswired digital sensor.

For the physical bench check, flash the updated UF2 and test all four
centre/right black/background combinations against values 4, 5, 6, 7 above.
Then disconnect the car and verify both live indicators become unavailable.
Do not mark this test passed until the real hardware behavior is observed.

## Integration contract

Live mode accepts the original distance-only JSON and the updated `line_bits`
field. It never invents missing measurements. To populate the remaining charts
later, the JSON decoder also accepts these optional fields:

| Field | Unit / accepted representation |
| --- | --- |
| `left_mps`, `right_mps` | Finite speed in m/s, −5..5 (viewer sanity bounds; team must agree actual physical limits) |
| `distance_m` | Nonnegative finite accumulated distance, metres |
| `state` | `Ready`, `Following line`, `Turning left`, `Turning right`, `Scanning obstacle`, `Avoiding obstacle`, `Recovering line`, `Stopped`, `Fault` |
| `ir` | Legacy `[left, centre, right]` array, each 0, 1 or null; left is ignored because it is reserved for barcode. `line_bits`, when present, takes precedence. |
| `barcode` | `Turn left`, `Turn right`, `Go straight`, `U-turn`, or null |
| `hump_mm`, `max_hump_mm` | Nonnegative finite height in millimetres, ≤1000 (viewer sanity bound) |
| `faults` | Array of short fault descriptions; an empty array explicitly means no reported fault |
| `scan` | At most 181 `{angle, distance_mm}` entries; angle 0..180°, distance 20..4000 mm or null |

These JSON fields support incremental bench integration; **they are not the
final protocol for the change requirement**. The line flags use three logical
bits, but this MQTT payload is still JSON and the full application state is
not yet packed. The final binary schema, physical limits and controller
interfaces still require team integration. This update changes S1 code and
uses S3's existing read interface; it does not change S3 drivers or the kernel
network service.

## Proposed compact distance adapter

The browser includes a testable, **proposed** six-byte distance-only decoder. Firmware does not emit it yet. It is useful for reviewing the packing approach, not evidence that the firmware change is complete.

| Bits | Meaning | Range |
| --- | --- | --- |
| 0..31 | Sequence, little-endian | 0..4294967295, modular wrap |
| 32..43 | Obstacle distance, millimetres | 20..4000 when valid; zero otherwise |
| 44 | Measurement valid | 0 or 1 |
| 45..47 | Protocol version | 1 for this proposed frame |

The final two bytes are a little-endian word: `(1 << 13) | (valid << 12) | distance`. Example sequence 12 and valid 451 mm: **`0c 00 00 00 c3 31`**. Invalid range: clear the distance and validity bits. The comparable JSON example is 55 UTF-8 bytes; the proposed binary example is 6 bytes. These are calculated application-payload sizes, excluding MQTT and network headers. This is not a measured RAM reduction, nor a finalized whole-vehicle protocol.

## Validation

```powershell
npm test
```

The Node tests cover the actual firmware JSON, no-echo handling, malformed inputs, binary golden vectors, sequence wrap/replay/reboot handling, bounded buffers, MQTT packet fragmentation, preservation of raw binary bytes, retained packets, subscription readiness and connection failures.

Bench checks still required: real Pico-to-browser telemetry, broker outage/recovery, timing trials, full-team sensor integration, and the final binary firmware schema. Reconnection figures in the browser measure browser socket/subscription recovery, not the Pico's WiFi restoration requirement. Firmware RAM fields remain explicitly unmeasured.
