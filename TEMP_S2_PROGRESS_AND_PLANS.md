# Subsystem 2 progress

Updated 19 November 2026. Finish one feature and verify it before starting the next.

| Step | Work | Status |
| --- | --- | --- |
| Baseline | PWM, both encoders, speed, odometry, and STOP | Reviewed. Working foundation |
| 1 | Correct PID duty ramp when steering changes near an encoder sample | Done. Software checks and all three user bench checks passed |
| 2 | Stop at a requested forward distance using encoders | Encoder stop passed on Pico. 300 mm target, 301 mm cutoff, 304 mm settled, PWM 0. Ground distance unmeasured |
| 3 | Improve straight-line correction with wheel-distance feedback | Pico reported 300 mm cutoff, 303 mm settled, 305/301 mm wheels and 0.5 RPM trim. Floor drift unmeasured |
| 4 | Add signed wheel commands and a measured reverse move | Four Pico passes. Floor encoder cutoff −300 mm both times, settled −303 to −304 mm. PWM 0 |
| 5 | Turn by encoder-measured angle and calibrate effective track width | Visual U-turn pass at 120 mm effective spacing on the test surface. Requested −180°, encoder settled −182.9°, PWM 0. Physical spacing measured 110 mm |
| 6a | Connect S5's left/right request to one encoder-controlled turn | Passed both raised-wheel directions with the 20° turn and 120 mm calibration. User confirmed east and west. Live west telemetry showed Turning left at −42.8 mm/s left and +41.1 mm/s right |
| 6b | Known-obstacle bypass and IR line rejoin | Paused. Raised-wheel rejoin passed. Tape confirmation at the return cutoff was fixed and checked in software. Full taped-track run pending |
| 6b follow-up | Size-aware bypass and barcode-triggered maneuvers | Pending S5 width measurements and S3 barcode events/code mapping |
| 7 | Wire the S4 pickup event to a latched end-of-run motor stop | Controller checks passed during line following and bypass. User confirmed pickup STOP on the restored S4 bench image. The latch clears at Pico restart |
| 8 | Audit and pack S2 shared data. Check floor distance and turn accuracy | Bench checks complete. Command mailbox 20 → 7 B and shared feedback 132 → 72 B. Matched Pico links save 72 B static RAM and add 496 B code. Floor distance ≈310 mm for 300 mm target, encoder settled 305 mm. At 120 mm calibration, the −180° turn visually aligned with the tape in reverse, encoder settled −183.6°. Both ended at PWM 0 |

Step 8 used the standalone 300 mm distance (`58370269`) and −180° turn (`8ae3ab45`) builds. Floor distance was approximate and heading was checked visually.

For normal driving and S1 MQTT, use the [integrated image](build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2) with the latest S2 packing. The [dashboard README](dashboard/README.md) records its current identity.

Step 6b uses `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_s2_bypass.uf2`
(735744 bytes, SHA-256 starts `35f3d4a3`). Raise both drive wheels before
flashing and leave the servo clear. Keep the wheels raised for the entire
bench run, or start on the tape and stay on the floor for the full track test.
