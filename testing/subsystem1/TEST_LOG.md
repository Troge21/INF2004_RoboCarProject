# Subsystem 1 — test log

Subsystem1 owns WiFi command handling and telemetry (TEAM.md). It currently
monitors radio status and has a locally tested proposed STOP/STATUS parser.
Temporary S5 -> S1 -> MQTT telemetry is now implemented for bench testing.
Controller routing and command execution remain pending. The LED example and its
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
| S1-U22 | Format valid 451 mm sample | Exact JSON with numeric distance and true validity |
| S1-U23 | Format invalid measurement | Distance=null, validity=false |
| S1-U24 | Format into too-small buffer | E_PAR, empty output, guard byte preserved |
| S1-U25 | Null/out-of-range sample | E_PAR from formatter/submission; no queue access |
| S1-U26 | Maximum sequence and valid distance | Exact JSON within fixed buffer |
| S1-U27 | Invalid validity flag/output arguments | E_PAR |
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
[TEST] subsystem1 SUMMARY 14 passed, 0 failed
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

## S5 distance telemetry bench setup

No additional source files were added. S5 now submits each valid or failed
measurement to S1 using `s1_submit_telemetry()`. S1's task formats JSON and
queues it; `mqtt_utk_poll()` publishes only from the existing network task.
This is a temporary IF-13 bench route; replace it with controller routing
through IF-03/09 during final integration. Commands and motors are unchanged.

The telemetry topic defaults to `MQTT_CLIENT_ID "/telemetry"`, normally
`mtk3smp-pico/telemetry`. Existing secrets files need no new setting; an
optional `MQTT_TELEMETRY_TOPIC` overrides it. Heartbeats keep their old topic.

1. Keep Mosquitto running on the computer named by `MQTT_BROKER_IP`.
2. In Ubuntu, from `~/pico/robocar/build_make`, build:
   `make PICO_SDK_PATH="$HOME/pico/pico-sdk" NET=lwip MQTT=1 TEST=1 -j8`.
3. Flash `mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2` and open serial at 115200.
4. On the Windows broker computer, open another PowerShell window and run:

   ```powershell
   & "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 127.0.0.1 -p 1883 -t "+/telemetry" -v
   ```

5. Move a target closer/farther from the ultrasonic sensor. Expect around two
   updates per second (sample timing includes echo wait), for example:

   ```text
   mtk3smp-pico/telemetry {"sequence":12,"obstacle_mm":451,"obstacle_valid":true}
   ```

6. A failed measurement must publish `obstacle_mm:null, obstacle_valid:false`
   as JSON, rather than a numeric clear-space distance. Lack of a nearby target
   does not guarantee failure because a farther surface can still return an echo.

S1 owns one task and two four-message buffers. Input/output sends and network
receives never block. Full queues drop the new sample. The network drains up
to four payloads per poll, sends only the newest fresh one, and discards queued
telemetry while offline. Samples older than 2 seconds are rejected. MQTT uses
QoS 0 without retain; immediate publish errors drop that sample and are logged.
This is best-effort live telemetry, not guaranteed delivery or command handling.
The existing heartbeat RX is independent: telemetry TX appears in serial, and
the external subscriber above verifies delivery (telemetry is not self-subscribed).

Development verification: all 14 local checks passed on the host with the
actual S1 source and kernel/console stubs under address/undefined-behavior
sanitizers (leak checking disabled because the execution environment uses ptrace).
A deterministic host queue simulation also checked input overflow, newest-sample
selection, invalid/null forwarding, and expiry at input/output. These are not
on-board or MQTT integration passes. See integration tests INT-03 through INT-05;
hardware outcomes remain pending.
