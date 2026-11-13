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

Development verification: all 15 local checks passed on the host with the
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

## Confirmed-flash hotspot recovery - 11 November 2026

The user completed the requested clean five-trial series after the explicit
flash confirmation. The capture is normally closed, contains all five trials
with no timeout/abort reason, and each stored assessment recomputes exactly
using the unchanged recorder. Local UF2 hash matches the current file:
9c5ac8f0876055939f2cc4049a22e3ca9285b38ccccd889096bd801dafc2a7a0.
The captured recorder hash also matches current source. Image identity rests
on the explicit user flash confirmation plus local fingerprints, not flash
readback. Earlier failed/unconfirmed-image captures remain preserved.

| Trial | Restore-marker to first new telemetry receipt | Matching fresh STATUS / compatible uptime | Maximum telemetry arrival interval | Observed heartbeat interval |
| --- | ---: | --- | ---: | ---: |
| 1 | 8.511 s | Yes | 2.080 s | 4.997 s |
| 2 | 7.922 s | Yes | 0.539 s | 4.987 s |
| 3 | 8.913 s | Yes | 0.546 s | 5.020 s |
| 4 | 9.242 s | Yes | 0.599 s | 5.086 s |
| 5 | 8.688 s | Yes | 0.592 s | 4.992 s |

**Recovery result: 5/5 within 15 seconds**, meeting the earlier design/test
criterion of at least four of five. Each trial received at least five
telemetry messages, two heartbeats and a matching fresh TEST=1 STATUS reply.
All five individual uptime checks passed. Across the complete series, Pico
uptime advanced **245660 ms**, versus **245691 ms** between first/last received
STATUS observations, compatible with uninterrupted operation and reply latency.
No reboot was indicated during this series.

One attempted baseline between trials 3 and 4 was rejected by the fresh-data
gate before a recorded trial started; a later valid baseline began trial 4.
It is retained in events and is not an omitted completed/failed trial.
The previous timeout records and their image-identity caveats are unchanged;
they are not pooled into this newly confirmed series.

The recovery bounds begin at an operator marker before AP restoration and
include human delay, laptop association, observation and Pico recovery.
They do not measure isolated AP-beacon-to-Pico restoration time. The first
new telemetry receipt is not independently source-timestamped; correlated
fresh STATUS establishes recent communication. This is a stationary bench
result, not full-project acceptance or proof of long-run reliability.

Separate earlier interval targets remain **OPEN**. Four/five short telemetry
windows met 2 s (maximum 2.080 s); three/five heartbeat windows met 5 s
(maximum 5.086 s). Do not round these misses into passes or count recovery
success as delivery-cadence validation. Peak runtime RAM, controller-owned
commands/status, sensor accuracy and final integrated demonstration remain
separate work. STOP remains disabled.

Source commit at review: **b11c682808c3e82caae907fba0d1ef5c3c69311c**.
Tracked source was clean at review start; no firmware or recorder source
change, rebuild, reflash or additional outage was performed during review.

- [Original clean series](logs/2026-11-11T06-09-18-051Z-wifi-recovery.json).
- [Verified five-trial summary](logs/2026-11-11-hotspot-confirmed-recovery-summary.json).
- [Capture/source fingerprint](logs/2026-11-11-hotspot-recovery-fingerprint.json).

This completes the requested clean hotspot-recovery milestone. Next S1
priority is the agreed controller-owned command/status handoff, alongside
the remaining interval, RAM and final demonstration evidence. No additional
hotspot series or reflash is requested just to repeat this completed result.


## Controller command/status integration — 11 November 2026

User explicitly requested proceeding with controller-owned command/status
integration. The shared controller and S1/network/browser were updated; S2,
S3, S4 and S5 source folders and the IMU algorithm were not changed. This is
a local development implementation, not team/instructor approval or a physical
motor-stop pass. No live STOP, flash, outage or motor movement was performed
in this stage. Browser access host was unavailable, so no visual QA/screenshot
was captured.

The controller now owns a persistent stop inhibit, a protected state
publication and bounded request/result slots. It runs every 20 ms, processes
STOP before normal steering and throughout startup, and repeatedly submits
zero duty to the existing S2 API while latched. S2 alone writes PWM. Contention
never blocks; a normal controller poll conflict requests zero for that loop.
A successful normal reply requires S2 acceptance, not merely queue submission.
Motor handoff errors preserve the inhibit and report fault; even an already
latched fault cannot be reported as a successful stop. Only Pico restart clears
the latch. There is no remote RESUME.

In the stationary TEST=1 image the supervisor now runs, but the motor task
remains disabled and the supervisor never calls S2. State is Control disabled,
plus a real software latch if requested. A bench success cannot establish
physical motor stopping. The encoder calibration configuration is excluded
from command admission. Normal driving task code was compile-checked to
temporary objects only; the single UF2 remains TEST=1.

Sensor v3 stays 11 bytes and heartbeat five bytes. STATUS request remains
five bytes; its current reply is 15/26 bytes (old 11/22 removed). Existing
status topic also carries six-byte controller beacons and ten-byte STOP
results. STOP request is 13 bytes: correlation ID/session/boot deadline.
The explicit-click browser handshake uses current STATUS, a two-second
lease, one pending command, three-second reply timeout and no automatic retry.
Session changes cancel browser requests; controller-applied latch persists.
The stock receive callback has no retain flag; session/deadline checks reject
old commands, but are not authentication on the anonymous trusted bench broker.

All new controller deadlines/publications use operating time (`tk_get_otm`),
not existing sensor calendar-domain timestamps or lwIP's retry clock. Controller
snapshot expiry is 500 ms in firmware, two seconds at browser receipt. CSV
controller columns explicitly describe state at sensor receipt, not a
synchronized sensor sample. The recovery recorder ignores non-STATUS markers
on the shared status topic and remains read-only. Earlier captures/source
fingerprints and recovery results are unchanged.

