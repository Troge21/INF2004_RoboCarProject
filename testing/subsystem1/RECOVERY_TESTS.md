# S1 WiFi and MQTT recovery tests ? current v4

Prepared for the final hardware session. The current single UF2 is stationary
NET=lwip MQTT=1 TEST=1 APP_MODE=integrated, with Packed v4 main frames (46
bytes), diagnostics (40), optional scan chunks (19..56), five-byte heartbeats
and 16/62-byte STATUS replies. Old telemetry/STATUS/beacon decoders are gone.
No current-batch outage test has run. Follow [the combined checklist](FINAL_ACCEPTANCE.md).

The network owner retains WiFi DOWN/failure retries (5 s), MQTT error backoff
(3 s), keepalive and pending connection deadline (5 s). Application heartbeat
is now nominally 4 s and main telemetry 400 ms to leave delivery jitter margin.
Schedules and offline checks are not physical timing guarantees.

Historical results remain in [the test log](TEST_LOG.md): five broker restarts
recovered in 3.318..5.811 s. The confirmed-flash clean hotspot series on
11 November recovered 5/5 in 7.922..9.242 s. Its maximum main/heartbeat arrival
gaps were 2.080/5.086 s, leaving those strict targets open. Earlier failures
and image-unconfirmed runs are preserved. Those results belong to their
original v3 images, not the current v4 batch.

## What this checks

Can the powered Pico resume fresh telemetry, heartbeats and a correlated
read-only STATUS reply after losing the broker or hotspot, without a reboot?
Run **five broker trials and five hotspot trials separately**. A browser
reconnecting alone does not demonstrate that the Pico recovered.

The earlier design/test targets are telemetry at most 2 s, heartbeat at most
5 s and restoration within 15 s in at least four of five trials. These numeric
targets are not stated in the professor's Project Write-up PDF. Retain actual
timings and agree the final acceptance criteria with the team.

## Prepare the car and laptop later

