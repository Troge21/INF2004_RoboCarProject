# RoboCar Mission Control

Local real-MQTT dashboard for Buddy/Subsystem 1: Overview, Sensors & motion,
Communications, charts and exports. No cloud hosting, demo feed, npm packages
or website build step.

## Current integrated firmware - 22 November 2026

One current **Packed v4** contract covers distance, IR, IMU, measured wheel
velocity/counts/net travel, controller state, faults, encoder path and future barcode
data. Typed scan chunks, diagnostics and S4 terrain reports share the telemetry topic. The old
v3/JSON decoders and controller beacon path are removed.

Current normal operating image (motor control enabled):
**build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2** (NO _test suffix)

Profile **NET=lwip MQTT=1 TEST=0 APP_MODE=integrated CONSOLE=usb_cdc CRASH_REPORT=0**.
Size **738816 bytes**. SHA-256
`336ee965c0b974e4c6bda93900dd9388c5f25ee92ed718248fc0e449043e9c11`.
The integrated build includes MQTT, encoder motor control, the S3 line
sampling fixes and GP27 cutoff of 455, sensor-guided left-curve recovery,
obstacle scans, S4 hump estimates, motion reporting and pickup STOP. Flash
the UF2. The matching ELF and MAP are for debugging and memory inspection.
The build and offline checks passed. The user confirmed
right-wheel hump detection and pickup STOP on the earlier S4 trace image
`c396fa80`.
The demo layout places the barcode on the left and the hump under the right wheel.

The normal image uses the checkpoint's line-following logic and reports
forward range while driving. A confirmed obstacle at **240 mm or closer**
automatically latches STOP for a width scan. The separate `_s2_bypass.uf2`
is for obstacle course testing.

The controller allows a short bounded wait for the wheel-feedback snapshot
to unlock. An unavailable or stale snapshot still stops the car.

The controller starts with **STOP latched** on every power-up. **Resume** in
Overview or Communications releases the inhibit without rebooting or resetting
run distance. It requires fresh stopped-wheel feedback, valid line data and
no motor fault. STOP takes priority; reconnect never resumes
movement automatically. A pickup ends the run and requires a Pico restart.
Barcode/navigation and external faults still await producers.

On **18 November**, the user reported that the Resume test works after the
requested Stop -> Resume -> Stop procedure. This is a user-reported basic
functional pass; no new timing/packet capture or flash readback was collected.
Detailed blocked/fault/expiry/path-retention checks remain separate.
[Resume bench record](../testing/subsystem1/logs/2026-11-18-resume-user-bench-observation.md).
The preceding normal image 9c0dbfd5 also passed physical STOP and persistence.

**Delivery timing remains open:** the initial 45-second normal window had a
688 ms main maximum, but a later 180-second window reached 5499 ms main and
9304 ms heartbeat. No publish errors or missing sequence numbers were
recorded; gaps also occurred while STOP was latched. The earlier stationary
one-minute pass remains valid only for that captured window. See
[normal observations](../testing/subsystem1/logs/2026-11-17-normal-stop-summary.json).

**Raise both drive wheels securely for the first flash and startup check.**
The whole car uses USB or a power bank. Reconnecting the supply starts a new
run with STOP latched. Keep the GP12 servo and its leads free during setup.
See [the normal mode and STOP/Resume check](../testing/subsystem1/MOTOR_STOP_TEST.md).

The preceding stationary image 62e5bd63 passed a full-minute delivery capture:
149 main packets, 15 heartbeats and six scans; median 402 ms/max 600 ms main,
max 4057 ms heartbeat; zero missing sequences/publish/decoding errors.
Those are measured stationary results, not a physical normal-mode pass.
That delivery policy remains in the Resume image. Its WiFi no-sleep mode
trades power saving for latency; current draw remains unmeasured.
[Delivery evidence](../testing/subsystem1/TELEMETRY_DELIVERY.md),
[17 November Resume build identity](../testing/subsystem1/logs/2026-11-17-resume-fingerprint.json).

The `_s2_bypass.uf2` image and matching ELF/MAP are for the pending 6b
bypass and line rejoin. Build a stationary sensor image on demand with
`TEST=1 APP_MODE=integrated`. It disables wheel PWM and leaves servo scanning
active. Build standalone motion diagnostics with `TEST=1` and `APP_MODE=distance`,
`APP_MODE=reverse` or `APP_MODE=turn S2_TURN_TRACK_MM=120`.

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

4. With both drive wheels securely raised and the servo free, flash the
   current normal UF2 through BOOTSEL. Keep the wheels raised and click
   Connect robot using the settings below. Check for **Stop latched** before
   clicking Resume.

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
User STATUS, STOP and Resume retain correlation IDs, bounded waits and no
automatic actuator retry. Credentials stay in tab memory and are not exported.

## Measurements and missing producers

| Data | Current source / readiness |
| --- | --- |
| Distance, left/right IR, pitch and hump estimates | Existing protected S5/S3/S4 snapshots |
| Wheel speeds, encoder counts, net travel, motor mode/reason | Measured S2 feedback in normal and passive stationary modes |
| Controller state / STOP latch | Actual protected controller publication |
| Faults | Fresh known/active reports; incomplete coverage is not global all-clear |
| Barcode / navigation intent | Transport/display ready; S3 passive snapshot getter pending |
| Total distance travelled | S2 protected encoder odometer; reverse adds path, reset on Pico restart |
| Completed servo scan | S5 protected 37-angle profile and estimated width when both edges resolve |
| Heap / resources | Read-only allocator snapshot, sampled minimum, requested app stacks |
| Stack high-water | Unavailable until actual platform instrumentation supplies it |

