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
| S1-U22 | Encode valid 451 mm sample | Exact six-byte packed v2 vector |
| S1-U23 | Encode invalid measurement | Distance code zero; browser decodes null/false |
| S1-U24 | Encode into too-small buffer | E_PAR, output/guard bytes unchanged |
| S1-U25 | Null/out-of-range sample | E_PAR from formatter/submission; no queue access |
| S1-U26 | Maximum sequence and valid distance | Exact packed bytes, no value truncation |
| S1-U27 | Invalid validity flag/output arguments | E_PAR |
| S1-U28 | Left line (firmware centre) black/right background at age 100 ms | Exact packed bytes, line_bits=6 |
| S1-U29 | Fresh all-background snapshot | line_bits=4, distinct from unavailable |
| S1-U30 | IR age 101 ms with fresh distance | line_bits=0, distance preserved |
| S1-U31 | Fresh right-black snapshot across 32-bit uptime wrap | line_bits=5 |
| S1-U32 | Source timestamp in the future | line_bits=0 |
| S1-U33 | Noncanonical/reserved line bits | E_PAR, output unchanged |
| S1-U34 | Host queue pipeline, both IR inputs black | line_bits=7 |
| S1-U35 | Host advances clock after output enqueue | IR expires; distance preserved |
| S1-U36 | S3 snapshot read reports busy/unavailable | line_bits=0, no reused data |
| S1-U37 | S3 snapshot reports unsupported pattern | line_bits=0 |
| S1-U38 | S3 snapshot is already stale when gathered | line_bits=0 |
| S1-U39 | Fill input queue, submit fifth sample, poll output | Fifth send E_TMOUT; newest accepted sample selected; all-background=4 |
| S1-U40 | Fill outbound queue, submit fifth sample | New sample dropped without blocking; newest accepted sample selected |
| S1-U41 | Host expires distance while in input queue | No output published |
| S1-U42 | Host expires distance while in outbound queue | E_TMOUT, no payload returned |
| S1-U43 | Pico-only: read tick enable/divisor and compare hardware timer over a 1000 us CPU/SysTick busy interval, with local interrupts masked | Configured 1 MHz tick; elapsed hardware time 900..1100 us; repeat on normal cold boot |
| S1-U44 | Check packed application object sizes | Measurement=6 bytes, local line snapshot=5 bytes |
| S1-U45 | Encode heartbeat counter 0 and uint32 maximum | Exact five-byte golden vectors |
| S1-U46 | Heartbeat null/too-small output | Error with no buffer writes |
| S1-U47 | Raw values that could alias valid values if masked first | Out-of-range distance and line pattern rejected before packing |
| S1-M01 | Build NET=lwip, boot near configured AP, observe USB status | init_done=1, init_result=0, ready=1; link=3 and an IP after connection |
| S1-M02 | Boot with wrong AP password; then restore credentials and rebuild | No connection with wrong credentials; normal connection after restore |
| S1-M03 | Build NET=lwip MQTT=1 against configured broker | mqtt connected/subscribed, recurring heartbeat TX and matching RX |
| S1-M04 | Flash IR image; compare four C/R black/background patterns; disconnect car | Raw line_bits=4/6/5/7 respectively; dashboard matches C/R; left reserved for barcode; live values unavailable after freshness timeout |
| S1-M05 | Compare BOOTSEL post-flash operation with complete power-off and normal USB/laptop and power-bank boots | Clock test S1-U43 passes on normal boot; distance measurements resume without reflashing; capture each power source separately |
| S1-M06 | Flash packed v2, use Auto/Packed v2 dashboard, move target and check IR patterns | Six-byte telemetry, five-byte heartbeats; real distance and correct left/right states; unavailable stays distinct from background/clear space |
| S1-M07 | Compare timing before/after packing under matched conditions; exercise AP/broker outages | Required telemetry/heartbeat intervals and recovery targets met; record actual timings, failures and conditions |

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
[TEST] subsystem1 SUMMARY 25 passed, 0 failed
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

