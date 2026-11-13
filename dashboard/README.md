# RoboCar Mission Control

Local real-MQTT dashboard for Buddy/Subsystem 1: Overview, Sensors & motion,
Communications, charts and exports. No cloud hosting, demo feed, npm packages
or website build step.

## Current batch — 13 November 2026

One current **Packed v4** contract covers distance, IR, IMU, measured wheel
velocity/counts/net travel, controller state, faults and future path/barcode
data. Typed scan chunks and diagnostics share the telemetry topic. The old
v3/JSON decoders and controller beacon path are removed.

Current stationary image:
**build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2**

Profile: **NET=lwip MQTT=1 TEST=1 APP_MODE=integrated**.
Bytes: **723456**; SHA-256: **3a8b79c37a01cfa92615db69e62d2817795ab53ec9decae5e33c4faa19f3aaf6**.

This image integrates origin/sensor with the current S1/controller.
The user reports flashing it and confirms the servo sweeps, the three-point
profile appears, and existing readings update. An independent capture has
220 valid distance/pitch frames, 26 scan packets, 22 heartbeats and matching
STATUS, with zero decode errors. Normal physical line/avoidance/STOP and
accuracy checks remain open. The preceding 390c5893 working image and
original failure records are preserved. Only one current UF2 is kept.

**The wheel motors are disabled in this test image, but the GP12 servo
centres at startup and sweeps when the front is blocked.** Normal mode
keeps line following and lets the shared controller arbitrate scan/turn
intent; the STOP latch has priority over both.

The whole car uses laptop USB or a power bank, with no separate battery
switch. TEST=1 integrated disables motor control. Normal TEST=0 can request
motion three seconds after startup; raise both wheels before flashing or
connecting power in that mode. Unplugging USB turns off the whole car.
Reconnecting starts a new run and clears the STOP latch.

## Start the bench

1. Join the configured hotspot. For iPhone, enable Maximize Compatibility.
   The laptop WiFi IPv4 must match ignored config/wifi_secrets.h and the
   custom Mosquitto configuration (currently 172.20.10.2).
2. At the repository root, start the custom broker and leave it open:

~~~powershell
& "C:\Program Files\mosquitto\mosquitto.exe" -c ".\dashboard\mosquitto.local.conf" -v
~~~

3. In another terminal:

~~~powershell
cd dashboard
npm start
~~~

Use **node server.mjs** if PowerShell blocks npm.ps1. Open
**http://127.0.0.1:4173**. Refresh after website changes.

4. Flash the current stationary UF2 through BOOTSEL. Let it restart and keep
   USB connected. Click Connect robot using:

| Setting | Value |
| --- | --- |
| Broker | ws://127.0.0.1:9001 |
| Telemetry | mtk3smp-pico/telemetry |
| Heartbeat | mtk3smp-pico/heartbeat |
| Commands | mtk3smp-pico/command |
| Status replies | mtk3smp-pico/status |
| Credentials | Empty for the local bench |

Both listeners belong to the same custom broker: car TCP on WiFi:1883 and
browser WebSocket on 127.0.0.1:9001. The default Windows service on
localhost:1883 is separate. All four topic names must be distinct; the
browser waits for all three subscription acknowledgements.

STATUS clock synchronization is automatic and read-only. Measurements wait
for a matching Pico clock/session reply; stale buffered frames are rejected.
User STATUS and STOP requests retain correlation IDs, bounded waits and no
automatic actuator retry. Credentials stay in tab memory and are not exported.

## Measurements and missing producers

| Data | Current source / readiness |
| --- | --- |
| Distance, left/right IR, pitch and hump estimates | Existing protected S5/S3/S4 snapshots |
| Wheel speeds, encoder counts, net travel, motor mode/reason | Existing measured S2 feedback; available in normal mode |
| Controller state / STOP latch | Actual protected controller publication |
| Faults | Fresh known/active reports; incomplete coverage is not global all-clear |
| Barcode / navigation intent | Transport/display ready; S3 passive snapshot getter pending |
| Total distance travelled | Transport/display ready; S2 actual path getter pending |
| Completed servo scan | S5 protected three-angle profile; 30/right, 90/centre, 150/left degrees |
| Heap / resources | Read-only allocator snapshot, sampled minimum, requested app stacks |
| Stack high-water | Unavailable until actual platform instrumentation supplies it |

In stationary mode the S2 motor/encoder task is disabled, so its measurements
remain unavailable. PWM duty and nominal speed are never shown as measured
wheel velocity. Signed net travel can decrease in reverse and is displayed
separately from total travelled path length.

