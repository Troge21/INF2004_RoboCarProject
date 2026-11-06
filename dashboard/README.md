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

Build/flash the existing WiFi/MQTT firmware using the repository's instructions (`NET=lwip MQTT=1`). The current bench route provides only:

```json
{"sequence":12,"obstacle_mm":451,"obstacle_valid":true}
```

Failed readings are `{"sequence":13,"obstacle_mm":null,"obstacle_valid":false}`. Heartbeats use `hb 0`, `hb 1`, etc. Only a successful MQTT connection **and both accepted subscriptions** count as broker connected. Robot presence is inferred separately from fresh messages. Retained messages are rejected so a stale retained sample cannot make the robot appear live. Duplicate/backward sequences are rejected; wraparound is supported. A decreasing heartbeat counter resets the sequence baseline after a possible robot reboot.

A privately hosted HTTPS copy displays the same live dashboard. To use live data there, the broker needs a browser-reachable **wss://** endpoint and appropriate browser network access. The normal local **ws://** bench setup is intended for the locally served dashboard. No public broker or credentials are configured automatically.

## Integration contract

Live mode works with the existing three-field JSON above. It never invents the missing measurements. To populate the remaining charts later, the JSON decoder also accepts these optional fields:

| Field | Unit / accepted representation |
| --- | --- |
| `left_mps`, `right_mps` | Finite speed in m/s, −5..5 (viewer sanity bounds; team must agree actual physical limits) |
| `distance_m` | Nonnegative finite accumulated distance, metres |
| `state` | `Ready`, `Following line`, `Turning left`, `Turning right`, `Scanning obstacle`, `Avoiding obstacle`, `Recovering line`, `Stopped`, `Fault` |
| `ir` | Three values `[left, centre, right]`, each 0 or 1; 1 means black line |
| `barcode` | `Turn left`, `Turn right`, `Go straight`, `U-turn`, or null |
| `hump_mm`, `max_hump_mm` | Nonnegative finite height in millimetres, ≤1000 (viewer sanity bound) |
| `faults` | Array of short fault descriptions; an empty array explicitly means no reported fault |
| `scan` | At most 181 `{angle, distance_mm}` entries; angle 0..180°, distance 20..4000 mm or null |

These optional JSON fields support dashboard development; **they are not a final protocol for the change requirement**. The full firmware binary schema, physical limits and controller interfaces still require team integration. The dashboard does not modify any C source or kernel service.

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
