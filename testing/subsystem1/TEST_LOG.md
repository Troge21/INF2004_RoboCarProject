# Subsystem 1 — test log

Subsystem1 owns WiFi command handling and telemetry (TEAM.md). It currently
publishes real telemetry and handles read-only binary STATUS requests/replies.
Temporary S5 -> S1 -> MQTT telemetry is now implemented for bench testing.
Controller routing and actuator-command execution remain pending; STATUS itself is read-only. The LED example and its
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

S1-U14..U21 (the unconnected ASCII STOP/STATUS parser) are retired.
Historical results remain below; current request/reply tests are U62..U69.

| ID | Check / method | Expected result |
|---|---|---|
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
| S1-U48 | V3 independent golden vector and snapshot layout | Exact 11-byte frame; 13-byte local IMU snapshot |
| S1-U49 | Signed pitch limits and maximum accepted height | -900..900 tenths and 80 mm retained |
| S1-U50 | Raw IMU range errors | Reject before truncation |
| S1-U51 | Queued pitch and S4 publication expiry | Pitch expires after 100 ms; whole S4 block after 1000 ms |
| S1-U52 | IMU uptime wrap/future timestamps | Wrap stays fresh; future source unavailable |
| S1-U53 | IMU reserved bits | Reject without output writes |
| S1-U54 | V3 buffer limits | Null/small output rejected |
| S1-U55 | Contradictory IMU fields | Reject invalid state/validity/height ordering |
| S1-U56 | Host S4 to S1 queue pipeline | Actual pitch and retained max forwarded |
| S1-U57 | Host queued pitch expires | Retained height remains, pitch unavailable |
| S1-U58 | Host busy S4 getter | Explicit unavailable IMU; distance/IR unaffected |
| S1-U59 | Host stale S4 publication | Getter cannot refresh old source data |
| S1-U60 | Host sensor fault | Fault status and retained height; no valid live pitch |
| S1-U61 | Host invalid S4 values | Unavailable IMU rather than masked/truncated values |
| S1-U62 | Parse binary STATUS request | Exact correlation ID |
| S1-U63 | Null, oversized and ASCII requests | Invalid; no execution |
| S1-U64 | Zero ID/null result | Error; output unchanged |
| S1-U65 | No-sample reply | Exact 11-byte golden vector |
| S1-U66 | Fresh-sample reply | Exact header plus current telemetry; 22 bytes |
| S1-U67 | Small reply buffers | Error without writes |
| S1-U68 | Invalid frame/age/build flag/ID | Rejected before writes |
| S1-U69 | Full counters and presence boundaries | uint32 values preserved; canonical presence |
| S1-U74 | Host STATUS before sensor samples | No sample, test-build flag and operating uptime |
| S1-U75 | Host STATUS after real S1 queue cache | Source age and actual cached values |
| S1-U76 | Cached IR and telemetry expire | IR clears independently, then no sample |
| S1-U77 | Unsupported request | Queues and output unchanged |
| S1-U78 | Uptime wrap and initialization | Age correct; cache marker reset |
| S1-M01 | Build NET=lwip, boot near configured AP, observe USB status | init_done=1, init_result=0, ready=1; link=3 and an IP after connection |
| S1-M02 | Boot with wrong AP password; then restore credentials and rebuild | No connection with wrong credentials; normal connection after restore |
| S1-M03 | Build NET=lwip MQTT=1 against configured broker | Connected, real command subscription and recurring heartbeat; current round-trip proof is STATUS (older heartbeat-loopback records are historical) |
| S1-M04 | Flash IR image; compare four C/R black/background patterns; disconnect car | Raw line_bits=4/6/5/7 respectively; dashboard matches C/R; left reserved for barcode; live values unavailable after freshness timeout |
| S1-M05 | Compare BOOTSEL post-flash operation with complete power-off and normal USB/laptop and power-bank boots | Clock test S1-U43 passes on normal boot; distance measurements resume without reflashing; capture each power source separately |
| S1-M06 | Flash packed v2, use Auto/Packed v2 dashboard, move target and check IR patterns | Six-byte telemetry, five-byte heartbeats; real distance and correct left/right states; unavailable stays distinct from background/clear space |
| S1-M07 | Compare timing before/after packing under matched conditions; exercise AP/broker outages | Required telemetry/heartbeat intervals and recovery targets met; record actual timings, failures and conditions |
| S1-M08 | Flash IMU/v3, keep stationary for calibration, gently raise/lower front | 11-byte telemetry, 5-byte heartbeats, actual changing pitch and S4 status; existing distance/IR still work; nominal speed assumption visible |
| S1-M09 | Request STATUS from website and verify matching Pico response | Matching ID, correct operating uptime/build mode/source age; live telemetry unchanged; timeout/cancel/invalid-message cases tested |

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
[TEST] subsystem1 SUMMARY 33 passed, 0 failed
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

