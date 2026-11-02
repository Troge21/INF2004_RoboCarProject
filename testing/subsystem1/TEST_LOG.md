git add app_program/subsystem1/subsystem1.c \
        app_program/subsystem1/subsystem1.h \
        app_program/subsystem1/subsystem1_test.c \
        testing/subsystem1/TEST_LOG.md \
        testing/subsystem1/logs/2026-11-02-mqtt-heartbeat.txt# Subsystem 1 — test log

Subsystem1 owns WiFi command handling and telemetry (TEAM.md). It currently
monitors radio status and has a locally tested proposed STOP/STATUS parser.
MQTT/controller queues, telemetry, freshness checks and command execution
remain pending. The LED example and its
S1-U01 through S1-U03 tests are retired; no LED test runs now.

Motor and encoder bench tests were temporarily hosted here. Their results,
calibration trials, and original S1-U04 through S1-U13 historical IDs are
preserved in [the subsystem2 test log](../subsystem2/TEST_LOG.md).
The [reported serial excerpts](../subsystem2/logs/2026-10-26-motor-encoder.txt)
were moved with the motor records; their contents are unchanged.

Encoder calibration now lives in `app_program/subsystem2/subsystem2_test.c`
and is controlled by `S2_ENCODER_CALIBRATION` in `subsystem2.h` (default 0).
There is no motor or encoder test in subsystem1.

## Test cases

| ID | Check / method | Expected result |
|---|---|---|
| S1-U14 | Parse four STOP bytes without a terminator | STOP |
| S1-U15 | Parse STATUS | STATUS |
| S1-U16 | Parse null pointer and empty span | Invalid |
| S1-U17 | Parse GO and lowercase stop | Invalid |
| S1-U18 | Parse incomplete STO | Invalid |
| S1-U19 | Parse STOP NOW and STOP with newline | Invalid |
| S1-U20 | Parse embedded or trailing NUL | Invalid |
| S1-U21 | Parse oversized length | Invalid, no payload access |
| S1-M01 | Build NET=lwip, boot near configured AP, observe USB status | init_done=1, init_result=0, ready=1; link=3 and an IP after connection |
| S1-M02 | Boot with wrong AP password; then restore credentials and rebuild | No connection with wrong credentials; normal connection after restore |
| S1-M03 | Build NET=lwip MQTT=1 against configured broker | mqtt connected/subscribed, recurring heartbeat TX and matching RX |

## Results

| Date recorded | Test ID | Tested commit | Operator / source | Result | Log / evidence |
|---|---|---|---|---|---|
| 2026-11-02 | S1-M01 | Not captured; exact flashed revision unknown | User-provided COM3 output | PASS (observed) | [Serial excerpt](logs/2026-11-02-mqtt-heartbeat.txt): initialization succeeded, link=3, IP 192.168.0.6 |
| 2026-11-02 | S1-M03 | Not captured; exact flashed revision unknown | User-provided COM3 output | PASS (observed) | [Serial excerpt](logs/2026-11-02-mqtt-heartbeat.txt): broker 192.168.0.9 connected, subscription succeeded, matching TX/RX for hb 0 and hb 1 |

These observations confirm WiFi and the existing MQTT heartbeat round trip.
They do not confirm sensor telemetry or remote command integration. The
excerpt contains no parser-test results, so S1-U14 through S1-U21 are not
recorded as hardware passes here. S1-M02 also remains untested.
The date above is the recording date; the capture timestamp was not supplied.
For reproducible project evidence, rerun after committing and append a result
with the exact tested commit ID and operator name; preserve these entries.

## Run on Pico W

Configure `config/wifi_secrets.h` from the example if it does not already
exist. Do not commit credentials. From the repository root:

```sh
make -C build_make PICO_SDK_PATH=/path/to/pico-sdk NET=lwip MQTT=1 TEST=1 -j8
```

Flash `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2` using BOOTSEL.
Open the USB serial console at 115200 baud. Expected local-test summary:

```text
[TEST] subsystem1 SUMMARY 8 passed, 0 failed
```

These parser tests do not require an AP or broker. To test them without
network credentials, omit `NET=lwip MQTT=1`; flash the resulting
`mtk3pico_smp1_usb_cdc_wifi_test.uf2`. The default NET=none build initializes
the radio but does not join an AP. `ready=1` alone does not prove an AP or
MQTT connection. `last reported IP` can be stale after a disconnect.
The SDK link-up value is **3** (the local status-header comment saying 2 is
outdated).

For S1-M03, ensure an MQTT broker is running at the configured IP/port and
the Pico can reach it. This checks the existing heartbeat demonstration;
it does not test S1 command delivery. Publishing STOP or STATUS currently
only reaches the demo logger, not this parser or the motors.

Save serial output under `testing/subsystem1/logs/` and append actual results
with the tested commit ID, operator and evidence as described in
`testing/README.md`. No hardware result is recorded until run on a board.