S4 still uses its nominal 150 mm/s input unless its owner supplies measured
speed. One-wheel/curb detection and height accuracy remain Buddy 4 work.
Missing measurements are null, not fabricated zeroes. A valid accepted
packet can contain unavailable measurements. Validity does not prove wiring
or sensor calibration.

The owner-facing plug-in contract is [Producer handoff](../testing/subsystem1/PRODUCER_HANDOFF.md).
An owner's strong nonblocking getter replaces an unavailable weak default;
S1 and the browser need no further edits within the documented ranges.
No barcode event queue is consumed by S1.

## Wiring and IR checks

| Role | GPIO |
| --- | --- |
| Barcode AO, reserved | GP28 / ADC2 |
| Left IR (firmware centre) | GP27 / ADC1 |
| Right IR | GP26 / ADC0 |
| Left encoder A/B | GP16 / GP17 |
| Right encoder A/B | GP0 / GP1 |
| Left / right motors | GP8/9 / GP10/11 |
| Ultrasonic TRIG/ECHO | GP4/GP5 |
| IMU SDA/SCL | GP2/GP3 |
| Servo scan | GP12, PWM slice 6 |

Line bits: 4 white/white, 5 white/black, 6 black/white, 7 black/black,
0 unavailable. IR/pitch expire independently at 100 ms on the Pico.
Both white and both black request forward motion in normal mode; white
is not a motor-stop condition. The old digital barcode sampler is inactive.

## Servo scan and obstacle integration

Bring a target to **250 mm or closer in front** to trigger a scan. S5 samples
left/right, returns the head to centre and publishes a completed three-point
profile. Under Sensors & motion, the scan plot should update at angles
30/90/150 and its CSV export should contain those points. Invalid echoes
remain gaps. Angles are servo commands; physical orientation/calibration
still needs the bench check. A clear centre alone does not trigger a profile.

In normal mode, a clear front uses the existing IR line targets. Scanning
requests zero wheels; a clear side permits a brief controller-owned turn.
Blocked/invalid range, missing/busy/stale navigation and STOP inhibit motion.
S5 never calls the motor API. The front-distance tile uses centre samples,
not the left/right echoes. Servo sensing can continue while STOP is latched.

See [the integration and evidence](../testing/subsystem1/SENSOR_INTEGRATION.md)
and [the physical checklist](../testing/subsystem1/FINAL_ACCEPTANCE.md).
This uses the existing v4 decoder, topics and scan fields.

## Binary messages

| Message | Bytes |
| --- | ---: |
| Main telemetry, b4 | 46 |
| Completed scan chunk, b5 | 19..56 |
| Diagnostics, b6 | 40 |
| Heartbeat, 80 | 5 |
| STATUS request / reply | 5 / 16 or 62 |
| STOP request / result | 13 / 11 |

Generation uptime, session, independent source freshness and unknown
sentinels are part of the current contract. Heartbeat is nominally every
4 seconds; main telemetry every 400 ms. These schedules do not establish
received timing guarantees. See [the bit table](../testing/subsystem1/S1_COMPLETION_BATCH.md).

Charts retain received history. Current measurements expire after two
seconds without sufficiently fresh telemetry. Completed scans are explicitly
historical profiles. Telemetry CSV, scan CSV and evidence JSON contain actual
received data, no credentials. Histories are bounded to 600 telemetry rows,
120 resource rows, 30 inspector packets, 80 events and one 181-point profile.
Sampled heap minimum is not an exact allocation peak; stack reservations
exclude kernel/radio stacks and are not stack high-water.

## Build and final validation

~~~sh
make -B -C build_make PICO_SDK_PATH=/opt/robocar/pico-sdk-2.2.0 NET=lwip MQTT=1 TEST=1 APP_MODE=integrated -j8
~~~

For a raised-wheel normal motor phase use TEST=0 instead. Standalone IR and
powered encoder diagnostics require explicit APP_MODE=ir or
TEST=1 APP_MODE=encoder. TEST=1 alone never enables that powered bench.
Keep USB CDC / CRASH_REPORT=0 because GP0/GP1 belong to the encoder.

Run the offline consolidated checks from the root:

~~~powershell
& "C:\msys64\ucrt64\bin\python.exe" testing/subsystem1/run_batch_checks.py
~~~

After building, run the ARM stack-budget guard from WSL at the root:

~~~sh
python3 testing/subsystem1/test_s1_stack_frames.py
~~~

Remaining hardware checks are in [one combined final session](../testing/subsystem1/FINAL_ACCEPTANCE.md).
No offline test establishes current-image physical accuracy, recovery,
stopping time or runtime peaks. Earlier observations and failures remain in
[the test log](../testing/subsystem1/TEST_LOG.md).
