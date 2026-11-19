# Subsystem 4 progress

Updated 19 November 2026. Remaining software is bundled for one bench session.

| Step | Work | Status |
| --- | --- | --- |
| Baseline | GY-511 sampling, tilt math and S1 telemetry | Reviewed. GP2/GP3 accelerometer, negative-Y vehicle pitch, 50-sample baseline, and an 18-byte shared snapshot. All 10 existing checks passed in a host harness. Earlier hand tilts demonstrated pitch and state transitions |
| 1 | Make sensor samples and calibration reliable | Passed bench checks. User observed front-raised pitch near 14° and left-raised roll near 5.2°. After the tick correction, both flat samples had 20 ms spacing and pitch 5.4–5.5° against a 5.3° baseline |
| 2 | Detect pickup and stop the motors | User confirmed pickup STOP on the restored build. Earlier trials also confirmed pickup after crossing the ramp. Response tuning deferred |
| 3 | Detect and measure the right-wheel hump | User confirmed detection on the restored build. This version previously read 13 mm on the measured 10 mm ramp. Height calibration deferred |
| 4 | Verify highest peak and new-run reset | Offline checks passed. A smaller hump preserves the maximum. Reset clears accepted values and cancels the in-progress crossing |
| 5 | Classify motion, estimate turn rate and report events | Implemented and checked offline. Dashboard receives the S4 classification and encoder-based turn estimate. Serial records transitions |
| 6 | Finish S4 evidence | User accepted the right-wheel detection and pickup STOP checks for the demo. Hardware timing and accuracy remain open |

Host audit reproduced a stuck-low bus yielding valid-looking zeros, a 39 mm
synthetic estimate without drive feedback, a roll-only miss and a pre-reset
hump updating the new run. The same pitch sequence gave 39 mm at both 20 ms
and 30 ms sample spacing. Physical hump height still needs testing.

The right-side ramp is 195 mm long with a 10 mm peak. The earlier 254 mm capture started latched. Excluded `pickup-curb-20261119-062427-328.csv` per user. The accepted ramp and lift retests on `cfb4768d` each recorded 402 samples at 20 ms with zero sequence gaps.

The [combined powered S4 UF2](build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_s4_motor_trace.uf2) is 741888 bytes, SHA-256 `c396fa80a58b8a547b36db4347203d63e07742c68d39ea069fc656c549054799`. The rebuilt file matches the earlier right-wheel image byte for byte. The user reported that the right-wheel test failed on the intervening `fc6dd216` build, so that change was reverted. The [PowerShell recorder](testing/subsystem4/capture_pickup_trace.ps1) supports `-Trial powered-ramp` and `-Trial powered-lift`.

The [powered right-wheel ramp capture](testing/subsystem4/logs/pickup-powered-ramp-20261119-124241-009.csv) contained 402 consecutive 20 ms samples. The old detector's level check timed out at 3 seconds. After filtering the signed angles over time, replay accepts one right-wheel estimate of 13 mm on the measured 10 mm ramp. The earlier hand-rolled trace still replays at 12 mm.

The [live retest](testing/subsystem4/logs/pickup-powered-ramp-20261119-125015-125.csv) showed 13 mm on the dashboard, 3 mm above the measured peak. Replay matched the final right-wheel estimate and also flagged an earlier 8 mm left-wheel event. Verify crossing repeatability and check for extra events.

After restoring `c396fa80`, the user confirmed right-wheel hump detection and pickup STOP again. This confirmation has no new measured height or response time. The user deferred tuning.

The professor's demo layout, as relayed by the user, places the barcode on
the left and the hump under the right wheel. For S4, flash with the drive
wheels raised and servo free, then test the measured right-wheel ramp.
Check maximum retention if using two heights. Finish by lifting after the
ramp, then restart flat to check the new-run reset.

Height uses pitch and measured travel for full-width humps. For one-wheel
ramps, S4 filters roll over travelled distance and uses the 110 mm wheel
spacing. Turn rate uses the 120 mm effective S2 track and depends on traction.
The powered build and integrated tests passed offline. S4's shared snapshot
grew from 18 to 24 bytes and S1's from 73 to 79 bytes. Linked static RAM is
84220 bytes, 80 bytes above the preceding normal image. The existing
46-byte main frame is joined by a 15-byte terrain report, up to twice per
second. Live accuracy, delivery cadence and runtime stack use still need
measurement on this image.

The [selected powered lift](testing/subsystem4/logs/pickup-powered-lift-20261119-105358-944.csv) recorded 402 consecutive samples at 20 ms. Both motors reached zero duty 60 ms after the latch, and both encoders reported zero RPM by 400 ms. The latch stayed set for the rest of the recording.

The [powered lift from the track](testing/subsystem4/logs/pickup-powered-lift-20261119-110116-975.csv) recorded 400 samples with no missing sequences. Both motors reached zero duty 40 ms after the latch and both encoders reported zero RPM by 500 ms, with no count changes through the end.

The user also confirmed pickup after driving over the ramp. In the earlier [10 mm right-side ramp capture](testing/subsystem4/logs/pickup-curb-20261119-063302-181.csv), the five-sample roll estimate peaked at 9.1° relative to level. At a 110 mm wheel-centre spacing, that single peak suggests 17 mm, so height estimation needs to account for transient acceleration.

Scope comes from the project write-up (pages 2, 3 and 8) and the released demo
pickup-stop requirement.