## Earlier JSON distance telemetry bench setup

S5 submits each valid or failed measurement to S1 using
`s1_submit_telemetry()`. S1 gathers a nonblocking protected S3 line snapshot
and queues raw snapshots. `s1_network_poll()` rechecks IR freshness and
formats JSON; `mqtt_utk_poll()` publishes only from the existing network task.
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
   & "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 172.20.10.2 -p 1883 -t "+/telemetry" -v
   ```

5. Move a target closer/farther from the ultrasonic sensor. Expect around two
   updates per second (sample timing includes echo wait), for example:

   ```text
   mtk3smp-pico/telemetry {"sequence":12,"obstacle_mm":451,"obstacle_valid":true,"line_bits":6}
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

## Centre/right IR extension - 6 November 2026

Use the laptop's actual WiFi address for the subscriber above (172.20.10.2
is the current bench address). The default Windows broker service on
127.0.0.1:1883 is separate from the dual-listener bench broker.

S1 uses the existing S3 snapshot reader; S3 GPIO code and the kernel network
service are unchanged. Bit 2 of `line_bits` means valid, bit 1 means centre
black and bit 0 means right black. Values 0 and 4 differ: unavailable versus
valid all-background. Timestamp checks use unsigned uptime subtraction and
the controller's 100 ms IR age limit, including after outbound queue delay.
This is still a JSON bench protocol, not the complete packed-data change.

S1-U28 through S1-U33 run in the on-device test image and on the host.
S1-U34 through S1-U42 are host-only queue/scheduler checks in
`host/test_telemetry.c`; they compile the real S1 C sources and public
interfaces with stub kernel queues, clock and S3 reader. They never send
samples to a live MQTT broker. Windows host reproduction, at repository root:

```powershell
$hostTelemetryExe = Join-Path $env:TEMP 'robocar-s1-ir-tests.exe'
& 'C:\msys64\ucrt64\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror -I testing/subsystem1/host/include testing/subsystem1/host/test_telemetry.c -o $hostTelemetryExe
if ($LASTEXITCODE -eq 0) { & $hostTelemetryExe }
```

On Linux/WSL, use `gcc` with the same flags and a temporary output path.

Development checks on the working copy based on commit `b4f6a6a`:

| Check | Development outcome | Evidence |
| --- | --- | --- |
| Actual S1 parser/formatter source on host | 20 passed, 0 failed | [Host log](logs/2026-11-06-ir-host-tests.txt) |
| Actual S1 queue pipeline on host | 9 passed, 0 failed | Same host log, S1-U34 through S1-U42 |
| Dashboard MQTT/protocol tests | 20 passed, 0 failed | [Node log](logs/2026-11-06-ir-dashboard-tests.txt) |
| WiFi/MQTT TEST firmware | Build succeeded | [Build output](logs/2026-11-06-ir-firmware-build.txt) |
| Physical four-pattern IR / disconnect trial S1-M04 | PENDING | Flash and run with real sensors; no hardware pass claimed |
| Visual browser QA | NOT RUN | Browser-control host unavailable |

The [source fingerprint](logs/2026-11-06-ir-source-fingerprint.json) identifies
the tested working-copy files and UF2. These are development checks, not
commit-pinned submission results. After review/commit, rerun the required
checks and append formal results with the tested commit and saved evidence.
Existing historical results above remain unchanged.

## BOOTSEL versus normal boot timebase - 6 November 2026

The user clarified that distance works just after flashing through BOOTSEL,
then fails after disconnect/reconnect and remains failed on laptop USB.
The earlier supply-voltage concern does not establish this failure's cause.

