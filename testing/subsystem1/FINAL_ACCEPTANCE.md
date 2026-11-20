# One final S1 hardware session

Use the [current normal integrated UF2](../../dashboard/README.md#current-integrated-firmware---20-november-2026).
**Raise both drive wheels for the first flash and startup check.** The normal
controller starts with STOP latched, and the servo can move during setup.
The startup latch has offline/build checks passed. On
18 November the user reported the basic Resume test works after the requested
Stop -> Resume -> Stop procedure. This is a
[user-reported functional pass](logs/2026-11-18-resume-user-bench-observation.md),
not a capture of timing, all negative cases or exact flashed identity.
Other checked observations below retain their stated older image identities.

The preceding normal image 9c0dbfd5 passed user-observed rotation, physical
STOP and persistence through IR/scan changes and dashboard-only reconnect.
[User bench record](logs/2026-11-17-normal-user-bench-observation.md).
Longer normal timing exposed max 5499 ms main/max 9304 ms heartbeat; delivery
acceptance remains open despite the short stationary pass.
[17 November Resume build identity](logs/2026-11-17-resume-fingerprint.json),
[normal mode and Resume procedure](MOTOR_STOP_TEST.md).

The preceding stationary 62e5bd63 image passed a 60-second delivery capture:
main max 600 ms, heartbeat max 4057 ms, zero missing sequences/publish errors.
That result and the checked items below identify their measured stationary
stage; they are not a new normal-image hardware pass. The initial-task stack
fix is retained. The Resume image preserves the sensor contract and adds explicit command admission.

The whole car uses one laptop USB/power-bank supply. There is no separate
battery switch. Keep the supply connected throughout each persistence or
outage trial. Reconnecting power starts a new run with STOP latched.

## Preparation

- [ ] Use the single current normal UF2 (no _test), NET=lwip MQTT=1 TEST=0
      APP_MODE=integrated. Start the hotspot, custom Mosquitto and local website
      as described in [dashboard setup](../../dashboard/README.md).
- [ ] Confirm the laptop WiFi IPv4 matches the ignored firmware settings and
      broker listener; never include credentials in logs.
- [ ] Flash once, refresh/reconnect the dashboard and wait for automatic
      read-only STATUS clock sync with wheels raised. Expect Normal / TEST=false;
      live controller state follows actual line/obstacle/latch conditions.
- [ ] Record local UF2 SHA-256, source identity and whether the user flashed
      that exact file. A local hash alone is not flash readback.

## Historical stationary delivery check (62e5bd63)

- [x] Collect 60 seconds: 149 main frames, 15 heartbeats, 41 ms matching STATUS.
- [x] Compare receive gaps, dropped sequences and publish-counter deltas
      against the recorded 2.959/3.104 s main and 8.734 s heartbeat gaps.
- [x] Real range/pitch/encoder/path fields and six scan packets continue.
- [ ] Complete physical hand-turn/calibration checks below; this delivery
      capture does not independently measure wheel rotation or ground distance.
- [ ] Keep stale-data rejection and STOP correlation; do not hide gaps by
      extending freshness. See [delivery guide](TELEMETRY_DELIVERY.md).

## Optional passive encoder check - explicit stationary profile only

These hand-turn steps require rebuilding TEST=1 integrated and flashing that
stationary image. The current TEST=0 normal image can drive the wheels; use
the normal running feedback/STOP section for its check.

- [ ] Build/flash TEST=1 only when this separate passive diagnostic is needed;
      keep both drive wheels raised and servo free.
- [ ] At rest, after the first speed window, observe two zero velocities,
      average zero and total distance zero after restart.
- [ ] Turn one wheel forward: its count/velocity changes, the other stays
      near zero, average becomes positive and centre path increases.
- [ ] Turn both forward, then reverse: velocities change sign, signed net
      travel can decrease, total path continues increasing. Compare against
      counted rotations (about 0.204 m per equal wheel revolution).
- [ ] Stop hand turning: speed returns to zero; total distance is retained.
- [ ] Browser reconnect/STOP must not reset the path; Pico restart resets it.
- [ ] Export CSV and confirm average_wheel_mps, signed velocities and distance_m.
- [ ] Repeat distance/IR/pitch/servo/profile/STATUS/latch checks on this image.

## Stationary-only sources/commands (TEST=1)

- [x] Observe accepted 46-byte main frames, five-byte heartbeats and 40-byte
      diagnostics. Packet acceptance is distinct from measurement validity.
- [ ] Check GP27/GP26 white/white=4, white/black=5, black/white=6, black/black=7.
      No GPIO26 barcode capture should be active.
- [ ] Check a known-distance target, invalid/no-echo behavior, live pitch
      and the actual reported IMU state. Record results; do not claim height
      accuracy from a hand tilt or nominal-speed estimate.
- [ ] Absent barcode/external fault producers and unsupported
      stack high-water remain unavailable rather than plausible zero values.
- [ ] STATUS confirms its matching 16/62-byte reply. Logical STOP confirms an
      11-byte result in the disabled build. Dashboard reconnect preserves it;
      unplug/reconnect USB normally clears it.
- [ ] Disconnect the browser and stop Pico telemetry: live values expire,
      received chart/profile history remains labelled historical.

## Stationary-only servo/profile check (TEST=1)

- [ ] Keep the servo head and its leads free to move. The test image enables
      the servo while wheel motor control remains disabled.
- [x] Confirm distance/IR/pitch/resources and matching TEST=true STATUS.
      Expected board summaries: S1 24/24 and S5 software 6/6.
- [x] Hold a flat target about 10-20 cm in front while the head faces centre.
      A <=250 mm reading triggers left/right samples and return to centre.
- [ ] In Sensors & motion, observe a completed profile at 30/90/150 degrees.
      Side no-echo values are gaps; front distance remains a centre sample.
      Export scan CSV and preserve the raw 24-byte three-point scan frame.
- [ ] Confirm physical left/right orientation matches the owner's mounting.
      Commanded angles and software settling time are not measured servo pose.
- [ ] Logical Latch stop and browser reconnect preserve the wheel inhibit;
      range/IR/pitch/scan telemetry may continue.

## Motion and STOP in normal firmware

The current image is already TEST=0 APP_MODE=integrated. Record its hash;
keep a single current UF2 after replacing the stationary image. Both drive
wheels must be securely raised **before flashing or connecting power**.
Normal driving can start after three seconds before WiFi connects.

- [ ] Observe Normal / Following line, actual left/right speeds/counts/net
      travel. Compare against an external wheel/count reference with S2;
      PWM duty is not a measured speed. True travelled path remains separate.
- [ ] With front clear, verify normal IR following still uses the previous
      targets. With a blocked front, observe wheel requests stop for scanning,
      then turn toward a clear side and return to line following on clear front.
- [ ] Press Latch stop during a repeated scan/turn cycle. Both wheels must
      stop and stay stopped through subsequent S5 turns, all IR combinations
      and browser reconnect. Unplug USB if either continues.
- [x] User confirms BOTH wheels rotated, then stopped on Latch stop;
      matching normal result captured. Original procedure:
      hold a stable line_bits 4 or 7 target; observe BOTH wheels rotating.
      Click Latch stop once without changing the target. Both must physically
      stop and the matching result must confirm the latch. If either keeps
      moving, disconnect the USB supply immediately.
- [x] User confirms both wheels stayed stopped and the latch persisted
      through the requested four IR combinations, close-target scan procedure
      and dashboard-only reconnect, with USB continuously connected.
- [ ] Record any motor fault as a failure/diagnosis condition, not an already
      stationary STOP pass. Do not bypass encoder/PID protections.
- [x] User confirms car unplugged after the historical test.
- [ ] Power-cycle the current normal build with both wheels raised and verify
      Stop latched and zero movement past the startup delay.

## Explicit Resume - basic user pass; detailed acceptance pending

Keep both wheels raised, servo free and power continuous throughout.

- [x] User reports the basic Resume test works following the requested
      Stop -> Resume -> Stop procedure (18 November). Do not infer every
      detailed item below from that short confirmation.
- [ ] With a valid front target above 250 mm and valid line readings, click
      Resume and observe both wheels rotating. Stop and confirm both wheels
      halt and the dashboard shows the matching result.
- [ ] Wait for zero measured velocities, click Resume once, and confirm its
      matching result and both wheels rotating again. Counts/path must retain
      the earlier run; STOP -> Resume does not restart the Pico.
- [ ] Latch stop again. Vary IR/scan targets and reconnect only the browser;
      both wheels must remain stopped and the latch must persist.
- [ ] With front blocked, Resume must remain latched and report not ready.
      Restoring clear front must not automatically release it; a new explicit
      click is required. Do not bypass or clear faults to obtain a pass.
- [ ] Check STOP remains available during a pending Resume. Unknown outcomes
      must be shown explicitly, with no actuator retry after timeout/reconnect.
- [ ] Unplug the whole-car supply after testing. No floor mission pass is
      inferred from a raised-wheel cycle. Save user and packet observations.

## Outages and delivery timing

Use the stationary build for timing measurements without motor load.

- [ ] Record a sustained baseline with real telemetry, heartbeat and matching
      STATUS. Export telemetry CSV and evidence JSON from Communications.
- [ ] Run five broker trials and five hotspot trials separately using
      recovery_probe.mjs, following [the recovery guide](RECOVERY_TESTS.md).
      Do not turn off Pico power. The recorder sends read-only STATUS only.
- [ ] Mark restoration before restoring the service. For hotspot outages run
      the recorder locally so logging survives chat internet loss.
- [ ] Keep failed/timed-out trials and confirm no reboot. V4 filters old
      generation/session data; browser reconnect is not Pico recovery.
- [ ] Evaluate the earlier design targets without rounding misses: telemetry
      <=2 s, heartbeat <=5 s, >=4/5 restoration within 15 s. The PDF does not
      state those numbers. The new 400 ms/4 s schedules alone are not passes.

## Resources and producer handoff

- [ ] Export linked static RAM, free/largest heap, minimum sampled free heap
      and requested application stacks. Record build flags and workload.
      Sampled minimum can miss short allocation peaks; app stack capacities
      exclude kernel/radio stacks and are not stack high-water.
- [x] S1's [Change Requirement 1 evidence](CHANGE_REQUIREMENT_1.md) includes the
      matched JSON-to-v2 RAM/message comparison and the current v4 footprint.
- [ ] Collect the other owners' representation and performance evidence for
      the group submission.
- [ ] When owners implement the [passive getters](PRODUCER_HANDOFF.md), verify
      real barcode/navigation, path length and external faults. The S5 scan
      profile/CSV is now covered by the stationary section. Leave these tests pending while producers are absent.
      No S1 format/decoder edit is needed within the defined contract.
- [ ] Capture the final integrated demonstration and commit/source identity.
      Do not mark the whole group requirement complete from S1 tests.

Read-only capture:

~~~powershell
node testing/subsystem1/observe_controller.mjs --seconds 300
~~~

Type **done** to finish early. It records telemetry/scans/resources/STATUS/
STOP/Resume results but never sends an actuator request or controls a service. Physical movement
needs a separate user observation. Exported evidence contains no credentials.
