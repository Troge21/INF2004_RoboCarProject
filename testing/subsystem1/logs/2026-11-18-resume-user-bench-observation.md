# Resume functional check - user observation

Date: 18 November 2026 (Asia/Kuala_Lumpur).

After the preceding instruction to test Stop -> Resume -> Stop, the user
reported: "I tested it, it works." Record the basic Resume functional check
as a **user-reported pass**. No independent packet/serial capture, timing or
visual observation was collected in this confirmation.

The intended current normal image is
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2`, 721920 bytes, SHA-256
`2c5a4652c7c434fb1d27743a3579b6a9ff0dcf2f2f9a4ee79a813840ecd1bcbe`.
Its local hash was rechecked on this date and matches the prepared build.
This identifies the local file, not independent readback of the Pico flash.
The user did not separately state the flashed hash or current power state.
The [build fingerprint](2026-11-17-resume-fingerprint.json) preserves the
pre-hardware-test state when that record was created; this observation is
the subsequent functional follow-up.

Do not infer from this short confirmation that blocked/faulted/expired
Resume, preserved odometer values, pending-command STOP priority, reconnection
behavior or stopping time were all physically tested. Their software checks
remain recorded; detailed final-image evidence stays on the checklist.

Core S1 communication/telemetry/STATUS/STOP/Resume feature work for available
producer interfaces is implemented. The main outstanding S1 issue is the
previously measured longer normal delivery gaps (main5499 ms, heartbeat9304 ms).
This Resume pass does not close that issue. Final-image recovery/cold boot,
resource evidence and integrated demonstration also remain. Missing barcode
and external fault producers depend on their owners; deferred IMU curb/height
accuracy is not established by this communication test.

Only documentation was changed when recording this report. No firmware build,
flash, car connection, actuator command, broker or hotspot change occurred.
