# Telemetry delivery correction - 17 November 2026

Normal operating follow-up: the single current image is now the TEST=0
integrated build with motors enabled, using identical sources. Its current
identity and raised-wheel procedure are in [normal mode](MOTOR_STOP_TEST.md).
The measurements below belong to stationary 62e5bd63; preserve that distinction.

**Later normal-image timing qualification:** normal 9c0dbfd5 was user-flashed.
An initial 45-second window had max 688 ms main. A later 180-second mixed
line/scan/STOP run had max 5499 ms main and 9304 ms heartbeat, zero missing
sequence numbers and publish errors. Gaps also occurred while latched; this
does not isolate motor load or a particular network component. Continuous
timing acceptance is **open** despite the earlier short stationary pass.
The normal STOP request/reply and user-observed physical stopping passed.
[Normal summary](logs/2026-11-17-normal-stop-summary.json),
[latched window](logs/2026-11-17-normal-latch-persistence-summary.json).

The user reported slow, intermittent telemetry after the expanded S1 batch.
Two live captures reproduced this on the stationary motion image. No broker,
hotspot, motor or servo commands were issued. One observer sent a read-only
STATUS request; the paired TCP/WebSocket observer sent no commands.

## Observed baseline

| Measurement | Result |
| --- | --- |
| First 45-second WebSocket capture | 109 main packets, 11 heartbeats, matching STATUS in 54 ms |
| Longest main arrival gap | 2959 ms |
| Longest heartbeat arrival gap | 8734 ms |
| Missing main sequence numbers in first window | 6 |
| Publish error counter | 96 to 103; publication contention remained zero |
| Second, paired 45-second capture | 101 matching main packets on direct TCP and WebSocket |
| Paired longest main arrival gap | 3104 ms on both transports |
| Difference between arrivals of the same packet | 0..1 ms, median 0 |

Delivered consecutive sequence numbers carry 400 ms generation steps.
Missing sends account for observed 800..2000 ms generation gaps. Packet
generation time is serialization time, not the timestamp of every sensor.
The captures locate the multi-second stalls upstream of the local subscriber
transport, rather than in browser chart drawing. They do not isolate radio,
AP, TCP retransmission or laptop WiFi delay. Publish failures confirm output
pressure but the old counter did not retain individual lwIP error codes.

[Baseline summary](logs/2026-11-17-delivery-baseline-summary.json),
[first raw capture](logs/2026-11-17-telemetry-delay-baseline.json),
[paired raw capture](logs/2026-11-17-delivery-paired-baseline.json).
An initial paired observer used the wrong PacketReader method and exited;
that incomplete record is explicitly marked, not counted as firmware failure.

## Focused correction

1. The existing radio-owner task selects CYW43_NONE_PM after interface
   setup and before joining, under its existing driver lock. The installed
   SDK otherwise selects PM2. Failure is reported without preventing normal
   join/recovery. No-sleep mode trades radio power saving for latency; actual
   current draw has not been measured.
2. Each accepted MQTT TCP connection disables Nagle coalescing so small
   telemetry and replies need not wait for aggregation. This is local to
   the Pico MQTT connection, including reconnects.
3. Ordinary telemetry, scans, diagnostics and heartbeats allow at most two
   outstanding requests, leaving two SDK slots for commands/subscriptions. QoS 0 also occupies a request until a TCP
   acknowledgment; the SDK defaults to four slots. Periodic packets no
   longer fill all four slots with old measurements during an output stall.
   STATUS and controller STOP dispatch/results retain priority. After an
   acknowledgment, S1 serializes the latest snapshot; no offline queue or
   larger transport buffer is introduced.
4. A busy/failed heartbeat retries through its single existing timer after
   100 ms, rather than dropping it and waiting another four seconds. WiFi
   loss cancels this timer as before. Counter advances only on accepted send.
5. The dashboard schedules a redraw on packet receipt, coalescing a burst
   into one animation frame. Its existing timer still handles expiry. The
   previous periodic refresh alone could add up to 500 ms of display delay.

The SDK-specific queue/connection access is confined to mqtt_utk.c using
lwIP's mqtt_priv.h. Both full builds compile against the real installed SDK
2.2.0 headers. Recheck this adapter when upgrading the SDK. Host substitutes
only model this narrow interface; they do not prove real radio performance.

No schema, topic, nominal 400 ms publish schedule, freshness threshold,
sensor driver, GPIO, calibration, encoder measurement, PID, steering, scan
algorithm or controller STOP lease/latch was changed. Sensor values are not
interpolated and stale frames remain rejected.

## Software/build status