| Check | Result | Evidence |
| --- | --- | --- |
| Actual controller source, TEST=0 and TEST=1 | 16 checks each, zero failures | [Owner tests](logs/2026-11-11-controller-owner-tests.txt) |
| Actual MQTT port: fragmentation, admission, replay/session/expiry, failures/recovery | 50 checks, zero failures | [Port tests](logs/2026-11-11-controller-mqtt-tests.txt) |
| Actual S1 codec and queue/cache bridge | 36 + 23 checks, zero failures | [S1 tests](logs/2026-11-11-controller-s1-tests.txt) |
| Dashboard current schema, command lifecycle and existing MQTT/telemetry | 45 tests, zero failures | [Dashboard tests](logs/2026-11-11-controller-dashboard-tests.txt) |
| Actual C → JavaScript contract/framing | 19914 distance/IR + 4083 IMU frames, 3 heartbeats, 3 STATUS requests, 24 STATUS replies, 147 controller/STOP vectors | [Wire tests](logs/2026-11-11-controller-wire-tests.txt) |
| Existing recovery recorder | 8 tests, zero failures | [Recorder tests](logs/2026-11-11-controller-recovery-probe-tests.txt) |
| Existing S2 line policy (unchanged) | Passed | [Line tests](logs/2026-11-11-controller-line-policy-tests.txt) |
| Actual normal app_main/controller branch | ARM compilation passed; temporary objects only | [Normal compile](logs/2026-11-11-controller-normal-compile.txt) |
| ARM stationary image | Build passed | [Build log](logs/2026-11-11-controller-build.txt) |
| Real Pico/controller/motors | NOT TESTED in this stage | Stationary checklist in the guide; motor trial remains separate |

Expected on-device S1 summary is now **37 passed, 0 failed**: 36 software
checks plus the Pico clock check. This expectation is not a captured board
pass. Existing earlier 33-check startup evidence belongs to the prior image.
The wire matrix omits two fault-state success combinations deliberately:
a fault may carry a latch but must not encode a successful STOP.

Current UF2: `build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2`,
**714752 bytes**, SHA-256
`eb29f4ab661a6810fbf43095cbf9052e4fac4100870de6cb158787e8bf9f412b`.
Base commit **b11c682808c3e82caae907fba0d1ef5c3c69311c**, with local changes;
no commit was created. [Fingerprint](logs/2026-11-11-controller-fingerprint.json)
records source hashes without WiFi credentials. Only one UF2 is kept. **This
image has not yet been flashed.**

Matched TEST=1 linked sizes: text **357108**, data **104**, bss **82576**;
static RAM **82680 bytes**, +68 versus previous recovery image (82612).
Text grew 3464 bytes. S1 queue-buffer requests remain **192 bytes**. Added
controller record bytes and the RX slot are compact application state;
no new S1 message queue or lwIP timer was created. The stationary profile
now also allocates a controller task with a **2048-byte stack plus kernel
bookkeeping**, and one semaphore. Normal driving already had the task.
Static size is not a peak RAM measurement or an accounting of dynamic cost.
This is a feature addition, not a like-for-like savings comparison.

[Controller guide](CONTROLLER_COMMANDS.md) gives the exact bit/range tables,
resource costs and physical steps. First flash/verify the stationary image
and test software latch/persistence/restart behavior; then prepare normal
controller/S2 motor-inhibit validation with the owners. Earlier 5/5 recovery
results, strict interval misses, runtime peak RAM and final demonstration
remain separately scoped. The IMU one-wheel/curb algorithm is still deferred.


## User-reported stationary controller checks — 11 November 2026

The user reports all five tests from the immediately preceding instructions
produced the expected result: normal restart shows Control disabled with no
latch; Request status confirms a matching reply; Latch stop confirms the
software inhibit with motor control disabled; dashboard reconnect preserves
the latch; a second normal Pico restart clears it. Record these as **five
user-observed functional passes**.

See [the bench observation](logs/2026-11-11-controller-user-bench-observation.md).
The intended local UF2 remains 714752 bytes with SHA-256
`eb29f4ab661a6810fbf43095cbf9052e4fac4100870de6cb158787e8bf9f412b`.
No flash readback, independent packet/serial capture or timing was obtained
in this confirmation. Do not infer actual motor stopping, new-image outage
recovery/cadence or the 37-check board summary from these five passes.

This supersedes the earlier controller development section's then-unflashed
status, while preserving the original source/build fingerprint. The agent
made documentation changes only when recording the report; no firmware
change, rebuild, flash, live command or outage occurred. Next is normal
controller/S2 motor-inhibit validation, followed by final integration and
cadence/resource/submission evidence. Buddy 4's IMU algorithm remains deferred.


## Normal motor-stop trial preparation — 11 November 2026

After the stationary five-check pass, the user requested the next milestone
and confirmed **car available, both wheels raised securely, battery off**.
The actual normal controller loop, controller latch, S2 API and S2 motor loop
were exercised together with deterministic RTOS/PWM stubs. Eight integration
checks passed: startup delay, expired request, contention fallback, no early
success on a busy S2 handoff, accepted zero output, persistent inhibit despite
changing/stale IR, STOP during startup, and inhibition beyond startup.
[Actual-loop log](logs/2026-11-11-motor-stop-path-tests.txt). Existing S2 policy
and motor API tests also pass unchanged; [log](logs/2026-11-11-motor-stop-s2-tests.txt).
These are offline checks, not physical wheel stopping evidence.

A forced ARM rebuild with `NET=lwip MQTT=1 TEST=0` passed without warnings:
[build](logs/2026-11-11-motor-stop-build.txt). The source, motor duties (20%
forward/15% inner), packet format, dashboard decoder and S2–S5 files are
unchanged. Normal steering can start 3000 ms after boot, before WiFi is ready.
No on-device self-test summary runs in normal mode. The current image is
`build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2` (no `_test`), 694784 bytes,
SHA-256 `fa871dff33210a77bf3166baed8f29e4dcd65342868e3a1c2a3103ae5752bf55`.
It is **not yet flashed or physically tested**. The superseded stationary
UF2 was removed after both hashes were checked, leaving only one current UF2;
old logs/fingerprints are preserved. No physical command or motor power was
performed by the agent during preparation.

Normal linked sizes: text 347276, data 104, bss 82592, static RAM **82696**.
S1 queue requests remain 192 bytes. Normal versus stationary linked sections
are different build profiles, not a like-for-like saving. Normal additionally
runs S2 motor and barcode reporter tasks with a 2048-byte stack each plus
kernel overhead; runtime peaks are not measured.

`observe_controller.mjs` is a bounded local WebSocket recorder using the
existing dashboard MQTT adapter and binary decoder. It sends only read-only
STATUS, records controller results from other clients and does not claim
physical wheel or recovery timing measurements. Syntax check passed; its live
use follows the flash. The existing recovery trial recorder intentionally
requires stationary TEST=1 and is not used to command normal motors.

