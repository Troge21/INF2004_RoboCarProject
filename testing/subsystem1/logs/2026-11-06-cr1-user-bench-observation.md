# Packed telemetry bench observation - 6 November 2026

Evidence source: user observation in the local dashboard, reported at about
16:50 Asia/Kuala_Lumpur. This is not an independently captured MQTT or serial log.

The user reports:

> I can see that the line_bits is working as intended, I can also see that
> the recent messages are 6 bytes with a few 5 byte.

This supports functional line-IR display and reception of the expected packed
message sizes: six-byte telemetry and five-byte heartbeats. The user was
instructed to load `mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`; that local
file previously matched the build fingerprint in `2026-11-06-cr1-evidence.json`.
The identity of the running image has not been independently read from the Pico.

Not established by this report:

- Exact observed values for each of the four controlled IR patterns.
- Valid distance readings at known target positions using the packed image.
- Packed-image no-echo behavior, normal cold boot or power-bank restart.
- The expected 25/25 on-device S1 test result or hardware clock measurement.
- Measured publish/heartbeat intervals, outage recovery or runtime RAM peaks.

Next bench steps: test a flat target at two known distances, cold-start normally
on laptop USB and then on the power bank, and check that stale live readings
become unavailable while the car is off. Record actual observations before
marking these checks complete. The earlier JSON-image results remain separate.