The original v2 image in this stage uses six-byte binary distance/IR telemetry and
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
| Packed Pico-to-browser delivery S1-M06 | Functional PASS, user-reported | Working line bits, six-/five-byte messages, target movement, stale readings, normal laptop and power-bank restarts. Exact per-pattern values and timings not captured. [Observation](logs/2026-11-06-cr1-user-bench-observation.md) |
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

## IMU/v3 integration stage - 6 November 2026

The previous section's four v2 functional bench checks passed according to
the user. A new v3 image is now built, **not flashed by the agent**:
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`.
The identical `_imu` alias and obsolete variants were removed at the user's
request; the retained image matches the recorded IMU build hash.
Expected S1 board summary: **33 passed, 0 failed**, not observed yet (32
software checks plus clock check S1-U43). The 15 queue checks are host-only.

| Check | Outcome | Evidence |
| --- | --- | --- |
| Actual S1 codec/parser + queue source | 32 + 15 passed, zero failures | [S1 host log](logs/2026-11-06-imu-s1-host-tests.txt) |
| Actual S4 source, stub kernel/driver | 16 snapshot and owner-loop checks passed | [Snapshot log](logs/2026-11-06-imu-snapshot-tests.txt) |
| Dashboard protocol/MQTT/store | 32 passed | [Node log](logs/2026-11-06-imu-dashboard-tests.txt) |
| C bytes to JS decoder and MQTT framing | 19914 v2 + 4083 v3 frames, 3 heartbeats passed | [Contract log](logs/2026-11-06-imu-c-to-js-tests.txt) |
| ARM WiFi/MQTT TEST image | Build passed | [Build log](logs/2026-11-06-imu-firmware-build.txt) |
| Physical IMU/v3 S1-M08 | PENDING | Actual calibration/tilt, distance/IR regression, normal restarts and stale behavior still required |
| Height accuracy, timing and peak RAM | PENDING | Actual course/encoder data and measured trials required |

The integration fixes actual-vs-baseline pitch, source timestamps and
snapshot synchronization. S4 retains sensor ownership; its API/snapshot
changes need owner review. Heights remain nominal-speed estimates until
measured speed is supplied. See [IMU contract and bench steps](IMU_TELEMETRY.md)
and [build/source fingerprint](logs/2026-11-06-imu-evidence.json).
Browser visual QA unavailable: browser access host unavailable.

## Current-format decoder and pulled firmware - 10 November 2026

The user clarified that the reported failure was a setup issue: the system
works as intended and the car is powered by laptop USB. No IMU regression
was reproduced, and no rollback or new S4/driver change was made in this
update. Existing local IMU work is preserved alongside the pulled changes.

Pulled base revision: `89b582fb870397c1e645226692ecc4cc6f2511ae` plus the local working-copy IMU/decoder changes.
The latest commits separate S3 line sensing, tune barcode tolerance/ratio
checks and update normal-build motor duty. Those changes are preserved.

The UF2 was force-rebuilt (`make -B`) so all sources and profile flags were
compiled together, including the new line_detection module. The linked image
contains the BC-TRACE-1 diagnostic marker. Flags remain
`NET=lwip MQTT=1 TEST=1 SMP=1 CONSOLE=usb_cdc`: this stationary test profile
disables automatic driving. No firmware was flashed by the agent.

Decoder cleanup: removed compact-v1, v2, JSON and ASCII-heartbeat fallback
implementations and the format selector. Only 11-byte current telemetry and
five-byte binary heartbeats are accepted. The six-byte distance/IR prefix
still exists inside the current C frame; it is not a separately supported
legacy transport. Malformed/obsolete messages are rejected clearly. A missing
or faulted IMU does not invalidate otherwise valid distance/IR.

| Check | Result | Evidence |
| --- | --- | --- |
| S1 codec/parser + queue tests | 32 + 15 passed | [S1 log](logs/2026-11-10-s1-tests.txt) |
| Actual S4 snapshot/owner-loop source, stub driver/kernel | 16 passed | [S4 log](logs/2026-11-10-s4-snapshot-tests.txt) |
| Current dashboard protocol/MQTT/store | 26 passed | [Node log](logs/2026-11-10-dashboard-tests.txt) |
| Actual C frames decoded by JS and MQTT-framed byte-for-byte | 23997 current-format telemetry cases + 3 heartbeats passed | [Wire log](logs/2026-11-10-wire-tests.txt) |
| Pulled actual S3 barcode/line/capture/controller source | 16 host test groups passed | [S3 log](logs/2026-11-10-barcode-tests.txt) |
| Full ARM WiFi/MQTT TEST build | Passed | [Build log](logs/2026-11-10-firmware-build.txt) |
| Local dashboard assets | index.html, app.js and protocol.js served matching local files; JS syntax passed | HTTP check |
| Visual browser inspection | Not available | Browser access host unavailable |

Current single flash file: `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`
(704000 bytes). Linked text/data/bss: 351748/100/82664 bytes;
static data+bss = 82764 bytes. Runtime heap/stack peaks remain
unmeasured. No additional UF2 variant was created.

- Current UF2 SHA-256: `32e4fb5c6629c6a70a892a422ea078016799d09bc3e94d3c2d2a8c29a155945b`
- Current dashboard protocol.js SHA-256: `2392f3df96c17e7f906a39cbbb6bb465b75a2b5a22d5595c876d534ed53518b6`

Older fingerprints describe their capture-time images; the standard UF2 path
now contains this rebuilt image. Refresh the dashboard and reconnect after
flashing; there is no decoder setting. Expect 11-byte accepted telemetry,
five-byte heartbeats and the same real measurements. S1 on-device summary
is still expected to be 33/33; the new image's board output has not been captured.
Physical sensor accuracy, repeated recovery timing and nominal-speed hump
height accuracy remain separate pending checks. Review/commit the final
working copy for submission; these are development results.

## Read-only STATUS request/reply - 10 November 2026

Implemented the authorized dashboard -> MQTT -> Pico -> correlated STATUS
reply path. No motor action or IMU algorithm change. The user explicitly
deferred the one-wheel/curb IMU test case to Buddy 4 for later review.

- Requests: five bytes, a0 + nonzero uint32 ID. Replies: a1 + echoed ID,
  actual boot uptime, source age/test-build bits and an optional fresh
  current telemetry snapshot (11 bytes without, 22 with snapshot).
- Command subscription replaces the obsolete heartbeat-loopback subscription.
  One five-byte slot; strict topic/size/type/ID checks; bounded fragments,
  one accepted request per 250 ms, retry on subscription failure, pending
  requests discarded on disconnect. The network task exclusively owns it.
- Browser subscribes to telemetry, heartbeat and status reply; all three
  SUBACK results must succeed. One pending request, three-second timeout,
  fresh IDs, no automatic retry. Retained/unmatched/late replies are ignored.
- STATUS reads the network-owned timestamped sensor cache and recomputes
  validity; it never updates the browser's live sequence/history. STOP stays
  disabled; actuator expiry/controller handoff is not implemented here.
- Stock lwIP incoming callbacks do not expose MQTT retain, so firmware does
  not claim retained-command rejection. Repeated STATUS remains read-only;
  old responses cannot satisfy a different browser request ID.
- Legacy ASCII STOP/STATUS parser removed; U14..U21 retired. Pure request/reply
  tests U62..U69 replace those local tests. On-device S1 expected total stays
  33, including hardware clock check; serial test output not captured yet.

| Check | Result | Evidence |
| --- | --- | --- |
| Actual S1 codec + queue/cache source | 32 + 20 passed | [S1 log](logs/2026-11-10-status-s1-tests.txt) |
| Actual MQTT port with stub transport/timers | 17 passed | [Port log](logs/2026-11-10-status-mqtt-tests.txt) |
| Dashboard codec/store/MQTT client | 35 passed | [Node log](logs/2026-11-10-status-dashboard-tests.txt) |
| Actual C bytes to JS/MQTT framing | 23997 telemetry + 3 heartbeat cases, 3 STATUS requests and 24 STATUS replies passed | [Wire log](logs/2026-11-10-status-wire-tests.txt) |
| ARM build | Passed | [Build log](logs/2026-11-10-status-build.txt) |
| Initial live transport | Matching reply in 59 ms; uptime field INCORRECT | [Initial raw capture](logs/2026-11-10-status-live.json) |
| Corrected real-Pico path S1-M09 | PASS: matching ID, 29 ms round trip, boot uptime 118530 ms, source age 50 ms, test mode true; five telemetry samples and one heartbeat continued | [Final capture](logs/2026-11-10-status-live-final.json) |
| Real website Request status button | PASS, user-reported after refreshing/reconnecting | User confirmed the note shows Pico STATUS reply confirmed |
| Current local assets | index/app/protocol/mqtt served matching files; JS syntax passed | HTTP/code checks |

The initial capture's transport-only PASS did not check semantic uptime.
It exposed `tk_get_tim` being used as boot uptime: this kernel returns TRON
calendar time from that API. Fixed S1's reported uptime to `tk_get_otm` and
added distinct calendar/operating clocks to the host fixture. Existing S3/S4
source-age differences retain their shared calendar-time domain. The final
capture verifies the corrected field. Both raw captures are preserved.

Current single UF2: `mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`, 707072 bytes.
SHA-256: `2338a09c0dba16401b2cadf6f8c50a323dc3b9d7c0f14c6f41ad779707d592ff`. User copied/reflashed it; no additional image variant
was created. Base pulled revision remains 89b582f plus the local working copy.
For formal submission, review/commit and capture the exact on-device revision.

Matched TEST=1 build before STATUS: text 351748, data 100, bss 82664.
After STATUS: text 353220, data 100, bss 82504; static RAM 82604
bytes, 160 less. The old 64/128-byte inbound topic/data staging is replaced by
five payload bytes and one packed control byte (length:3, five flags:1 each).
A 27-byte raw-snapshot cache is added; presence is derived from its existing
marker without an extra flag. S1's requested queue storage remains 192 bytes;
no new task/kernel object. These are static/requested sizes, not measured
whole-device heap/stack peaks. Round trip is one functional sample, not a
worst-case performance or AP/broker recovery acceptance result.

Remaining S1 priorities: agree controller-owned permitted actuator requests,
controller state/status, timed outage/recovery trials and final demonstration
evidence. Keep the deferred IMU detection/height-accuracy work separate.

## Recovery preparation - 10 November 2026

The next communication milestone is controlled loss/recovery evidence. A
baseline subscriber reached the custom broker but received no telemetry in
its eight-second window. The user confirmed the **car was unavailable** and
asked to prepare the tests for later. This is not a firmware-failure result.
No broker/hotspot interruption or flash was performed in this stage.

Code inspection found three concrete gaps, corrected in this working copy:

- WiFi rejoin previously armed only for negative states on a status change;
  DOWN (0) could remain idle. The existing five-second deadline now handles
  DOWN/failure and repeated unchanged failures, leaving positive JOIN/DHCP
  progress alone. Deadline comparisons include uint64 wrap/sentinel tests.
- A transient initial join-request error previously parked the radio task
  like a hardware setup failure. Successful interface setup now continues
  servicing/retrying association; a failed bus/interface still parks.
- Immediate MQTT connect/allocation/address errors could be retried every
  poll. They now use a deduplicated three-second backoff. WiFi/IP loss cancels
  timers/pending requests and explicitly disconnects stale TCP outside the
  radio lock. On restoration, the owner starts a fresh MQTT subscription.

The deployed SDK's explicit mqtt_disconnect closes/clears the client without
an automatic connection-status callback; the link handoff performs its own
idempotent cleanup. An offline late-accept callback is also closed. Repeated
MQTT sessions retain a single heartbeat chain. All driver/lwIP calls remain
on the existing radio/network owner. Binary telemetry/heartbeat/STATUS sizes,
controller behavior, sensor GPIO and IMU algorithms are unchanged.

The [direct-TCP recorder](recovery_probe.mjs) and
[physical procedure](RECOVERY_TESTS.md) prepare five broker and five separate
hotspot trials. The recorder needs fresh real telemetry/heartbeat and a
correlated TEST=1 STATUS baseline; it never stops a service or toggles WiFi.
It rejects retained/duplicate/backward telemetry, tracks uptime continuity,
saves partial/failed results and refuses to overwrite earlier evidence.
Restoration uses an operator marker before enabling service, so the saved
delay is a conservative end-to-end upper bound, not isolated Pico recovery.
The guide identifies that limitation and separates browser evidence.

| Development check | Result | Evidence |
| --- | --- | --- |
| Actual WiFi rejoin policy, synthetic clock/state | 13 passed | [Policy log](logs/2026-11-10-recovery-wifi-tests.txt) |
| Actual MQTT port, mock transport/timer registry | 30 passed, including twenty reconnects and backoff cancellation | [Port log](logs/2026-11-10-recovery-mqtt-tests.txt) |
| Recorder guards/result semantics/CLI evidence protection | 6 passed; CLI uses no live car/broker | [Recorder log](logs/2026-11-10-recovery-probe-tests.txt) |
| Actual S1 codec + queue/cache | 32 + 20 passed | [S1 log](logs/2026-11-10-recovery-s1-tests.txt) |
| Dashboard codec/store/MQTT client | 35 passed | [Dashboard log](logs/2026-11-10-recovery-dashboard-tests.txt) |
| Actual C to JS wire contract | 23997 telemetry, 3 heartbeat, 3 STATUS requests, 24 STATUS replies passed | [Wire log](logs/2026-11-10-recovery-wire-tests.txt) |
| Current ARM stationary build | Passed; no compiler warnings/errors | [Build log](logs/2026-11-10-recovery-build.txt) |
| New image flash / actual radio & broker recovery | **NOT RUN**, car unavailable | User explicitly deferred physical tests |

Current single UF2: `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`,
**707584 bytes**, SHA-256
`aaf0e39836b759904add31509ca4fef3a06a6892f6e58589b3cbf4342226fa77`.
Build flags remain `NET=lwip MQTT=1 TEST=1`. The earlier STATUS image/hash
above was last flashed by the user; this newer build still needs flashing.
No extra UF2 variant was created. Current base is d3cdd08 (the IMU/dashboard
commit after 89b582f), plus local STATUS/recovery work;
the [recovery fingerprint](logs/2026-11-10-recovery-fingerprint.json) records
source hashes, not credentials. Review/commit before formal submission.

Linked sizes: text 353580, data 104, bss 82508; static RAM **82612 bytes**,
eight more than the preceding STATUS build. WiFi unavailable is code 7 in
the existing three-bit receive-length field, not a new flag byte. Requested
S1 queue storage remains 192 bytes; peak heap/stack use is unmeasured.

STOP remains disabled. TEAM.md still leaves controller stop arbitration and
the supervisory interface unresolved; a one-time zero-duty write would be
overwritten by the controller's periodic updates. Keep actuator/controller
handoff as team integration work. The one-wheel/curb IMU case remains deferred.
Next physical step: flash this image and follow RECOVERY_TESTS.md. No hardware
timing, repeated recovery or sensor-accuracy pass is inferred from these tests.

## Live broker recovery - 10 November 2026

The user connected the car and reported flashing the current 707584-byte
recovery UF2 (SHA-256 aaf0e39836b759904add31509ca4fef3a06a6892f6e58589b3cbf4342226fa77).
Fresh telemetry and a matching STATUS with fresh sample and TEST=1 were
observed before interruption. No firmware was changed in this live session.

Five actual custom-broker interruptions were performed. Each stop verified
the process executable/configuration, matching car-facing/WebSocket listener
owners and exclusion of Windows services. A finally block restored the broker,
with a 55-second fallback if the recorder's marker was not delivered. Pico
power and hotspot remained on. The unrelated Windows broker service was
untouched. Afterward the custom broker (PID 7200 at verification) listened
again on 172.20.10.2:1883 and 127.0.0.1:9001, with the existing dashboard
server still on 127.0.0.1:4173. This broker is now a hidden background process;
its verbose output is in process-5.stderr.log below, not the old foreground
terminal. No restore-marker file remained.

| Actual interruption | Marker-to-first-fresh-telemetry upper bound | Uptime compatible | Maximum post-recovery telemetry interval | Observed post-recovery heartbeat interval |
| --- | ---: | --- | ---: | ---: |
| 1 | 4.415 s | Yes | 1.655 s | 4.846 s |
| 2 | 3.552 s | Yes, after corrected recorder assessment | 1.767 s | 5.908 s |
| 3 | 3.318 s | Yes | 0.543 s | 4.997 s |
| 4 | 5.641 s | Yes | 2.111 s | 6.393 s |
| 5 | 5.811 s | Yes | 1.919 s | 5.005 s |

All five actual interruptions returned telemetry, at least two heartbeats
and a matching fresh STATUS without an indicated Pico reboot. All five
met the earlier conservative 15-second broker-recovery design target.
This is **not isolated Pico recovery time**: the marker precedes broker
restoration, and the bound includes broker startup, observer subscription,
network scheduling and car recovery. Hotspot/AP loss was not exercised.

The separate strict interval targets are **not all passed**: four/five short
telemetry windows stayed within 2 s; two/five heartbeat windows within 5 s.
Retain the exact observations, including the 5.005-second heartbeat, without
rounding a miss into a pass. These short post-recovery windows do not establish
long-run delivery reliability. No cause of the jitter has been isolated yet.

### Recorder corrections and preservation of original evidence

The first two actual interruptions used the initial recorder. Its outbound
keepalive was incorrectly postponed by incoming telemetry; Mosquitto logs
showed observer-only keepalive disconnects before the first outage. The Pico
continued publishing. The recorder now schedules PINGREQ from its last sent
packet. The next three interruptions used this fix. Actual PINGREQs every
15 seconds appear in the process-4 broker log, without the old observer-only
45-second timeout. This changed the test tool, not firmware or the decoder.

Initial trial 2's uptime check reported false: baseline STATUS round trip was
2348 ms, but its old bound considered only the recovery reply's 57 ms latency.
Pico uptime advanced 47780 ms across 45595 ms between received replies. That
difference fits the baseline request/reply window. The corrected check brackets
both source sample instants: elapsed minus recovery RTT through elapsed plus
baseline RTT, with the existing 2-second clock tolerance and uint32 wrap.
It still rejects actual reset-like or implausible uptime changes. Eight
offline recorder checks, including this real regression case, passed.

The original JSON/result is preserved unchanged. A separate summary applies
the corrected assessor to **all five** actual interruptions; no failed
physical interruption was dropped. One additional prepared baseline was
interrupted to fix the recorder before any broker stop occurred; it is kept
as incomplete in the original file and explicitly listed as not interrupted
in the summary, not counted as a sixth physical trial.

- [Original capture: two actual interruptions plus the unexecuted baseline](logs/2026-11-10-broker-recovery-live.json).
- [Continued capture: remaining three actual interruptions](logs/2026-11-10-broker-recovery-live-continued.json).
- [All-five summary with original and reassessed results](logs/2026-11-10-broker-recovery-summary.json).
- [Eight recorder checks](logs/2026-11-10-recovery-probe-live-fixes-tests.txt).
- Broker verbose logs: `2026-11-10-broker-recovery-process-1.stderr.log`
  through `2026-11-10-broker-recovery-process-5.stderr.log` in this logs directory.
- [Live source/capture fingerprint](logs/2026-11-10-broker-recovery-fingerprint.json).

The user is ready for the separate hotspot test using their own local
terminal. That avoids needing this chat while their laptop's internet is
temporarily disconnected. Follow RECOVERY_TESTS.md; keep Pico power, the
broker and dashboard running. No new flash is needed for recorder-only fixes.

## Hotspot trials and bounded MQTT connection fix - 10 November 2026

The user completed the five-trial local recorder procedure and reported
finished. The capture is closed and its recorder source hash matches the
current recorder. Tested UF2 was the earlier aaf0e398... image, not the newer
replacement prepared below. All five saved results recompute exactly.

| Hotspot trial | First newly received telemetry after restore marker | Within-window STATUS / uptime continuity | Result |
| --- | ---: | --- | --- |
| 1 | 15.572 s | Matching fresh STATUS; compatible uptime | Functional return within 30 s; exceeds 15 s |
| 2 | 18.015 s | Matching fresh STATUS; compatible uptime | Functional return within 30 s; exceeds 15 s |
| 3 | None within window | No STATUS in window | Incomplete at 30 s |
| 4 | 20.484 s | Matching fresh STATUS; compatible uptime | Functional return within 30 s; exceeds 15 s |
| 5 | None within window | No STATUS in window | Incomplete at 30 s |

**0/5 met the earlier 15-second design target; 3/5 completed inside 30 s.**
This does not meet the earlier four-of-five design target. It is not a
professor-PDF numeric acceptance claim. The restoration marker is before the
operator enables the AP; no separate AP-on or laptop-association timestamp
was recorded. Windows WLAN event lookup found no matching connection records.
Do not assign the entire measured delay to Pico WiFi from this capture alone.

The still-running broker's log showed telemetry returning later for trials
3 and 5, roughly 32..33 s and 74..75 s after the markers (one-second broker
timestamp resolution). Those are later transport observations, not successful
within-window STATUS results. Original incomplete trials remain unchanged.
Current broker logs also show the car publishing again after the last trial.

Multiple heartbeats/telemetry packets arrived together on recovered links.
The tiny intervals in these bursts do not establish normal cadence. Sequence
progress and retain=false do not establish sample-generation time: v3 has no
source timestamp on the wire. Matching fresh STATUS verifies recent command
communication, while sustained periodic delivery still needs observation.
The firmware's pre-serialization age checks cannot undo time already spent
in a TCP/radio send buffer. This limitation is now explicit in the guide.

### Evidence and targeted source correction

Broker logs show the old Pico MQTT TCP session persisted through the first
outages with advertised keepalive k60. At epoch 1794301610 the broker expired
that old session for timeout; at 1794301614 it accepted a new Pico session.
This exposes an overly long dead-session fallback. The radio may still
report UP while packets cannot get through; the existing link-status cleanup
alone is insufficient to bound that case. Exact AP/driver behavior remains
to be confirmed with a timestamped serial trace.

Prepared two changes in the existing network-owned MQTT port:

- Advertise MQTT keepalive **5 s**, previously 60 s. The installed SDK checks
  liveness using a five-second cyclic timer; this is not a guaranteed
  five-second outage-detection time. The application heartbeat stays 5 s.
- Reuse the existing retry timer for a **5 s pending TCP/MQTT connection
  deadline**. A stalled attempt is explicitly closed before another attempt.
  Accepted sessions cancel the deadline; a late callback cannot close a
  healthy session. Link loss cancels it, and ordinary errors retain 3 s backoff.

No GPIO/IMU/control/schema changes, new task, queue or state flag. The tests
compile the actual MQTT port and cover pending/deadline retries, cancellation,
healthy late callbacks and the existing STATUS/rate/subscription paths.
All **36 port checks passed**; ARM stationary build passed without warnings.

Current single UF2 remains `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`,
707584 bytes, **SHA-256 9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0**.
It supersedes the tested aaf0e398... image at the same filename. It has **not
been flashed or hardware-tested**. Flags remain NET=lwip MQTT=1 TEST=1.
Sizes: text 353644, data 104, bss 82508; static RAM remains **82612 bytes**,
text grew 64 bytes. Runtime heap/stack peaks are still unmeasured.

- [Original completed hotspot capture](logs/2026-11-10T08-59-05-093Z-wifi-recovery.json).
- [Summary retaining every failed/incomplete trial](logs/2026-11-10-hotspot-recovery-summary.json).
- [Frozen relevant broker-event excerpt](logs/2026-11-10-hotspot-broker-events.txt).
- [36 actual-port checks](logs/2026-11-10-hotspot-fix-mqtt-tests.txt).
- [Replacement build](logs/2026-11-10-hotspot-fix-build.txt).
- [Replacement fingerprint](logs/2026-11-10-hotspot-fix-fingerprint.json).

Next: flash the replacement and verify baseline STATUS/telemetry first, then
repeat the hotspot series with Pico serial and laptop association timing if
available. Do not upgrade the failed trial result until new hardware evidence
supports it. Other controller/STOP and peak-RAM work remains open.

## Replacement normal-operation baseline - 10 November 2026

The user reported ready after the requested replacement flash. Local UF2
hash remains 9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0.
Broker logs show the new Pico connection advertising **k5** rather than k60.
No flash readback was performed; user report and observed runtime setting
support the image identity, without claiming complete binary attestation.

A 60-second real WebSocket capture using the dashboard's actual MQTT adapter,
codec/store and read-only STATUS path produced:

- **118 accepted telemetry messages**, maximum arrival interval **2007 ms**.
- **13 accepted heartbeats**, maximum arrival interval **6362 ms**.
- Three matching STATUS replies: **103, 207 and 1505 ms** round trips.
  Each reported stationary TEST mode and a fresh snapshot.
- No rejected packets; **one sequence gap**. The broker log has no Pico MQTT
  session transition within this window. The five-second configuration did
  not cause a reconnection loop during this observation.

Normal communication is established, but the strict earlier 2 s/5 s
interval targets remain open. **This is not a hotspot recovery pass.**
No firmware or decoder change was made in this baseline session.

- [Raw baseline observations](logs/2026-11-10-hotspot-fix-live-baseline.json).
- [Capture fingerprint and broker-runtime evidence](logs/2026-11-10-hotspot-fix-baseline-evidence.json).

An additional bounded diagnostic capture was started on COM5 at 115200 baud
for up to 20 minutes, writing `logs/2026-11-10-hotspot-retest-diagnostics.jsonl`.
It records only allowlisted Pico link/MQTT lines and changes in laptop WiFi
interface status/IPv4; no SSID/password or raw console dump is saved. Opening
the USB console did not reset the Pico, and no network change was performed.
Timestamps are serial-arrival/interface observations, not exact AP-beacon
availability. The file is live and must be closed before final fingerprinting.
The initial drain included `[TEST] subsystem1 SUMMARY 33 passed, 0 failed`.
This can be buffered startup output: it observes that summary, not a fresh
controlled normal-power-on trial or a timestamp of when the tests ran.

Next: user repeats the five local-terminal hotspot trials with the same
flashed image, keeping USB power and the running broker/dashboard unchanged.

## Follow-up hotspot review and confirmed-flash check - 10 November 2026

The user reported the retest finished. Three additional recorder files were
found; all original bytes are preserved:

| Capture start UTC | Saved trials | Observations |
| --- | ---: | --- |
| 09:35:42 | 3 of 5 requested | Two under-15-second returns (10.220, 9.877 s); trial 3 incomplete at 30 s |
| 09:39:53 | 0 | Startup-only file without a finished marker; no trial can be assessed |
| 09:47:05 | 5 | Four under-15-second returns (7.532, 6.668, 7.170, 9.096 s); trial 5 incomplete at 30 s |

The user confirmed restarting the **laptop** between the partial and complete
runs. The latest first STATUS uptime also indicates a new Pico boot before
that series. There are six passing and two timed-out recorded trials across
these changed conditions. Keep the earlier partial failure, rather than
presenting only the better run as uniform reliability evidence. The latest
complete series alone meets the earlier four-of-five count criterion, with
one unresolved timeout. That count is not full-project acceptance.

### Firmware-identity clarification supersedes earlier attribution

After reviewing the silent post-test condition below, the user clarified
that the intended UF2 had not been flashed and explicitly reported flashing
it then. Earlier reports had assumed it was flashed based on the readiness
reply; broker k5 confirmed a runtime setting, not an exact binary identity.
The raw recorder hashes identify **local files**, not flash readback.

Therefore the preceding follow-up captures have **unconfirmed running-image
identity** and cannot establish recovery acceptance for the intended corrected
image. Their observed timings remain unchanged. A clean series after the
newly confirmed flash remains required. No firmware source change, rebuild
or new image was made during this review; current local hash is still
9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0.

### Post-timeout fault observation and subsequent restoration

A 12-second read-only WebSocket check reached/subscribed to the running
bench broker but received zero telemetry, zero heartbeats and no STATUS.
The user confirmed the Pico was still powered from laptop USB, hotspot on,
laptop connected. Windows reported COM5 present/OK with device error code 0,
but opening it at 115200 failed: `A device attached to the system is not functioning.`
This records a powered, noncommunicating state, not proof of a particular
firmware crash, driver fault or WiFi cause. SDK MQTT disconnect cleanup was
reviewed; it cancels its cyclic timer and pending requests. No new cleanup
defect was established and no speculative patch was applied.

The user then clarified the missing flash and reported flashing the current
file. After that flash/restart:

- A new 20-second WebSocket check received **37 telemetry messages and three
  heartbeats**, plus matching fresh TEST=1 STATUS in **81 ms**.
- USB COM5 opened successfully. Allowed console output included WiFi/MQTT-up
  messages and **subsystem1 SUMMARY 33 passed, 0 failed**. Initial output may
  be buffered startup text; it is not a new timed cold-boot test.
- Communication is restored, but this cannot distinguish the effect of the
  firmware from the reset performed by flashing. No hotspot trial has yet
  been observed after this newly confirmed flash.

The extra diagnostic helper was terminated by the laptop restart and its
original tool session no longer exists. Its JSONL lacks an end marker.
Last serial output was at 09:30:55 UTC, before any retest outage; laptop
interface transitions extend to 09:41:32 UTC. It provides no Pico serial
trace during the outages and does not cover the latest complete series.
Do not treat it as the planned full 20-minute trace or an active recorder.

Evidence:

- [Partial run](logs/2026-11-10T09-35-42-751Z-wifi-recovery.json),
  [startup-only file](logs/2026-11-10T09-39-53-541Z-wifi-recovery.json),
  [complete five-trial run](logs/2026-11-10T09-47-05-672Z-wifi-recovery.json).
- [Follow-up summary with identity caveat](logs/2026-11-10-hotspot-corrected-retest-summary.json).
- [Powered-silent observation](logs/2026-11-10-hotspot-retest-fault-observation.json)
  and [read-only silent check](logs/2026-11-10-hotspot-retest-current-check.json).
- [Post-confirmed-flash normal communication](logs/2026-11-10-hotspot-post-confirmed-flash-check.json)
  and [USB console observation](logs/2026-11-10-post-confirmed-flash-serial.jsonl).
- [Raw-capture fingerprint](logs/2026-11-10-hotspot-followup-fingerprint.json).

Next: one clean five-trial hotspot series after the explicit flash, without
restarting the laptop/Pico/broker within that series. No further reflash or
code change is needed just to start the measurement. Keep the unresolved
timeout, exact-image caveat and strict interval/peak-RAM/controller work open.