[Motor-stop procedure](MOTOR_STOP_TEST.md) and
[normal build fingerprint](logs/2026-11-11-motor-stop-fingerprint.json)
identify the intended image and exact physical steps. First verify Normal
build with battery off. Then observe both wheels rotating on both-black;
keep the same target while requesting STOP; verify actual stopping and
persistence through changing line input/dashboard reconnect. Physical results
will be recorded separately, without overwriting this preparation record.


## Normal flash / USB reconnect follow-up — 11 November 2026

The user reported flashing the normal UF2 and reconnecting with wheels raised
and battery still off. Initial preflight and normal-flash WebSocket observers
connected/subscribed to the broker but received no car packets. Windows found
neither Pico USB nor BOOTSEL/serial at that moment. The laptop WiFi address
and car-facing broker listener were still 172.20.10.2. No powered motor trial
was started or counted from those silent windows.

After the user confirmed a normal USB reconnect (without BOOTSEL), COM5 / Pico
USB and the car TCP session returned. The 20-second live observer received a
matching fresh STATUS in **24 ms**, TEST=false, Waiting for line, stop latch
false. Real telemetry/heartbeat/controller packets continued. This verifies
normal-mode communication and a read-only reply, not physical wheel stopping
or independent flash identity. No firmware change, agent flash or motor
command was needed for this communication restoration.

[Live baseline summary](logs/2026-11-11-motor-stop-normal-baseline-summary.json)
links/hashes the completed raw capture and intended local UF2. The earlier
silent captures remain preserved. The user is now asked to power the raised
wheels, observe both rotating on both-black, keep that target unchanged and
request Latch stop through the website. A bounded read-only capture is running;
physical stopping/persistence must be reported separately. No powered motor
pass has been recorded yet.


## Motor trial deferred for teammate wiring/code — 11 November 2026

The user reports changed wiring by the teammate responsible for the motor
and other subsystem integration, and explicitly requests waiting for that
teammate's code before continuing. Record the physical motor-stop trial as
**DEFERRED / NOT COMPLETED**. Do not turn the hardware mismatch report into a
physical STOP pass or a conclusive S1 protocol failure. No new pin map or
motor interface was supplied; no speculative driver/pin changes were made.

The prior five stationary checks remain user-reported passes. Normal-mode
communication and the 24 ms matched read-only STATUS after USB reconnect
remain recorded. The bounded 180-second capture is finished and preserved;
it cannot establish wheel motion/physical stopping. The agent made only
local documentation updates for this deferral, with no flash, motor command,
GPIO change, additional live probe or Git pull/merge.

[Deferral record](logs/2026-11-11-motor-stop-deferred-wiring.md) hashes the
finished raw capture and identifies the existing normal UF2, which still
reflects the preceding software/pin assignments. The original build/source
fingerprint is preserved as historical preparation evidence. Resume by
preserving local S1 changes, integrating the teammate's actual wiring/code,
checking GPIO ownership/controller/motor contracts and rebuilding one current
UF2. Verify communication with motor power off before repeating the raised-
wheel motor-stop trial. Do not modify teammate-owned S2 files based on guesses.


## Safe teammate pull — 12 November 2026

The user's earlier VS Code pull fetched f37055f but aborted because local
TEAM.md and app_main.c changes would be overwritten. Main was still b11c682.
A verified file backup preserved 59 modified/untracked files plus binary
working-tree/staging patches. A named Git stash also preserved the local work.
After fetching, a fast-forward pull moved main to f37055f; local S1 changes
were reapplied. The one content conflict in TEAM.md was resolved by keeping
the local S1 row and upstream's actual S2 motor/bench row. app_main.c merged
automatically, preserving both sides. All 57 non-overlapping saved files were
verified byte-for-byte; 17 Git-normalized files were restored to original
bytes so historical capture hashes remain valid. The original unstaged state
was restored, with no unmerged index entries. No feature commit or push occurred.
The recovery stash/file backup are retained.

Dashboard: **45 tests passed**; actual S1 codec **36 passed**, queue/cache
**23 passed**. [Dashboard log](logs/2026-11-12-post-pull-dashboard-tests.txt),
[S1 log](logs/2026-11-12-post-pull-s1-tests.txt),
[pull/recovery record](logs/2026-11-12-pull-record.json).
Main matches origin/main (0 ahead / 0 behind). Upstream-owned S2/S3/S5 and
new GPIO/encoder/motion files match HEAD; no speculative changes were made.

The new normal usermain invokes noreturn ir_adc_test() before the full
subsystem lifecycle. Analog IR uses GP28/27/26 and centre/right analog
tracking; encoders use GP16/17 and GP0/1. These source assignments must be
reviewed against actual hardware, and the active standalone startup needs
S1/controller integration before dashboard/motor acceptance resumes. Existing
S1 sensor formats/algorithms were not expanded as part of this Git operation.
The old motor harness/evidence belongs to its prior S2 API/source. No ARM
build, UF2 replacement, flash, live command or motor-power operation occurred.
The existing local UF2 predates this pull and must not be mistaken for the
new teammate firmware. Earlier passes and raw records remain historical.

## Analog-wiring / full S1 integration — 12 November 2026

User request: check the pulled wiring changes, continue S1 integration and
fix affected earlier work. Base HEAD remains f37055f, with local restored S1
changes. No commit/push, live MQTT command, flash or motor-power action was
performed in this development stage.

Three concrete conflicts were fixed: the old GP16 line path overlapped the
left encoder, digital barcode capture on GP26 would conflict with the new
right AO input, and noreturn standalone IR startup bypassed the full S1/
controller lifecycle. S3 now owns GP28/27/26 ADC2/1/0 via the existing kernel
ADC device. Thresholds (1390 reserved barcode, 950 centre/left, 1420 right),
discard-one/two-sample averaging and raw 0..4095 bounds are retained from
the teammate. Failed/short/out-of-range or >100 ms sampling is invalid;
timestamp marks the oldest conversion. Snapshot readers never call drivers.

Default APP_MODE=integrated restores all subsystem lifecycles. Explicit
APP_MODE=ir preserves the standalone IR diagnostic. TEST=1 APP_MODE=encoder
preserves the teammate's timed powered encoder bench. S2 calibration is now
selected only by that explicit mode; ordinary integrated TEST=1 contains no
motor task. The build profile includes APP_MODE to avoid stale object reuse.