Code review found that `startup_clock()` never started WATCHDOG.TICK, which
also supplies the RP2040 hardware TIMER used by S5 pulse widths and S3
barcode sampling. The port does not execute Pico SDK runtime clock init.
Raspberry Pi's [ROM USB boot path](https://github.com/raspberrypi/pico-bootrom/blob/master/bootrom/bootrom_main.c)
enables the tick, so BOOTSEL can mask the omission. The normal flash path
does not perform that same USB initialization.

The local fix configures the tick after configuring clk_ref, with divisor 12
for a 12 MHz reference, giving 1 MHz. This enables the tick generator,
not the watchdog reset mechanism. Kernel startup is shared platform code
and should be included in the team's review of this patch.

S1-U43 is an actual on-board clock-rate check. It is intentionally omitted
from host tests, which cannot test the physical timer. Expected current Pico
summary is 21 checks; the host parser/formatter suite remains 20 plus the
9 queue checks. The existing IR evidence above predates this additional check.

The updated WiFi/MQTT TEST image built successfully; see
[build output](logs/2026-11-06-timer-startup-build.txt). S1-M05 remains pending
until cold boots and live distance measurements are observed. The earlier
image is preserved as
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test_before_tick_fix.uf2`.

Do not raise the HC-SR04 supply to 5 V with ECHO directly connected to GP5.
Hardware voltage requirements remain a separate integration issue; no
resistor purchase or wiring change is needed to evaluate the timer fix.

### Timing-check revisions and observed results

The first check compared a 100 ms kernel sleep to the hardware timer during
startup. It recorded 228722 hardware microseconds versus 110 kernel
milliseconds and failed. That check combined hardware clock configuration
with kernel tick accounting during initialization, so the mismatch did not
isolate the hardware timebase. Preserve its
[original FAIL output](logs/2026-11-06-timer-fix-post-flash-serial.txt).
The initial image is archived as
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test_tick_fix_initial_check.uf2`.

The revised S1-U43 reads the tick enable/divisor and uses a 1000 us CPU/SysTick
busy wait with local interrupts masked. It makes no kernel calls while masked
and runs for less than one 10 ms kernel tick. On the actual Pico after the
final flash, it measured **1009 us**, and the S1 summary was **21 passed,
0 failed**. See [final post-flash serial output](logs/2026-11-06-timer-fix-final-post-flash-serial.txt)
and [final build output](logs/2026-11-06-timer-startup-final-build.txt).

The earlier post-flash live capture also received **24/24 valid distances**
(159..178 mm) and two heartbeats with unchanged wiring; see
[capture](logs/2026-11-06-timer-fix-post-flash.json). Post-flash success alone
does not demonstrate the normal-boot fix: the user already reported that
BOOTSEL flashing temporarily restored readings. S1-M05 requires the complete
power-off comparisons below before claiming the reported fault is resolved.

### Normal laptop USB cold boot

The user confirmed switching off the car battery, removing USB for 5 seconds,
and reconnecting laptop USB normally without BOOTSEL. On this boot:

- S1-U43 passed: **1009 us** for a requested **1000 us** CPU wait.
- All **21 S1 checks passed**, zero failed.
- A 12-second live capture received **24/24 valid ultrasonic samples**,
  **162..163 mm**, two heartbeats, no rejected messages, last interval 499 ms.
- Sensor wiring and reported 3V3 supply were unchanged.

Evidence: [normal-boot serial](logs/2026-11-06-timer-fix-normal-laptop-serial.txt),
[live MQTT capture](logs/2026-11-06-timer-fix-normal-laptop.json),
[final source/image fingerprint](logs/2026-11-06-timer-startup-final-fingerprint.json).
This is observed development validation of the working copy. A power-bank
cold boot remains the next comparison; neither test is an accuracy calibration
or approval of an out-of-spec sensor supply.

### Normal power-bank boot and later successful observation

The user confirmed complete power-off followed by normal USB power-bank
startup without BOOTSEL. The first 15-second observation and a subsequent
25-second observation received no car messages, while broker subscriptions
were ready. Do not discard those observations:
[initial capture](logs/2026-11-06-timer-fix-normal-powerbank.json),
[extended capture](logs/2026-11-06-timer-fix-normal-powerbank-extended.json).

