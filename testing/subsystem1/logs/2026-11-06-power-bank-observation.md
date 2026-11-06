# Power-bank telemetry observation - 6 November 2026

Development diagnostic observation, not a commit-pinned formal hardware pass.
The user confirmed that the Pico was powered through USB from a power bank.
A flat book approximately 20 cm in front of the ultrasonic sensor was requested;
actual target geometry and supply voltage were not independently measured.

Observed for 12 seconds using the dashboard's actual MQTT WebSocket client and
JSON decoder against `ws://127.0.0.1:9001`, with the configured car telemetry
and heartbeat topics:

| Observation | Result |
| --- | --- |
| Accepted telemetry | 23 messages |
| Rejected telemetry | 0 messages |
| Valid ultrasonic measurements | 0 |
| Invalid ultrasonic measurements | 23 |
| Heartbeats received | `hb 7`, `hb 8` |
| Last telemetry interval | 500 ms |
| Sampled IR field | `line_bits:4` (valid centre/right snapshot, both background) |

Actual received example:

```json
{"sequence":79,"obstacle_mm":null,"obstacle_valid":false,"line_bits":4}
```

The captured examples continued through sequences 80 to 86 with the same
invalid-distance and line-bit values. No sensor samples were injected.

This confirms that WiFi/MQTT and telemetry delivery continued during this
observation while the ultrasonic measurement failed. It does not identify
the reason for failure or prove the physical IR black/background response.
The flashed firmware revision was not independently identified; the wire
messages include the new IR field.

## Laptop USB comparison

The user confirmed moving USB to the laptop and reported that ultrasonic VCC
is connected to Pico **3V3**. In a further 12-second capture:

| Observation | Result |
| --- | --- |
| Accepted telemetry | 26 messages |
| Rejected telemetry | 0 messages |
| Valid ultrasonic measurements | 0 |
| Invalid ultrasonic measurements | 26 |
| Heartbeats received | `hb 19`, `hb 20`, `hb 21` |
| Last telemetry interval | 500 ms |

See [actual laptop capture](2026-11-06-laptop-usb-capture.json). Thus the
observed failure is not specific to power-bank operation. The Pico continues
communicating while the ultrasonic measurement fails on both sources.

The standard HC-SR04 is specified for 5 V according to its
[datasheet](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf).
The user subsequently confirmed the module is **HC-SR04** and **ECHO is
connected directly to GP5**, without a voltage divider. This differs from
the existing S5 wiring specification in two respects: VCC requires regulated
5 V, and ECHO must be level-reduced before GP5 when the module is powered at
5 V. The correction must be assembled with all power disconnected, including
the ECHO divider before increasing the sensor supply. The project specifies
2.2 kOhm from ECHO to the GP5 junction and 3.3 kOhm from that junction to GND
(nominal 3.0 V from a 5 V ECHO).

No wiring or firmware was changed during these captures. Corrected wiring
and a successful retest are still pending. Physical supply voltage,
TRIG/ECHO waveforms and exact failure code have not been measured; hardware
damage has not been established.

## Later clarification and software finding

The user clarified that readings work immediately after flashing via BOOTSEL,
then fail after switching USB power and remain failed when returned to the
laptop. Resistors were not supplied with the project. Thus the supply-voltage
discrepancy alone does not explain the complete reported sequence, and the
earlier wiring conclusion was too strong.

Firmware review found that the project's normal startup never configures
WATCHDOG.TICK for 1 MHz. The RP2040 TIMER used for ultrasonic pulse timestamps depends
on this tick. Raspberry Pi's ROM USB boot path enables it, so entering
BOOTSEL can hide this missing initialization. The local board-clock patch
now starts the 1 MHz tick explicitly on every boot and adds actual hardware
clock test S1-U43. The build succeeded; a post-flash versus normal-power-on
comparison is still required. No voltage or wiring change is needed for
that comparison, and the sensor supply must not be raised to 5 V with
ECHO still directly connected to the Pico.

The first image with the tick initialization restored valid readings after
flashing: 24/24 valid distances, 159..178 mm, with no wiring change. The
initial kernel-sleep-based clock check failed; that failure is preserved in
the test log. After replacing it with an isolated CPU-timed interval, the
final image measured 1009 us for a requested 1000 us busy wait and passed
all 21 S1 checks. A normal power-on comparison is still needed because
post-flash success was already part of the user's original observation.

## Result of subsequent normal-power-on checks

After complete power-off and a normal laptop USB start without BOOTSEL,
all 21 S1 checks passed again (hardware timer 1009 us for a 1000 us CPU wait),
and 24/24 captured distances were valid at 162..163 mm.

For normal power-bank startup, the initial 15-second capture and a later
25-second capture had no car messages. On a subsequent user-requested
recheck with the board light reported on, 24/24 distances were valid at
117..147 mm; three heartbeats and fresh line_bits=5 messages also arrived.
See [power-bank success capture](2026-11-06-timer-fix-powerbank-recheck.json).

This supports the startup clock fix as addressing the BOOTSEL-dependent
measurement failure without a hardware change. The initial silent windows
remain a separate startup/recovery observation; their cause and recovery
time were not established. No distance accuracy calibration or guarantee
of reliable operation at an out-of-spec supply is claimed.