The normal controller preserves both-white/both-black forward and individual
corrections at 35/35 or 26/35, 35/26 RPM. It uses existing S2 APIs and leaves
PID gains, encoder calibration, polarity, PWM and fault algorithms intact.
Fresh ADC/motion validity gates steering; a known S2 fault requests zero and
is reported as a controller/motor fault. STOP always takes priority. A known
motor fault cannot become a successful STOP merely because a zero request
was accepted. The new regression covers that case.

S5 retains GP4/5 and the upstream per-pin IRQ13 dispatcher; no change to
distance conversion, startup tick fix or radio retry policy was made. S4
wiring and estimator are unchanged. It still uses nominal 150 mm/s until
measured speed is explicitly integrated; one-wheel/curb accuracy is deferred.
The GP28 AO input is barcode-reserved but not decoded; no digital capture/
timer or barcode-result queue is started. Future barcode acquisition needs
its owner rather than interpreting the new right sensor as barcode.

Sensor telemetry stays 11 bytes and heartbeat five; controller/status/STOP
contracts are unchanged. The website now labels actual GP27/26/28 roles and
describes white/white forward. S2 motion feedback is used for controller
validity/faults, not yet transmitted as wheel speed/distance in v3. Missing
panels remain unavailable. Browser access host was unavailable, so visual
inspection/screenshot was not obtained; software tests and syntax checks
are separate evidence.

| Validation | Result | Evidence |
| --- | --- | --- |
| Actual S3 ADC/snapshot/task and existing decoder models | 16 suite groups passed; AO harness in both controller configurations | [Analog tests](logs/2026-11-12-wiring-analog-tests.txt) |
| Actual controller + S2 API/PID/motor loop, no hardware | 13 checks passed | [Motor tests](logs/2026-11-12-wiring-motor-tests.txt) |
| AO policy in RPM and duty modes | Passed | [Policy tests](logs/2026-11-12-wiring-policy-tests.txt) |
| Actual shared IRQ dispatcher / echo masking / encoder continuity | Passed | [GPIO tests](logs/2026-11-12-wiring-gpio-tests.txt) |
| Controller owner | 17 checks in each TEST mode, zero failures | [Owner tests](logs/2026-11-12-wiring-owner-tests.txt) |
| Actual S1 codec and queue/cache bridge | 36 + 23 passed | [S1 tests](logs/2026-11-12-wiring-s1-tests.txt) |
| Actual MQTT port | 50 passed | [Port tests](logs/2026-11-12-wiring-mqtt-tests.txt) |
| Dashboard | 45 passed | [Dashboard tests](logs/2026-11-12-wiring-dashboard-tests.txt) |
| C → JavaScript packed contract | 23997 sensor frames, 147 controller vectors, 3 heartbeats, 3 requests / 24 STATUS replies | [Wire tests](logs/2026-11-12-wiring-wire-tests.txt) |
| Actual ARM compile/startup symbols, four modes | Passed; benches exclusive, default stationary has no motor task | [Mode checks](logs/2026-11-12-wiring-profile-tests.txt) |
| Final normal ARM link, WSL temporary output only | Passed: text 350712, data 104, bss 82652 | [Normal build](logs/2026-11-12-wiring-final-normal-build.txt) |
| Final stationary ARM link, current UF2 | Passed: text 357820, data 104, bss 82492 | [Stationary build](logs/2026-11-12-wiring-stationary-build.txt) |
| Actual current stationary ELF task symbols | S1/S3/controller present; motor, encoder bench, standalone IR, digital capture absent | [Image check](logs/2026-11-12-wiring-image-check.txt) |
| Rewired car / physical STOP | NOT TESTED | Stationary validation first; then separate raised-wheel normal test |

The current sole UF2 is
build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2,
716288 bytes, SHA-256 2cb408f2b453226ad9a55c005b59bad1e7171b0ccc4204b69a246fd129f2eccb.
Build NET=lwip MQTT=1 TEST=1 APP_MODE=integrated.
It has not been flashed or physically tested. Expected S1 board summary
remains 37 checks; this is not an observed board pass.

Current stationary static RAM is **82596 bytes**; checked normal mode uses
82756. Different modes/features are not like-for-like savings comparisons.
S1 input/output queue storage remains 64+128 =192 bytes. S3 sharing is nine
bytes: timestamp:32, raw ADCs 3×12, validity:1, reserved:3; pattern derives
from thresholds. No extra pattern/validity booleans or sample queue are
needed; old barcode-result queue allocation is removed. Driver-required UW
conversion buffers are transient local API working values. Runtime heap/
stack peaks remain unmeasured; other subsystem/native state conversion is
still group-wide work.

[Fingerprint](logs/2026-11-12-wiring-fingerprint.json) identifies source,
logs, current UF2 and validation limits without private WiFi credentials.
Current-mode checks may be reproduced in WSL with
python3 testing/subsystem1/test_build_modes.py, using the retained build-log
flags; pass a newer log as its first argument when rebuilding later.

The provided image is stationary first because new PID motion requests
with motor power off may latch a stall fault. After actual IR/telemetry/
latch validation, rebuild a matching normal integrated image and use the
documented raised-wheel power/restart procedure. Prior digital-IR, motor
and recovery observations remain preserved for their original images, not
silently transferred into a physical pass for this new wiring.

### Hardware availability follow-up - 12 November 2026

The user answered "Car unavailable - test later" to the stationary-image
flash/setup request. No flash, live capture, powered test or hardware pass
is recorded for this new image. Keep the current stationary UF2 and resume
Phase 1 of [the wiring/motor procedure](MOTOR_STOP_TEST.md) when the car is
available. Normal driving firmware and the raised-wheel STOP trial follow
only after the new sensor mapping is physically checked.

## Rewired stationary live baseline - 13 November 2026

The user reports connecting the car and flashing the latest UF2. Firmware,
build and dashboard source hashes still match the prepared 12 November
image (716288 bytes, SHA-256 2cb408f2b453226ad9a55c005b59bad1e7171b0ccc4204b69a246fd129f2eccb).
This identifies the intended local artifact, not a flash readback.