Nineteen offline groups pass, including 61 actual MQTT-port checks. New
cases exercise no-delay selection, stalled-request backpressure, latest
main-frame precedence, STATUS/STOP priority, bounded heartbeat rechecks and
link-loss cancellation. The actual WiFi setup sequence is checked with
deterministic driver/lock stubs for success, PM failure and interface failure.
Existing 43 dashboard, 20089 cross-language vectors, sensor, encoder,
controller/STOP and shared GPIO checks pass. Four ARM profile checks and
existing startup/task stack guards pass; S2/S1/S5 static chains remain
544/464/136 bytes of their 2048-byte task stacks. These are not runtime peaks.

Current stationary _test UF2: **731648 bytes**, SHA-256
**62e5bd639e2f363aa364b5bcbc4087fce624922227672ffb619f81361d68cd69**.
Static RAM remains **84860 bytes**; normal static RAM remains 84124 bytes.
No task, semaphore, queue or larger network buffer was added. Normal and
stationary full forced builds pass without warnings; at that stage only the stationary
UF2 was kept. The later normal operating profile supersedes it locally. The previous working motion image/source archive is identified
in [the rollback record](logs/2026-11-17-before-delivery-fix-backup.json).

The first correction (af00fa94) was user-flashed and measured. It eliminated
missing sequences and publish errors in a 45-second window; STATUS was 13 ms
and maximum heartbeat gap 4803 ms. Main telemetry still paused up to 2096 ms,
with 85 frames and median 400 ms. Its single-packet acknowledgment gate was
too restrictive for streaming, so it has been replaced with the bounded
**two-packet** policy. The final two-packet image was subsequently user-flashed and tested below.
Do not treat the first correction as complete latency acceptance.

Pings during later diagnosis: Pico 25/25 replies, 6..161 ms (mean 61);
hotspot gateway 25/25, 2..101 ms (mean 25). These were separate from the
45-second capture and may themselves keep a wireless link active. They do
not prove zero loss under all traffic. Windows packet capture was unavailable
because the PktMon driver denied access; no capture/filter settings changed.
No Windows TCP registry, adapter power or broker settings were changed.

[First corrected capture](logs/2026-11-17-delivery-corrected-observation.json),
[first corrected summary](logs/2026-11-17-delivery-corrected-summary.json).
## Final user-flashed delivery result

The user confirmed flashing **62e5bd63** and refreshing/reconnecting. The
60-second read-only observer received **149 main packets, 15 heartbeats and
six scan packets**, with one continuous MQTT session. Results:

| Metric | Before (45-second windows) | Final (60 seconds) |
| --- | --- | --- |
| Maximum main receive gap | 2959 / 3104 ms | **600 ms** |
| Median / 95th percentile main interval | approximately 388 ms median with bursts | **402 / 478 ms** |
| Main generation interval | 400 ms, gaps from lost sends | **400 ms throughout** |
| Missing main sequence numbers | 6 in first window | **0** |
| Publish-error counter increase | 7 in first window | **0** |
| Maximum heartbeat receive gap | 8734 ms | **4057 ms** |
| Matching STATUS round trip | 54 ms | **41 ms** |

No main arrival gap exceeded one second; no decoding errors. Pitch, left/right
speeds/counts and path were available in all 149 frames, valid range in 148;
IR remained carried, with six completed scan packets. This confirms delivery
of real source fields, not physical speed/distance/height calibration or a
moving-motor STOP test. Heap samples remained 148008 free/largest/minimum,
static RAM 84860; no runtime high-water claim. Broker and hotspot were not
interrupted or reconfigured. The observer sent only read-only STATUS.

[Final summary](logs/2026-11-17-delivery-final-summary.json),
[final raw capture](logs/2026-11-17-delivery-final-observation.json).
The observed stationary delivery issue is resolved in this window. It does
not establish a worst-case wireless guarantee or replace later repeated
recovery/final group demonstration checks. The combined correction was
measured as a whole; no isolated causal claim for each individual setting.

## After flashing

Keep the servo free and use the stationary _test image. Refresh/reconnect
the dashboard. Collect another 45-60 second window under the same hotspot,
power and target conditions, including existing range/IR/pitch, hand-turned
encoder readings and servo scans. Use the existing read-only observer:

~~~powershell
node testing/subsystem1/observe_controller.mjs --seconds 60
~~~

Compare main receive intervals, source sequence/generation intervals,
heartbeat gaps, matching STATUS round trip and publish-counter deltas.
Record new-image recovery separately; earlier hotspot passes belong to their
captured image. If long gaps remain, examine radio/AP/laptop link and TCP
retransmissions rather than hiding them with longer browser freshness limits.
