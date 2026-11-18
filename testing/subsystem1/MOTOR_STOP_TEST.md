# Motor STOP validation

The current implementation is Packed v4. Use [the combined final hardware
checklist](FINAL_ACCEPTANCE.md#motion-and-stop-in-normal-firmware) and
[the current controller contract](CONTROLLER_COMMANDS.md). The single current
UF2 is the normal integrated operating profile:
build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2 (no _test suffix).
NET=lwip MQTT=1 TEST=0 APP_MODE=integrated CONSOLE=usb_cdc CRASH_REPORT=0.
Prepared 17 November with explicit Resume: 721920 bytes, SHA-256
2c5a4652c7c434fb1d27743a3579b6a9ff0dcf2f2f9a4ee79a813840ecd1bcbe.
The motor task, measured encoder/path feedback, line following, S5 scans,
controller-owned avoidance, STATUS and latched STOP remain enabled. Resume
only releases the inhibit after fresh stopped-wheel/clear-front checks.
Sensor/PID/steering algorithms are unchanged. On 18 November the user reported
the Resume test works after the requested Stop -> Resume -> Stop procedure.
This basic functional pass is [user-reported](logs/2026-11-18-resume-user-bench-observation.md);
detailed negative cases/timing and exact flashed hash were not captured.
The older 9c0dbfd5 STOP pass below remains historical.

[Current Resume build fingerprint](logs/2026-11-17-resume-fingerprint.json),
[previous verified source/image backup](logs/2026-11-17-before-resume-backup.json).

The car has one USB/power-bank supply and no separate battery switch. Before
flashing or powering normal TEST=0, securely raise both drive wheels.
Normal driving can start after three seconds; unplugging clears the latch
on the next boot. A browser reconnect does not clear it.

After flashing with both wheels raised and the servo free:

1. Refresh/reconnect; Request status should report Normal (TEST=false).
   The live state follows the controller: Starting, Following line, scanning
   or turning as appropriate. It should no longer report Control disabled.
2. Use a broad flat target about 0.5-1 m in front (a valid reading above 250 mm)
   and hold a steady line target
   giving line_bits 4 or 7. Observe both wheels rotating and actual encoder
   speeds/counts/path changing. Do not bypass faults if they do not rotate.
3. Keeping the same target, click Stop once. Both wheels must stop and
   the matching controller result must confirm Stop latched. If either wheel
   continues, unplug the USB/power-bank supply immediately.
4. With power continuous, vary the line targets and reconnect only the
   dashboard. Both wheels must remain stopped; source telemetry may continue.
5. Keep power connected and both wheels raised. Restore the valid clear-front
   target above 250 mm and valid IR readings; wait for both measured velocities
   to be zero, then click Resume once. Expect the matching release result and
   both wheels rotating under the same original line policy. Counts/path must
   continue from the earlier run, not reset.
6. Click Latch stop again and confirm both wheels stop. Vary line targets and
   reconnect only the dashboard: they must remain stopped and latched. With a
   blocked front, an explicit Resume must report not ready and retain the latch;
   restoring a clear target must not automatically resume it. Use a new click.
7. Finish by unplugging the whole-car supply. Keep wheels raised for every
   later restart: restarting clears the latch and normal startup may drive.

The observer sends read-only STATUS only. Record physical wheel observations
separately; a controller reply confirms accepted zero commands, not measured
standstill. Do not put the car on the surface until the raised-wheel check
passes. Normal operation retains the existing autonomous three-second startup;
explicit Resume is available after STOP; no WiFi-dependent startup gate was added.

## Current Resume observation and remaining checks

- [ ] Observe normal mode and all existing distance/IR/pitch/motion/scan fields.
- [x] User reports the basic Resume test works after the requested
      Stop -> Resume -> Stop instructions (18 November); no independent capture.
- [ ] Collect detailed wheel/command results and timing on the intended image.
- [ ] Keep counts/path across Resume; preserve latch through browser reconnect.
- [ ] Blocked/stale source or motor-fault admission must not release the latch.
- [ ] A delayed/failed request has no automatic retry or later reconnect release.
- [ ] Repeat STOP while a Resume is pending; STOP must remain usable. Software
      checks cover expiry, replay, session changes and slot contention; record
      hardware evidence separately. Do not inject faults by changing wiring.

## Preceding normal-image observations (9c0dbfd5)

The user confirmed flashing the normal image and connecting the dashboard.
Read-only STATUS reports TEST=false; the initial 45-second capture received
111 main packets and 12 heartbeats, median 400 ms/max 688 ms main, no missing
sequences or decoding/publish errors. Actual nonzero speeds/counts and path
were observed. The user then confirms both wheels rotated and both stopped
when dashboard STOP was clicked. A real 11-byte STOP result with result 0,
controller state 6/latch=true/TEST=false was independently captured, followed
by 84 telemetry samples with zero speeds and unchanged counts. The user now confirms physical STOP persistence through the requested four
IR combinations, close-target scan procedure and dashboard-only reconnect,
then unplugged the car. This completes the functional normal STOP/persistence
check; it does not measure stopping time or floor avoidance.
[User bench record](logs/2026-11-17-normal-user-bench-observation.md).

**Timing acceptance remains open:** the longer 180-second normal run recorded
main max 5499 ms and heartbeat max 9304 ms, despite zero missing sequence
numbers and publish errors. Gaps also occurred after STOP was latched, so
motor motion alone is not established as their cause. The later latched
window also had gaps. The short stationary 60-second success is preserved
as its measured window, not a general wireless guarantee. No control/sensor
algorithms were changed while observing these results.

[Initial live summary](logs/2026-11-17-normal-profile-live-summary.json),
[STOP capture summary](logs/2026-11-17-normal-stop-summary.json),
[latched-window summary](logs/2026-11-17-normal-latch-persistence-summary.json).

## Historical normal image

The preceding v3 normal image (701952 bytes, SHA-256
1a2687d1c3b642626fb8c81b355587f8327e6d32a82e08e6829ae0d02dd1fc08)
passed the user-observed 13 November trial: both wheels rotated, STOP stopped
both, and they stayed stopped through all IR changes and dashboard reconnect
with USB continuously connected. Its local source/image identities and
captures are preserved in [the observation summary](logs/2026-11-13-normal-motor-observation-summary.json)
and [the test history](TEST_LOG.md). These are historical passes. The 9c0dbfd5 observations above are also
historical for the current Resume image.