A read-only 40-second WebSocket/MQTT capture received 77 eleven-byte telemetry
frames, eight five-byte heartbeats, 38 six-byte controller beacons and one
26-byte matching STATUS reply (31 ms). All 77 telemetry frames supplied valid
ultrasonic distance (226..554 mm) and pitch; there were no decode errors.
Controller state was Control disabled, TEST true, STOP latch already true.
Observed line_bits were 4 and 6; receipt alone does not validate the physical
sensor mapping. STATUS rechecked its older sensor snapshot and correctly
expired IR/pitch independently. No motor command was sent by the recorder.

The four actual black/background combinations and latch reconnect/restart
checks remain pending. Physical motion, encoder feedback, height/distance
accuracy, board check summary and final-image recovery are not established.

[Raw capture](logs/2026-11-13-wiring-stationary-baseline.json),
[summary and capture fingerprint](logs/2026-11-13-wiring-stationary-summary.json).

### Power clarification and IR observation - 13 November 2026

The user clarified that the whole car is powered by either laptop USB or a
power bank; there is no separate battery/motor switch. The earlier request
to switch a motor battery off assumed the wrong setup. Current dashboard,
motor, recovery and progress instructions now keep USB connected for
stationary tests and require raised wheels before normal firmware is powered
or flashed. Unplugging the active USB supply turns the whole car off.
No rewiring, voltage change or sensor/actuator code change was made.

With laptop USB connected, the user explicitly reports all four physical
GP27/GP26 black/background combinations match line_bits 4, 6, 5, 7. This is
user-observed mapping/threshold behavior, not a calibrated accuracy trial.
The prior power-clarification capture includes a controller session change
from latched to unlatched; without a correlated operator action it is not
counted as the requested reconnect/restart test. Latch confirmation and
normal powered motor STOP remain pending.

### Stationary latch follow-up - 13 November 2026

The user reports the latch persists when reconnecting only the dashboard,
and disappears after unplugging USB and reconnecting normally without
BOOTSEL. Both are the expected behavior: restarting the Pico clears its
controller inhibit; browser reconnection does not. Together with the four
IR combinations and captured live sensor/STATUS baseline, the stationary
functional checks pass. These user observations do not establish physical
motor stopping, calibrated sensor accuracy or repeated timing acceptance.
Preparing the matching normal integrated image is the next step; both drive
wheels must be raised before that firmware is flashed or powered.

### Matched normal motor image prepared - 13 November 2026

After the stationary checks passed, a forced normal build with NET=lwip
MQTT=1 TEST=0 APP_MODE=integrated passed without compiler/linker warnings.
Firmware sources still match the 12 November software-tested AO integration;
this step changes the compile-time mode, not pins, thresholds, PID gains,
IMU algorithms or the wire schema. ELF symbols confirm motor/PID, analog S3,
S1 and controller presence, with standalone benches/digital capture absent.

Current sole UF2: build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt.uf2,
701952 bytes, SHA-256 1a2687d1c3b642626fb8c81b355587f8327e6d32a82e08e6829ae0d02dd1fc08.
The superseded stationary UF2 was removed only after both hashes were
verified; its raw observations and original fingerprint remain preserved.
Normal sections: text 350712, data 104, bss 82652; linked static RAM 82756
bytes. S1 queues remain 192 bytes; runtime heap/stack peaks are unmeasured.
Normal mode runs no on-board TEST suite.

The normal image is not yet flashed or physically validated. The car has a
single USB/power-bank supply: both drive wheels must be securely raised
before flashing/connecting it. Normal motion may start after three seconds,
before WiFi or the dashboard connects. The motor STOP trial must observe
rotation before the explicit website click, then both wheels stopping;
nonmoving or already-faulted wheels are not a pass.

[Build output](logs/2026-11-13-normal-motor-build.txt),
[normal source/artifact fingerprint](logs/2026-11-13-normal-motor-fingerprint.json),
[physical procedure](MOTOR_STOP_TEST.md).

### Normal flash and live pre-STOP baseline - 13 November 2026

The user explicitly confirms both drive wheels raised and the current normal
UF2 flashed/reconnected. A read-only 45-second live capture received 80
eleven-byte telemetry frames, eight five-byte heartbeats, 41 six-byte controller
beacons and one 26-byte matching STATUS reply (203 ms), without decode errors.
The Pico reported TEST=false, Following line, stop_latched=false. All four
line codes appeared. This confirms normal communication/controller state;
physical wheel movement and actual STOP still require the user observation.

[Raw baseline](logs/2026-11-13-normal-motor-baseline.json),
[normal observation summary](logs/2026-11-13-normal-motor-observation-summary.json).
The recorder sent only read-only STATUS; the explicit motor STOP will be
clicked by the user on the website after observing both wheels rotating.

A subsequent 180-second recording received normal-mode telemetry/controller
messages but captured no STOP result. It is preserved as a pre-command
observation window, not counted as a physical STOP pass or failure. The
user's before/after wheel observation and post-latch persistence remain
pending. [Recording](logs/2026-11-13-normal-motor-stop-trial.json).

### User motor STOP observation and restart clarification - 13 November

The user reports both wheels rotating before the website STOP click, both
stopping afterward, and Stop latched confirmed on the dashboard. The
recording window above had ended before this result; no physical stopping
time or independently captured STOP result is claimed.

A subsequent fresh matching STATUS (68 ms) reported Following line,
unlatched, with uptime 5750 ms. Earlier matching normal uptimes were 57330 ms
and later 6970 ms, with different sessions. Restarts occurred between these
windows. The user was asked whether USB was deliberately unplugged/reconnected
after STOP. Reboot conditions and continuous-power latch persistence must
be clarified; do not treat the currently clear latch as either a persistence
pass or a proven latch-loss defect without that information.

The user subsequently confirmed unplugging/reconnecting USB after STOP;
the latch disappeared and the wheels resumed turning. This resolves the
restart condition as a deliberate power-cycle and matches the specified
restart-clears-latch behavior. It is not evidence of latch loss while
continuously powered. A final explicit check now keeps USB connected through
STOP, all four IR changes and dashboard reconnect, then turns the car off
by unplugging USB at the end. No firmware change is needed for this behavior.

### Normal STOP and continuous-power persistence PASS - 13 November 2026

The final explicit procedure kept USB connected throughout STOP, all four
IR patterns and dashboard disconnect/reconnect. The user confirms both wheels
were rotating, both stopped after the website STOP, and both stayed stopped
through the entire sequence with Stop latched displayed.