The user then reported the board light was on and requested another check.
That 12-second capture received **24/24 valid ultrasonic readings**,
**117..147 mm**, **three heartbeats** (`hb 19`, `hb 20`, `hb 21`), no rejected
messages, and fresh IR snapshots (`line_bits:5`, centre background/right black).
See [successful power-bank recheck](logs/2026-11-06-timer-fix-powerbank-recheck.json).
No supply-voltage or wiring changes were made during this troubleshooting.

The normal laptop cold boot and the later power-bank reading success support
the startup-clock correction for the reported BOOTSEL dependency. However,
WiFi association/startup timing and any intervening physical actions were not
continuously instrumented, so this is **not** a measured recovery-time pass
or proof of successful readings immediately after every power-bank boot.
Retest S1-M05 after committing, with continuous timestamps and repeated boots.
IR messages arrived, but the four known physical line patterns remain a
separate pending test.

## Change Requirement 1 packed telemetry stage - 6 November 2026

The latest built image uses six-byte binary distance/IR telemetry and
five-byte binary heartbeats. Its expected S1 on-device summary is now
**25 passed, 0 failed** (24 codec/parser checks plus hardware clock check
S1-U43). The successful 21-check JSON image trials above remain historical
evidence. The user now reports working line bits and six-/five-byte messages
from the packed bench session; on-device test output and physical timing
have not been captured for this image.

The source constructor validates raw ranges before packing, avoiding silent
bit truncation. Input/output records are 10/14 bytes with compile-time ARM
size assertions. Kernel message buffers round each record and add an INT
header; requests are therefore 64/80 bytes for four slots, not just four
times the raw data lengths. The host queue harness enforces that capacity.

Development validation of the current working copy:

| Check | Outcome | Evidence |
| --- | --- | --- |
| Actual S1 C codec/parser | 24 passed, 0 failed on host | [Final host log](logs/2026-11-06-cr1-host-final-tests.txt) |
| Actual S1 C queue pipeline | 9 passed, 0 failed with kernel-aligned capacity | Same host log, S1-U34..U42 |
| Browser protocol/MQTT/inspector tests | 27 passed, 0 failed | [Node log](logs/2026-11-06-cr1-dashboard-tests.txt) |
| Actual C bytes decoded by JS and preserved through MQTT framing | 19914 telemetry vectors + 3 heartbeats passed; all 114 reserved distance codes rejected by C | [Contract log](logs/2026-11-06-cr1-c-to-js-tests.txt) |
| ARM WiFi/MQTT TEST image | Build succeeded | [Final build](logs/2026-11-06-cr1-build-final.txt) |
| Packed Pico-to-browser delivery S1-M06 | PARTIAL, user-reported | Working line bits and six-/five-byte messages; distance, unavailable states and controlled per-pattern results remain to be recorded. [Observation](logs/2026-11-06-cr1-user-bench-observation.md) |
| Packed timing/recovery and runtime RAM S1-M07 | PENDING | Physical measurements required |

See [implementation, bit table and measurements](CHANGE_REQUIREMENT_1.md)
and [source/image fingerprint and comparison data](logs/2026-11-06-cr1-evidence.json).
Measured example payload: 69 to 6 bytes. Requested S1 queue buffers: 192 to
144 bytes. Linked data+bss: 82740 bytes before and after. Runtime heap/stack
high-water use is not measured. The five-byte heartbeat is larger than
`hb 0` (4 bytes), equal to `hb 12`, and smaller than the full-range counter
text (13 bytes); the report explains its compatibility marker.

The original ASCII command parser remains a local proposed vocabulary,
not the final compact remote-command protocol. Other subsystem state,
controller/fault data and commands still need group-wide CR1 work. Review,
commit and rerun formal evidence against the final revision before submission.
