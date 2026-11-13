# One final S1 hardware session

Updated for the **sensor integration**, 13 November 2026. Current image is
3a8b79c3 (723456 bytes), with wheel motors disabled but servo scanning enabled.
The preceding 390c5893 stationary restoration and earlier motor/recovery
observations remain historical. The initial-task stack fix is retained.
Record new-image observations separately; software checks are not physical
accuracy, stopping-time or floor avoidance evidence.
Current stationary servo/profile and existing readings are now user-confirmed;
an independent capture has 220 valid main frames, 26 scans and matching
TEST=true STATUS with zero decode errors. Serial S1 24/24 and S5 software
6/6. This does not close the normal motor or floor-trajectory items.
See [current integration and build identity](SENSOR_INTEGRATION.md).

The whole car uses one laptop USB/power-bank supply. There is no separate
battery switch. Keep the supply connected throughout each persistence or
outage trial. Unplugging it restarts the next run and clears the STOP latch.

## Preparation

- [ ] Use the single current stationary _test UF2, NET=lwip MQTT=1 TEST=1
      APP_MODE=integrated. Start the hotspot, custom Mosquitto and local website
      as described in [dashboard setup](../../dashboard/README.md).
- [ ] Confirm the laptop WiFi IPv4 matches the ignored firmware settings and
      broker listener; never include credentials in logs.
- [ ] Flash once, refresh/reconnect the dashboard and wait for automatic
      read-only STATUS clock sync. Expect Control disabled / Stationary test.
- [ ] Record local UF2 SHA-256, source identity and whether the user flashed
      that exact file. A local hash alone is not flash readback.

## Stationary sources, commands and missing producers

- [x] Observe accepted 46-byte main frames, five-byte heartbeats and 40-byte
      diagnostics. Packet acceptance is distinct from measurement validity.
- [ ] Check GP27/GP26 white/white=4, white/black=5, black/white=6, black/black=7.
      No GPIO26 barcode capture should be active.
- [ ] Check a known-distance target, invalid/no-echo behavior, live pitch
      and the actual reported IMU state. Record results; do not claim height
      accuracy from a hand tilt or nominal-speed estimate.
- [ ] Disabled S2 feedback, absent barcode/path producers and unsupported
      stack high-water remain unavailable rather than plausible zero values.
- [ ] STATUS confirms its matching 16/62-byte reply. Logical STOP confirms an
      11-byte result in the disabled build. Dashboard reconnect preserves it;
      unplug/reconnect USB normally clears it.
- [ ] Disconnect the browser and stop Pico telemetry: live values expire,
      received chart/profile history remains labelled historical.

## Stationary servo/profile check

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

Rebuild TEST=0 APP_MODE=integrated only for this phase. Record its new hash;
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
- [ ] Hold a stable line_bits 4 or 7 target; observe BOTH wheels rotating.
      Click Latch stop once without changing the target. Both must physically
      stop and the matching result must confirm the latch. If either keeps
      moving, disconnect the USB supply immediately.
- [ ] Keep USB continuously connected. Try every IR combination and reconnect
      only the dashboard. Both wheels remain stopped and the latch stays set.
- [ ] Record any motor fault as a failure/diagnosis condition, not an already
      stationary STOP pass. Do not bypass encoder/PID protections.
- [ ] Finish by unplugging USB. Do not reconnect normal firmware with wheels
      on the surface: restart clears the inhibit and may start movement.

## Outages and delivery timing

Return to the stationary build for these procedures. This avoids automatic
movement after any intentional or accidental restart.

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
- [ ] Compare like-for-like features if claiming RAM/message savings. Preserve
      the original JSON-to-v2 comparison as historical evidence; v4 adds data.
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
STOP results but never sends STOP or controls a service. Physical movement
needs a separate user observation. Exported evidence contains no credentials.