In stationary integrated mode, S2 collects encoders with wheel PWM disabled.
After about 200 ms, resting wheels should show real zero speeds; turn them
by hand to change velocity/counts and total distance. The existing servo is
active, so leave it free to move. Commands cannot enable wheel PWM in this
profile. Normal line following/PID and STOP ownership are retained.

Average wheel speed is the instantaneous mean of left/right speed magnitudes;
the card sparkline and CSV use the same value. Wheel velocity is signed.
Net travel can decrease in reverse; total centre path accumulates absolute
mean wheel increments. Both rely on encoder calibration and wheel traction.
Metres and m/s show three decimal places.

On the combined S4 image, full-width estimates integrate pitch over actual
encoder travel. One-wheel estimates use sustained roll and measured wheel
spacing. **Last hump** shows a completed crossing, and **Run maximum** keeps
the highest accepted estimate until a Pico restart or explicit new-run reset.
Positive turn rate indicates left. The rate comes from the difference in wheel
speeds and is sensitive to slip. Height calibration needs physical checks.
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

While driving, the robot confirms an obstacle using two consecutive centred
echoes at **240 mm or closer** and within 25 mm of each other. A single
close reading or a missing echo leaves line following active. For a manual
width check, click **Stop** and place a flat target within 250 mm. The scanner
waits for a complete stopped encoder window, samples 37 servo positions and
returns the head to centre. The Pico interpolates
between the five bearings estimated from overhead photos of this car
(48°, 70°, 90°, 110° and 130° at servo commands 0°, 45°, 90°, 135° and 180°).
The radar shows those calibrated bearings. The width estimate uses the
central return and two bracketed edges, with a nominal 7.5° correction per
edge for the ultrasonic beam. Once triggered, the scan accepts
a settled centre range up to 280 mm. A farther echo can corroborate an edge
across one intervening face echo. The scanner retries once if the first sweep
is unclear. In
**Sensors & motion → Obstacle radar**, read the estimate in
millimetres for the last completed scan. An ambiguous scan shows **Edges
unclear**. Scan CSV includes each measured bearing, distance and the estimate
if available. Use a measured, flat box to check physical accuracy.

After an automatic stop, move the obstacle away and wait for two centred
polls to show a clear path (above 280 mm or no return). The car stays stopped
until you click **Resume**. Two far readings also rearm a manual scan for a
new target. Front-distance telemetry uses centre echoes. The bypass test
profile uses its existing three-angle scans and separate turn/rejoin policy.

See [the integration and evidence](../testing/subsystem1/SENSOR_INTEGRATION.md)
and [the physical checklist](../testing/subsystem1/FINAL_ACCEPTANCE.md).
The first v4 scan chunk can carry a two-byte width trailer. The dashboard
continues to accept earlier scan chunks on the same MQTT topic.

## Resume after STOP

With BOTH wheels raised, leave power connected and click Stop. Once
both wheels stop and Stop latched is confirmed, keep the line sensors valid
and click **Resume**. Expect
**STOP latch released; autonomous control may move the car.** The button is
disabled in stationary TEST=1 and until a fresh normal latch is received.

Resume obtains a server-issued one-use ticket before sending its commit.
Expired, replayed or unready requests leave STOP latched. A missing completion
has an unknown outcome; inspect live state and use STOP if needed. No retry
or reconnect automatically releases the latch. Run data is retained. Finish
the raised-wheel check with another Stop and unplug the supply.
See [the controller contract](../testing/subsystem1/CONTROLLER_COMMANDS.md) and
[physical procedure](../testing/subsystem1/MOTOR_STOP_TEST.md).

## Binary messages

| Message | Bytes |
| --- | ---: |
| Main telemetry, b4 | 46 |
| Completed scan chunk, b5 | 19..58 |
| Diagnostics, b6 | 40 |
| S4 motion and terrain, b7 | 15 |
| Heartbeat, 80 | 5 |
| STATUS request / reply | 5 / 16 or 62 |
| STOP request / result | 13 / 11 |
| Resume arm request / authorization | 13 / 20 or 66 |
| Resume commit / result | 13 / 11 |

Generation uptime, session, independent source freshness and unknown
sentinels are part of the current contract. Heartbeat is nominally every
4 seconds; main telemetry every 400 ms. These schedules do not establish
received timing guarantees. The [Change Requirement 1 report](../testing/subsystem1/CHANGE_REQUIREMENT_1.md) records the current bit layouts, measured sizes and bench evidence.

Charts retain received history. Current measurements expire after two
seconds without sufficiently fresh telemetry. Completed scans are explicitly
historical profiles. Telemetry CSV, scan CSV and evidence JSON contain actual
received data, no credentials. Histories are bounded to 600 telemetry rows,
120 resource rows, 30 inspector packets, 80 events and one 181-point profile.
Sampled heap minimum is not an exact allocation peak; stack reservations
exclude kernel/radio stacks and are not stack high-water.

## Build and final validation

~~~sh
make -B -C build_make PICO_SDK_PATH=/opt/robocar/pico-sdk-2.2.0 NET=lwip MQTT=1 TEST=0 APP_MODE=integrated CONSOLE=usb_cdc CRASH_REPORT=0 -j8
~~~

Add `S4_MOTOR_TRACE=1` when a powered S4 USB trace is needed. Build locally
with your ignored WiFi configuration.

The current image is already normal TEST=0. Use TEST=1 only for an explicit
stationary hand-turn/sensor profile. Standalone IR and
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
