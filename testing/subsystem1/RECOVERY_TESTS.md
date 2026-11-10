# S1 WiFi and MQTT recovery tests

Prepared **10 November 2026**, initially while the car was unavailable.
Later the user reported flashing the current UF2, and five actual broker
restart trials restored telemetry, heartbeat and STATUS without a detected
Pico reboot. Conservative recovery bounds were 3.318..5.811 s. Some delivery
intervals exceeded the earlier 2 s/5 s design targets. Five later hotspot
trials produced three correlated recoveries at 15.572, 18.015 and 20.484 s,
and two incomplete trials within 30 s. **The hotspot target was not met.**
See [the live evidence](TEST_LOG.md#live-broker-recovery---10-november-2026).

A replacement UF2 now uses MQTT keepalive 5 s instead of 60 s and bounds
pending TCP/MQTT connection attempts to 5 s. It is built and passed 36 port
host checks. The user then reported flashing it; a one-minute live baseline
received 118 telemetry, 13 heartbeats and three STATUS replies with no Pico
MQTT restart. **Corrected-image hotspot recovery remains untested.** Its SHA-256
is `9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0`.
Retain the original failed results when collecting a new series.
Follow-up clarification: later captures recorded 2/3 passes in a partial run
and 4/5 in a complete run after a laptop restart. The user subsequently said
the intended UF2 had not been flashed, then explicitly reported flashing it.
Those captures remain evidence with **unconfirmed running-image identity**;
their local hashes alone do not validate the intended image. A new confirmed-
flash check received telemetry, heartbeat and STATUS; a clean recovery series
after that flash is still needed. Do not discard the earlier failures.
The current normal baseline still exceeded the earlier strict arrival-interval
targets (2.007 s telemetry and 6.362 s heartbeat); baseline success does not
close those acceptance items. An allowlisted Pico serial/laptop-interface
capture was started for the retest, but the laptop restart terminated it.
It does not cover the later complete series; do not assume it is still running.

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

1. Flash the single current file:
   `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
   This is the stationary `NET=lwip MQTT=1 TEST=1` build. Keep the car battery
   off and power the Pico normally by USB after flashing. Keep that power
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
   Refresh `http://127.0.0.1:4173`, connect, confirm real 11-byte telemetry,
   five-byte heartbeats and a successful **Request status** reply.
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

Several hotspot recoveries delivered multiple heartbeats together. Such
burst intervals are not steady-state cadence evidence. Current v3 has no
sample-generation timestamp on the wire, so a new sequence/non-retained
packet does not independently establish its source age after transport
buffering. A fresh correlated STATUS establishes recent Pico communication;
continue observing periodic delivery afterward. Include laptop reconnection
and Pico serial link/MQTT times in the next series to separate the delays.

Record results in the [S1 test log](TEST_LOG.md), with a source revision,
firmware hash, power source and any failed/incomplete attempts. Software tests
and a successful build do not substitute for these physical observations.

## Changes prepared for this session

- WiFi DOWN (0), as well as negative failure states, retries with a 5 s delay.
  Unchanged failure states continue retrying; JOIN/DHCP progress is left alone.
- A transient initial join-request error keeps the radio service running.
  A failed hardware/interface initialization still parks the failed service.
- WiFi/IP loss cancels MQTT timers, pending requests and stale TCP state.
  Restoration permits a clean connection and command subscription.
- Immediate MQTT connection/allocation/address failures use one 3 s retry,
  preventing a connection attempt on every 2 ms network poll.
- Following the failed hotspot trials, pending connections also have a 5 s
  deadline. MQTT keepalive is 5 s; the application's separate heartbeat is
  still 5 s. These source changes require flashing and physical retesting.
- Existing binary packet formats, sensor drivers and IMU algorithm are unchanged.
  STOP stays disabled: a latched controller-owned stop is still an integration
  dependency, not equivalent to writing motor duty zero once.