1. Build the stationary profile with `NET=lwip MQTT=1 TEST=1 APP_MODE=integrated`
   using the [Pico build instructions](../../dashboard/README.md#build-and-final-validation).
   Flash `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`. Laptop USB or a
   power bank is the sole car supply; there is no separate battery switch.
   Power the car normally by USB after flashing. Keep that power
   connected for the entire series; do not press BOOTSEL or reset during trials.
2. Join the configured hotspot with iPhone **Maximize Compatibility** enabled.
   Confirm the laptop's WiFi IPv4 matches the broker IP in the firmware and
   the custom broker configuration. The current address is `172.20.10.2`.
3. From a terminal at the repository root, start the custom bench broker:

   ```powershell
   & "C:\Program Files\mosquitto\mosquitto.exe" -c ".\dashboard\mosquitto.local.conf" -v
   ```

   Keep this terminal available for Ctrl+C and restart. The test uses this
   broker on `172.20.10.2:1883`, with the browser on `127.0.0.1:9001`.
   The separate default Windows Mosquitto service on `127.0.0.1:1883` is not
   involved; leave it alone.
4. Start the local dashboard in another terminal (`cd dashboard`, `npm start`).
   Refresh `http://127.0.0.1:4173`, connect, confirm accepted 46-byte main telemetry,
   40-byte resource packets, five-byte heartbeats and a successful **Request status** reply.
5. Optionally capture timestamped Pico USB serial output at 115200 baud.
   Preserve the boot S1 summary, link transitions and MQTT connection logs.
   Do not add WiFi passwords or SSIDs to evidence.

## Broker outage: five trials

From a third terminal at the **repository root**:

```powershell
node testing/subsystem1/recovery_probe.mjs --kind broker
```

The observer uses direct **MQTT over TCP**, independently of the dashboard's
WebSocket client. It generates no sensor readings and sends only read-only
binary STATUS requests. It never stops processes or changes the network.

For each trial:

1. Wait for the car's telemetry and heartbeat. Type `loss` and Enter. The
   recorder rejects the attempt unless both are fresh and its subscriptions
   are ready. It then obtains a matching STATUS reply with a fresh snapshot
   and stationary-test flag. **Wait for “Baseline confirmed”.**
2. Press **Ctrl+C in the custom Mosquitto terminal**, keeping Pico power and
   hotspot unchanged. Do not stop the recorder, dashboard or Windows service.
3. Wait for **“Outage observed”**, then keep the broker off another **10 s**.
   The dashboard should mark current readings unavailable and retain history.
4. In the recorder, type `restore` and Enter **immediately before** restarting
   the custom Mosquitto command above. This starts the conservative timer.
5. The recorder waits for five new telemetry messages, two heartbeats and a
   matching STATUS reply. It checks uptime continuity and reports observed
   delivery intervals. Wait for the saved trial result before the next `loss`.
6. Confirm the website reconnects and its live readings return. Request STATUS
   from the website once after the recorder's trial has completed. Record any
   browser issue separately from the direct TCP result.

It stops after five trials, saving a uniquely named JSON file under
`testing/subsystem1/logs/`. `status` prints the current phase; `abort` records
an incomplete attempt; `finish` or Ctrl+C saves the partial series and exits.
Earlier evidence files are never overwritten. An incomplete attempt counts
as an attempt, so keep it in the record and run additional attempts in a new
file if needed; do not silently remove failures from a five-trial result.

## Hotspot outage: five separate trials

Start a new recorder:

```powershell
node testing/subsystem1/recovery_probe.mjs --kind wifi
```

Use the same sequence, but after **“Baseline confirmed”** turn **off the
phone's hotspot**, leaving the custom broker, recorder, dashboard and Pico
power running. Wait for **“Outage observed”** and another 10 s. Type `restore`
**immediately before enabling the hotspot**. Let the laptop reconnect as well.

Keep the same configured network and Maximize Compatibility setting. If the
laptop gets a different WiFi IP, mark the attempt incomplete: the firmware's
broker destination and the listener must agree. Restore the original bench
setup before a new series; do not count a reflash or Pico reboot as recovery.

The recorder allows a 30 s observation window after the restoration marker.
Check that fresh data and the website STATUS button work again. Broker
readiness may remain true during hotspot loss because Mosquitto is local;
Pico telemetry/STATUS restoration is the meaningful result.

## Interpret the evidence honestly

The timing origin is the operator's `restore` marker **before** enabling the
service, not a measured AP-beacon or broker-listener timestamp. The first fresh
telemetry delay therefore includes human delay, service startup, laptop
association, the observer's connection/subscription and Pico recovery. It is
a **conservative end-to-end upper bound**, not an isolated Pico recovery time.

The observer retries its own TCP connection every 250 ms after disconnection
and sends an outbound keepalive after 15 s without a sent packet, even while
telemetry is arriving.
It accepts no retained telemetry, duplicate/backward sequences or unmatched
STATUS replies. The pre/post STATUS uptime difference must agree with elapsed
monotonic time using both exchanges' request/reply windows plus a 2 s clock
tolerance (uint32 uptime wrap is supported). Uptime is sampled before the
reply is received, so baseline reply delay must be included as well.
This catches a reset being used as recovery. Normal-driving builds cannot
start the stationary test baseline.

`within_conservative_15s_target` requires functional recovery and first fresh
telemetry within 15 s of the earlier marker. A slower result cannot isolate
whether the Pico, laptop, human delay or observer caused the excess. Inspect
the logs; do not label it a proven Pico-only timing failure. Browser timings
are separate again. For exact AP-restoration timing, add an independent
availability timestamp or synchronized external capture before claiming it.

JSON retains packet arrival times, counters, source age, STATUS uptime/reply
latency and observed post-restoration intervals. Use intervals after recovery
for the 2 s/5 s targets; never include the deliberate outage as normal delivery.
Network scheduling can add jitter: retain measured values instead of rounding
them down. The local UF2 hash is recorded when available, but it does not read
back the Pico's flash; record what you actually flashed alongside the capture.

Historical hotspot recoveries delivered heartbeats in bursts; tiny burst
intervals are not steady-state cadence evidence. Current v4 includes main
generation boot time and session. The recorder filters pre-restoration
generation and old-session data using correlated STATUS timing. Scan/resource
packets do not advance main cadence. Continue observing sustained delivery;
capture laptop and Pico serial transitions to separate component delays.

Record results in the [S1 test log](TEST_LOG.md), with a source revision,
firmware hash, power source and any failed/incomplete attempts. Software tests
and a successful build do not substitute for these physical observations.