The independent read-only recorder captured a successful normal STOP result
(request ID 2378069613, session 3056588280, TEST=false, Stop latched). After
that result it received 535 telemetry frames covering every valid IR code
(4:128, 5:179, 6:79, 7:149), 272 controller beacons all Stop latched/true in
the same normal session, and 55 heartbeats. Telemetry sequences and heartbeat
counters advanced; there were no decode errors or indicated reboot in this
window. The website sent STOP; the recorder sent only read-only STATUS.

This completes a functional normal-build motor STOP/persistence bench check
for the pulled analog wiring and current 701952-byte UF2. Physical wheel
motion is user-observed; packet/controller evidence is independently captured.
No stopping-time, encoder-standstill, accuracy, repeated fault-injection,
runtime heap/stack or final-image recovery acceptance claim is made.

[Successful continuous-power capture](logs/2026-11-13-normal-stop-continuous-power.json),
[combined normal summary and capture fingerprints](logs/2026-11-13-normal-motor-observation-summary.json),
[exact normal build/source fingerprint](logs/2026-11-13-normal-motor-fingerprint.json).
No firmware/schema change was needed during these physical checks.


## S1 completion batch ? 13 November 2026

The user disconnected the car and requested all remaining S1 implementation
together, with one consolidated testing stage afterward. Following a usage
interruption, the implementation was completed and checked offline. No Pico,
broker, hotspot, USB device or service was operated in this batch. No flash,
commit or push occurred. Earlier hardware passes remain historical.

Requirements were re-read from the supplied Project Write-up.pdf pages 1?5
and 10?11 using a temporary verified parser, subsequently removed. Buddy 1
covers connectivity, MQTT/topics, telemetry, commands, recovery and
heartbeat/status; the telemetry list is examples. The mission is autonomous.
No remote steering/START/RESUME or S4 curb algorithm was added.

### Resulting implementation

- One current Packed v4: main 46 bytes; scan chunks 19..56; resources 40;
  heartbeat five; STATUS request/reply 5 / 16 or 62; STOP request/result 13 / 11.
  Earlier live formats and separate controller beacons are removed.
- S1 collects independent passive snapshots every 40 ms, publishes main data
  every 400 ms and is no longer triggered by ultrasonic queue submissions.
  S5 supplies a protected ten-byte range snapshot. GPIO/IRQ/echo behavior stays
  with S5; S2 retains PWM/encoder/PID ownership.
- Actual S2 feedback supplies wheel speed/counts/signed net travel/mode/reason
  and faults. True travelled path is a distinct optional producer.
- Stable weak/strong passive getters prepare barcode/navigation, actual path,
  complete scans and external fault reports. Missing owners remain unavailable;
  S1 does not steal controller events or fabricate algorithms/data.
- Per-source expiry, main generation boot time/session, automatic read-only
  STATUS clock anchoring and stale transport rejection. At most 181 cached
  scan points and 16 points/chunk, with bounded local backpressure retry.
- Controller state is four bits plus latch/TEST, with 14 defined states.
  Existing owner-controlled inhibit, leases/replay validation and correlation
  remain. Future states are published only when real owner behavior exists.
- Read-only allocator free/largest block introspection includes duplicate-size
  free-list buckets. Minimum free heap is sampled, not an exact peak.
  Requested application stacks exclude kernel/radio stacks and are not stack
  high-water. Unsupported high-water remains unavailable.
- Dashboard current decoder, motion/resources/scan views, CSVs and bounded
  evidence JSON; current observers and recovery recorder use the new frames.
  Nominal heartbeat changed 5 s to 4 s for jitter margin; this is not a measured
  cadence pass.

### Consolidated offline validation

All 15 groups pass across the initial and focused correction runs. The initial
failed outputs are retained, not overwritten. Failures corrected: strict C
indentation warnings, test clock/expiry-call assumptions and Windows COFF weak
symbol behavior (actual default/strong linkage checked with ELF GCC in WSL).

| Check | Result |
| --- | --- |
| Dashboard | 42 tests passed |
| Actual MQTT port | 51 checks passed |
| Portable codec + actual collector/cache bridge | 23 + 25 checks passed |
| Controller owner, normal and stationary | 17 checks in each mode |
| Actual normal controller/S2 STOP path | 13 checks passed |
| Recovery recorder | Eight tests passed |
| Cross-language C/JavaScript/MQTT vectors | 20,089 passed |
| Weak defaults / strong owner override | Actual linking passed |
| Read-only heap snapshot model | Queue/bucket traversal passed |
| Shared GPIO and analog policy | Passed |
| Analog/barcode | 16 groups passed |
| Browser/recorder syntax | Passed |
| Actual ARM modes | Normal, stationary, explicit IR, explicit encoder passed |
| S1 static stack-frame chain | 1680 / 2048 bytes; external calls/runtime peak unmeasured |

[Consolidated results](logs/2026-11-13-s1-batch-summary.json) identify every
passing log and preserved earlier failure. The host codec excludes the
board-only microsecond timer check; expected on-device S1 summary is
24 checks including that check, not observed on this new image.
Visual browser QA was not performed.

### Firmware and resources

Both final ARM links completed without compiler/linker warnings. Earlier
build attempts exposed the kernel/newlib size_t header boundary and the
project's single-trailing-underscore linker symbols. These were fixed before
the successful links. The RAM report includes vector/data/BSS so it agrees
with the actual section total.

| Profile | text | data | BSS | linked static RAM | UF2 bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Integrated normal TEST=0 | 355204 | 108 | 83248 | 83356 | 710656 |
| Integrated stationary TEST=1 | 359204 | 108 | 83088 | 83196 | 718848 |

Single retained file:
build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2
SHA-256: 13057e343727ce8e151784c944c86331f1f492c2e89959985b6628d86565e0f4

The successful normal check UF2 was hashed, then removed; its SHA-256 was
536a762f1b6663a15aff368a01713f7aec34a0f6ddce22ae3b27b7b1a1ad53a7.
Both ELF hashes and section figures are preserved in
[the artifact record](logs/2026-11-13-s1-batch-artifacts.json).

S1 message-buffer requests are now zero, previously 192 bytes. The packed
publication is 73 bytes plus four-byte time; the completed scan cache is
462 bytes; resource publication 19 bytes, sampled minimum and requested-stack
totals three bytes each. These are feature/cache costs, not a like-for-like
whole-firmware RAM saving. The historical JSON-to-v2 69-to-6-byte result is
preserved separately. Whole-device runtime peaks and group-wide application
representation still need their own evidence.

