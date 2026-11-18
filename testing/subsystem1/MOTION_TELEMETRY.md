# Encoder motion telemetry - 17 November 2026

The missing stationary readings had two causes: TEST=1 did not start S2's
encoder task, and total path length had no producer. Left/right speed,
counts and signed net travel were already supported by S1 Packed v4.

The integrated stationary profile now starts a passive encoder task on core
0, priority 14, every 20 ms. It uses the existing driver and speed estimator.
Wheel GPIOs remain low; no wheel PWM setup/output calls are compiled into
this S2 profile. Duty/RPM commands still return E_OBJ. The normal motor task
uses the same measurement loop and retains its PID, watchdog and controller
arbitration. Explicit IR and encoder bench profiles retain their ownership.
The existing servo scan remains active in the stationary image.

## Measurements

| Dashboard value | Definition |
| --- | --- |
| Wheel velocity | Signed left/right encoder speed, forward positive; m/s |
| Average wheel speed | Instantaneous (abs(left speed) + abs(right speed))/2; m/s, not a run time-average |
| Net travel | Signed mean wheel displacement since S2 startup; can decrease in reverse |
| Distance travelled | Accumulated absolute centre displacement estimated from wheel increments; metres since S2 startup |

The new total-path producer integrates abs((left increment + right
increment)/2) each sample, using signed wheel increments before their mean.
Reverse adds distance. A one-wheel turn contributes half that wheel's
travel. Equal and opposite wheel displacements give zero centre translation,
although average wheel speed remains positive. This centre displacement
convention also appears in the [ROS differential-drive odometry source](https://github.com/ros-controls/ros_controllers/blob/noetic-devel/diff_drive_controller/src/odometry.cpp).
No ROS source or dependency is included in the firmware.

Geometry/calibration are unchanged: 65 mm tyres, left CPR 633.7 and right
CPR 540.9, existing forward signs and 355/113 circumference approximation.
Each wheel increment is converted to micrometres before averaging; fractional
millimetres survive across samples. Per-step integer truncation is below
2 micrometres of centre travel. Slip, tyre dimensions, encoder calibration,
missed edges and reversals within a 20 ms sample limit physical accuracy.
Hand turning raised wheels checks transport and counting, not ground travel.
Encoder validity describes driver/read health; it cannot detect every
disconnected sensor that simply produces no edges.

## Ownership, representation and validity

S2 owns the odometer and strongly implements telemetry_get_travel. The
getter copies path and the original publication timestamp under S2's existing
feedback semaphore with TMO_POL. It leaves output unchanged on error and
never samples hardware. S1 already reads this interface; its serialization,
topics, freshness and packet layout are unchanged.

| Record | Storage |
| --- | --- |
| Task-local odometer | 6 bytes: integer mm:32, fractional micrometres:10, spare zero:6 |
| Protected travel publication | 8 bytes: integer mm:32, timestamp ms:32 |
| Existing v4 path wire field | 32 bits, mm; UINT32_MAX unavailable |
| Browser average | Derived from existing two speed fields; no additional wire bits |

Valid path range is 0..4294967294 mm. Zero is a real measurement. Encoder
failure or overflow makes path unavailable until restart; accumulated
distance is never wrapped or silently repaired after missing movement.
STOP and MQTT/browser reconnect do not reset it. A Pico restart resets it.
S1 expires path after two seconds without a new source publication and
motion after its existing 500 ms limit. Velocity is valid after the first
200 ms window. Unsupported or out-of-range speed is unavailable, not clamped.

The dashboard uses three decimal places for metres and m/s, includes the
derived average in CSV, and plots that same average in the card sparkline.
There is still one 46-byte Packed v4 decoder; no new compatibility format.
S4's nominal-speed assumption and hump algorithm were not changed.

## Offline validation and prepared image

Both full ARM builds pass. Four profile checks confirm the stationary S2
object has passive feedback and no PWM references. Existing normal
controller/PID/STOP/scan tests pass. New tests exercise zero, forward/reverse,
one-wheel motion, counter wrap, fractional accumulation, faults, contention,
overflow and S1 path serialization/expiry. Dashboard tests cover both wheel
contributions and zero versus missing measurements.

Static stack estimates: S2 chain 544/2048, S1 collector 464/2048, S5 chain
136/2048; known startup test/console chains 312/1024 (S1), 208/1024 (S5).
These are compiler estimates, not runtime high-water measurements.

Motion implementation image (superseded locally by the delivery correction): build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2,
731136 bytes, SHA-256
5cc04c0ab0da1dcafb0b61b49f719f8391359048916876ac8af39314e88f311c.
Linked static RAM: 84860 bytes (+168 versus preceding stationary image).
Starting passive feedback adds one 2048-byte dynamically allocated task
stack and kernel task overhead; it reuses existing semaphores, adding no
queue. Normal linked static RAM is 84124 bytes (+8). Runtime peaks remain
unmeasured. The normal check UF2 was removed; its ELF/hash/build log remain.

**At motion preparation: no flash, broker operation or physical pass.**
The user subsequently sees website motion values; later latency investigation
captures real counts/path. Full calibration checks remain open. Use the
current image in [the delivery guide](TELEMETRY_DELIVERY.md).
The user chose to test later. See [build identity](logs/2026-11-17-motion-fingerprint.json),
[test history](TEST_LOG.md) and [later hand-turn checklist](FINAL_ACCEPTANCE.md).
The preceding working firmware is preserved in the
[rollback archive record](logs/2026-11-17-before-motion-feedback-backup.json).
