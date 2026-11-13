# Controller stationary bench observation — 11 November 2026

Evidence source: user report in the project conversation:

> I have done the 5 test and able to observe the expected result

The five tests are those in the immediately preceding instructions. These
are user-observed functional passes, not agent-captured packets or timings.

| Test | Expected result reported observed | Outcome |
| --- | --- | --- |
| Normal Pico restart | Control disabled, no stop latch | User-reported pass |
| Request status | Matching Pico reply confirmed | User-reported pass |
| Latch stop | Software latch confirmed; motor control remains disabled | User-reported pass |
| Dashboard disconnect/reconnect | Stop latch remains set | User-reported pass |
| Second normal Pico restart | Stop latch clears | User-reported pass |

The intended image was the single current stationary TEST=1 UF2,
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`. At recording time
the local file is still 714752 bytes, SHA-256
`eb29f4ab661a6810fbf43095cbf9052e4fac4100870de6cb158787e8bf9f412b`.
This identifies the intended local artifact; no independent flash readback
or live packet capture was performed to identify the exact running image.
The [build/source fingerprint](2026-11-11-controller-fingerprint.json) remains
the original development-time record; its then-pending hardware status is
superseded by this later user report, without rewriting historical evidence.

This establishes the reported stationary controller/status/latch behavior.
It does not establish actual motor stopping, motor handoff error behavior,
measured response times, the 37-check board summary, independent packet sizes,
new-image outage recovery/cadence, or peak heap/stack use. Those were not
separately reported or captured in this five-test confirmation. No firmware
change, rebuild, flash, live command or outage was performed by the agent
while recording this report.

Next S1 milestone: prepare and observe normal controller/S2 motor inhibition,
then finish final integrated communication and submission evidence. IMU
curb/one-wheel algorithm review remains with Buddy 4; further real measurement
integration depends on the owning subsystem interfaces.