[Source/build fingerprint](logs/2026-11-13-s1-batch-fingerprint.json),
[current bit table](S1_COMPLETION_BATCH.md),
[producer handoff](PRODUCER_HANDOFF.md) and
[one combined physical checklist](FINAL_ACCEPTANCE.md) describe the ready batch.
Current-image accuracy, physical STOP, cadence/recovery, boot/power-bank and
resource captures remain deferred. Missing producers and the final group
demonstration remain owner/integration work. Credentials are excluded from
the reports and fingerprints.


## V4 collector regression and heap-lock correction ? 13 November 2026

The user flashed the first v4 image and reported every measurement unavailable.
A 20-second independent observation received 50 main frames among 62 packets,
a matching STATUS in 32 ms and valid Control disabled / TEST=true, but all
sensor fields were unavailable. The resource report retained static RAM 83196
while other resource fields were unavailable. This is a real regression,
not a broker/decoder setup failure.

The serial tool observed:
*** Hard fault ***  prc:2 ctxtsk:4
The collector is task 4. The first filtered tool capture did not save the
fault frame registers. The subsequent normal-restart serial capture has the
24/24 on-board codec/clock summary but did not establish working sensors.
Later observers received no Pico messages in their 12/30-second windows;
do not count those as successful recovery.

Code review found the new knl_Imeminfo used DI/EI, which only masks local
interrupts. It is called directly by the collector on either core, while
startup task/stack allocations may mutate the free queues on the other core.
It now uses BEGIN/END_CRITICAL_SECTION, taking the recursive SMP kernel lock
as allocating syscalls do. It still changes no allocator metadata. The
host regression now explicitly requires that cross-core exclusion, and its
actual-function free/bucket/empty/error tests pass.

Corrected stationary ARM link: text 359276, data 108, BSS 83088; static RAM
83196, UF2 718848 bytes; SHA-256
16d737e9056637d168f95f543258af2a5b08c9711245dc6c4d328a3ce30b3491.
This fixes a definite race but needs a new flash/live check before claiming
the observed fault is fully resolved. No schema/sensor algorithm change.
The prior normal ELF is historical; rebuild TEST=0 before future motor use.

Evidence: logs/2026-11-13T09-02-24-418Z-controller-observation.json,
logs/2026-11-13-v4-collector-fault-serial.txt, and
logs/s1-batch-2026-11-13T09-09-58/heap-snapshot.txt.


### Heap-only correction did not restore delivery; collector workspace reduced

The user confirmed flashing 16d737e9 and restarting. The following observer
window received no Pico messages. That correction is not a successful live
fix, and the allocator race must not be claimed as the established sole
cause of the original hard fault.

The collector's 1680-byte static chain left only 368 bytes for additional
kernel/library/interrupt frames. Its optional scan getter allocated a native
736-byte profile and a second 453-byte packed scratch array on every call,
even when no scan producer existed. The profile now uses one collector-owned
static workspace; all points are validated before packing directly into the
protected existing cache. Malformed/getter-busy input preserves the old cache.
Actual bridge/layout/allocator checks pass after this change.

ARM static collector chain is now 464 bytes, leaving 1584 of the configured
2048 for external calls (still not runtime high-water). The regression limit
is tightened to 768 bytes, reserving at least 1280 for external frames.
The hard-fault reporter's FIRST line now includes captured PC/LR before
scheduler diagnostics; this makes a repeated fault easier to locate.

New stationary link: text 359316, data 108, BSS 83824, static RAM 83932.
UF2 remains 718848 bytes, SHA-256
fe80eb5972c21d66adf3ad8908ca7699f4b940a56049b5ae1d59d90dccad084d.
No sensor algorithm, GPIO, motor behavior or binary schema changed.
Live verification is pending. Prior correction/failure records stay preserved.


### Definite initial-task stack overflow found and corrected

After the user reported fe80eb59 still failed, the startup test/console call
chain was compiled with ARM -fstack-usage. The initial task has only 1024 bytes
(include/sys/inittask.h). Previous S1 test frame 872 + usermain 24 +
tm_printf 32 + tm_vsprintf 96 + out_cons 16 = 1040 bytes, BEFORE additional
initial-task, USB/driver/kernel and interrupt frames. Thus the new test code
could corrupt memory even while every printed condition passed.

Only the large S1 pure-code test fixtures were moved to static TEST-only
storage. The current test frame is 144, known chain 312 / 1024. The regression
now checks the startup path as well as the collector; both pass. All 23 host
codec plus 25 actual collector checks still pass. No teammate algorithm change.
The source audit verifies all 22 recorded S2/S3/S4 files still match the
preceding successful normal-v3 source fingerprint.

Current stationary image: 719360 bytes, text 359332, data 108, BSS 84560,
static RAM 84668. SHA-256
390c589344927280ab77175a1cee2d04a07beacad2d522d63760146ad30f6f55.
The added 736-byte TEST-only fixture storage does not exist in normal mode.
Live sensor restoration remains pending this final flash; do not present
the previous heap/collector-only attempts as resolved hardware failures.

See logs/2026-11-13-v4-startup-stack-check.txt and
logs/2026-11-13-v4-owner-source-audit.json.


### Corrected image restores real measurements

The user explicitly confirmed flashing 390c5893 and restarting. Serial now
completes calibration, with on-board S1 24/24 and no hard-fault line in the
corrected capture. The independent 35-second observer received:
86 main frames, all 86 with valid ultrasonic distance (366..542 mm) and pitch
(-0.5..7.6 degrees); IR values 4,5,6,7; nine heartbeats; seven diagnostics;
one matching STATUS in 41 ms; zero decode errors. The user confirms the
refreshed/reconnected dashboard updates again.

Latest resources: static RAM 84668; free/largest/minimum-sampled heap
150384; requested application stacks 10240; unsupported stack high-water
unavailable. MQTT connection count remained one. Fifteen publication error
events were reported cumulatively; these are not silently treated as zero.
These resource samples are not exact heap peaks or stack high-water.

This establishes stationary functional restoration after the startup-test
stack correction. It does not establish calibrated distance/height accuracy,
controlled target-to-IR mapping, moving-motor STOP, repeated cold boots,
outage restoration or strict cadence acceptance on the new image.
No further flash is needed for this restored stationary session.

