# Normal-mode STOP and persistence - user observation

Date: 17 November 2026, Asia/Kuala_Lumpur.

Intended flashed image: build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2,
716800 bytes, SHA-256
9c0dbfd56d03faf9eca242820f7ad56dd271cde735c83351839b238c34fc3314.
Profile: NET=lwip MQTT=1 TEST=0 APP_MODE=integrated, USB CDC, CRASH_REPORT=0.
The user confirms flashing and reconnecting. This is not flash readback.

The car uses one USB/power-bank supply, with no separate battery switch.
The user confirmed both drive wheels securely raised before flashing and
kept them raised for the test. The servo was required to remain free.

## User-reported functional passes

- Both wheels rotated before dashboard STOP was clicked.
- Both wheels stopped and the dashboard confirmed Stop latched.
- With USB power continuous, both stayed stopped through the requested four
  IR combinations, close front-target/servo-scan procedure, and dashboard-only
  disconnect/reconnect. The latch remained shown.
- The user then unplugged the whole-car supply and left it off.

Final confirmation: "Both stayed stopped; latch persisted; car unplugged".
These physical observations are user-reported, not agent visual inspection
or an independently measured stopping distance/time.

## Independent packet evidence and limits

Normal TEST=false and matching read-only STATUS were observed. A nonretained
11-byte normal STOP result was captured: request3430453922,
session1141228266, result0, control22 (Stop latched, latch true, TEST false).
The observer sent only read-only STATUS; the user clicked dashboard STOP.

The following 84 main samples, starting one second after the result, had
zero measured speeds and fixed counts21088/19074. The later latched window
kept the latch set in all 106 main frames and returned a matching 68 ms
STATUS. That later window also contains small count/speed changes while the
setup was handled; it is not independent proof of perfect standstill. The
observer cannot independently observe another browser's reconnect. The
user's report supplies the physical persistence/reconnect confirmation.

Delivery timing remains open: the longer mixed normal run reached5499 ms
main and9304 ms heartbeat gaps, with no missing main sequences or publish
errors. Gaps also occurred while latched. The earlier short stationary pass
is preserved; these longer misses are not rounded away or attributed to a
particular motor/radio/network component without further evidence.

This closes the normal two-wheel STOP and persistence functional bench
check for the user-confirmed image. It does not close STOP initiated during
an active avoidance turn, floor navigation, speed/distance/height calibration,
repeated recovery, continuous timing, runtime peaks or whole-group acceptance.
No source/firmware change or new flash was needed for this confirmation.

Evidence:

- [Normal build/source identity](2026-11-17-normal-profile-fingerprint.json).
- [Normal initial live summary](2026-11-17-normal-profile-live-summary.json).
- [STOP capture summary](2026-11-17-normal-stop-summary.json).
- [Latched-window summary](2026-11-17-normal-latch-persistence-summary.json).