[Restoration summary](logs/2026-11-13-v4-restored-measurements-summary.json),
[corrected source/image identity](logs/2026-11-13-v4-startup-stack-fingerprint.json),
[source-owner audit](logs/2026-11-13-v4-owner-source-audit.json).


## Sensor branch downloaded and reviewed ? 13 November 2026

At the user's request, fetched origin/sensor (c8a789b, Add servo obstacle
avoidance). It is one commit ahead of the shared f37055f base and changes
five files. No merge/checkout/stash/commit/flash occurred. The existing
startup correction, main/source state and working 390c5893 UF2 are verified
unchanged. Only review notes/evidence/progress were added.

Temporary three-way simulation found conflicts in app.h, app_main.c and
S5 C. Actual temporary ARM probes show remote S5 cannot compile against our
current S1 headers: it uses removed S1_TELEMETRY/make/submit APIs in both
TEST modes. This is not a failure claim about its standalone branch.
The actual branch decision function also selects blocked left 240 mm over
clear right 260 mm due to its 30 mm preference around the 250 mm threshold.

The branch disables line following, commands S2 from S5 outside the current
controller latch and moves the servo without an APP_TEST guard. It does not
publish telemetry_get_scan, so S1's prepared scan path is not activated yet.
Do not treat a branch pull as integrated STOP or stationary validation.
A controller-owned motion handoff and passive S5 profile publication are the
concrete integration needs; no teammate algorithm was changed in this review.

[Full review](SENSOR_BRANCH_REVIEW.md) links merge, policy and compile evidence.


## Sensor branch integrated through the controller - 13 November 2026

The user explicitly authorized integrating sensor c8a789b with the latest
working source: current S1 API, preserved line following, authoritative STOP
and scan publication. A source/artifact rollback ZIP was created first;
[backup identity](logs/2026-11-13-before-sensor-integration-backup.json).

Adapted S5 servo/range/obstacle code and app_main controller arbitration.
APP_LINE_FOLLOW_ENABLED stays 1. S5 calls no S2 wheel API: it publishes phase/
time and immutable scan snapshots through the existing range semaphore.
Normal controller gates startup, STOP, faults, fresh line/motion and fresh
S5 intent before every wheel request. Clear front uses unchanged line
targets; scanning/unknown/busy/stale/blocked stops; clear-side intent permits
a brief controller-owned turn, then a fresh centre check.
The blocked-left/tie-margin case from the branch review is fixed.

The strong telemetry_get_scan automatically activates the existing S1 v4
transport/display, with sorted 30/90/150 points and null for missing echoes.
Front distance remains an actual centre sample, never a side reading.
The wheel-disabled TEST image now operates the servo. No new task/queue/
semaphore. S1/S2/S3/S4/browser/latch source and the line-enable flag were
verified unchanged against the rollback source archive. No Git checkout,
merge commit or push was made.

### Offline and build results

All 17 groups pass: 8 actual S5 task/IRQ/publication/serialization checks in
each mode; 22 actual controller/PID/motor checks (including STOP during
avoidance and later phase changes); existing 42 dashboard, 51 MQTT-port,
23 codec + 25 collector, 17 controller-owner per mode, 20,089 cross-language
vectors, 8 recorder, allocator, analog/barcode and shared GPIO checks.
The first producer-linkage run was blocked by WSL/E_ACCESSDENIED in the
sandbox, not a code failure; the authorized rerun passed. The initial new
motor-path test stub had a static/external declaration mismatch; corrected
before all actual controller/motor checks passed. Failed logs remain intact.

Four actual ARM profiles pass, with no S5 imports of subsystem2_set_*.
Both forced full links have zero warnings. Strong telemetry_get_scan is
verified in normal and stationary ELF; no new decoder or topic.
Stack budgets: collector 464/2048; S5 sensing chain 136/2048;
initial-task test/console chains S1 312/1024 and S5 208/1024.
Controller frame is within its 768-byte budget. These are static guards,
not measured runtime high-water.

| Build | text | data | BSS | static RAM | UF2 bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Normal | 356864 | 108 | 84008 | 84116 | 714240 |
| Stationary | 361420 | 108 | 84584 | 84692 | 723456 |

Stationary static RAM is 24 bytes more than the preceding 84668-byte
390c5893 image: 18 packed publication bytes plus alignment. Normal also
contains earlier collector fixes absent from the older historical normal
artifact, so do not call its total difference a pure S5 feature cost.
S1 queue requests remain zero; no additional task stack allocation.

Current single UF2:
build_make/mtk3pico_smp1_usb_cdc_wifi_lwip_mqtt_test.uf2
SHA-256 3a8b79c37a01cfa92615db69e62d2817795ab53ec9decae5e33c4faa19f3aaf6
The normal check image was hashed then removed; its ELF/hash/log remain in
[the artifact record](logs/2026-11-13-sensor-integrated-artifacts.json).
[Software summary](logs/2026-11-13-sensor-integration-checks.json).

### User flash and stationary physical confirmation

The user confirmed flashing 3a8b79c3 with the servo free to move.
Read-only STATUS confirmed Control disabled / TEST=true in 30 ms.
A 90-second observer recorded 220 main frames, all with valid centre distance
and pitch; line_bits 5; 26 completed three-point scan packets; 22 heartbeats;
zero decode errors. One actual profile was right30=229, centre90=90,
left150=81 mm. Serial summaries were S1 24/24 and S5 software 6/6; S4
completed calibration. The user confirms the servo sweeps, the profile
appears, and distance/IR/pitch still update.

Latest resources: static84692, free/largest/min-sampled heap150360,
requested app stacks10240. Cumulative publish errors25, MQTT connections1,
snapshot contention1; no zero-error publication or exact high-water claim.
All data are stationary observations, not calibrated reference readings.
No moving-wheel normal test, physical STOP during avoidance, controlled
orientation/settling test, floor trajectory or repeated recovery series was
performed for this image. Those remain on the combined checklist.

The diagnostic tool sent read-only STATUS only; the user operated the flash
and target. No broker interruption or automated motor command occurred.
[Live summary](logs/2026-11-13-sensor-integrated-live-summary.json),
[integration contract](SENSOR_INTEGRATION.md),
[remaining physical checklist](FINAL_ACCEPTANCE.md).
