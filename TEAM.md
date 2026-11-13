# Team and subsystems

## S5/controller scan integration - 13 November 2026

User-authorized adaptation of sensor c8a789b: S5 owns GP4/5 ultrasonic and
GP12 servo, publishing centre range, a packed five-byte phase/time snapshot
and thirteen-byte three-point profile/time/ID under the existing range semaphore.
S1's prepared strong getter path carries the sorted 30/90/150 degree scan.
The existing controller alone calls S2, with STOP/startup/feedback/freshness
gates before line or turn intent; line following remains enabled.
S5 never calls a wheel API. No new task, queue or kernel object. TEST=1 keeps
wheel motors disabled while servo scans remain enabled.
[Integration and verification](testing/subsystem1/SENSOR_INTEGRATION.md).
Earlier dated narratives below describe their respective stages.


## Current S1 transport / owner interfaces ? 13 November 2026

S1 now gathers passive protected snapshots independently of S5 every 40 ms
and transmits one Packed v4 main frame every 400 ms. S2 measured motion and
known faults are connected; S3/S4 ownership stays unchanged. S5 publishes a
ten-byte protected range view. S1 has one semaphore, zero message buffers,
a 73-byte packed publication and one 462-byte completed scan cache.
Barcode/path/external fault producers use
[telemetry_sources.h](app_program/telemetry_sources.h); see
[the owner handoff](testing/subsystem1/PRODUCER_HANDOFF.md).
A strong owner getter activates the existing transport/display without queue
consumption. Missing algorithms stay owner work.

Current controller word is state4/latch1/TEST1, six bits; its protected
timestamped publication stays five bytes. State travels in the main frame,
not a beacon. STATUS is 16/62 bytes and STOP result 11 bytes. Existing
persistent inhibit/zero-duty handoff is implemented; the dedicated S2
stop-event flag below is an alternative proposal, not required for this
existing STOP path. Physical STOP passed on the preceding v3 image; v4
offline checks/builds pass and its hardware session is deferred.
Earlier dated integration narratives below are historical.

## Integrated AO / S1 startup — 12 November 2026

The user explicitly requested integration after pulling f37055f. Default
APP_MODE=integrated now starts S1, S2, S3 AO sampling, S4, S5 and the shared
controller. APP_MODE=ir selects the retained standalone analog diagnostic;
TEST=1 APP_MODE=encoder explicitly selects the powered S2 diagnostic. TEST=1
with integrated mode is stationary and contains no motor task.

S3 alone reads ADC2/1/0 on GP28/27/26, using the teammate thresholds and
discard-plus-two-sample average. Its shared raw/validity/timestamp record is
nine packed bytes; the line pair remains a nonblocking snapshot API. GP16
is encoder-only. No digital barcode capture or timer-2 sampler is started
on AO wiring; GP28 decoding remains pending. The current line geometry uses
both white or both black for forward, individual black for corrections.

The controller keeps the teammate RPM targets/PID and S2 driver ownership.
It checks fresh analog and motor feedback, handles latched STOP before any
steering, and reports a known S2 fault rather than successful STOP. S2/ultra
retain the shared per-pin IRQ13 dispatcher; echo waits cannot mask encoders.
This is local user-authorized integration, not a fabricated team sign-off or
physical pass. Earlier sections below describe historical configurations.
See dashboard/README.md and testing/subsystem1/MOTOR_STOP_TEST.md.



## Reusable S2 feedback and PID - 11 November 2026

S2's core-0 `motor_task` (priority 14) now owns PWM, encoder setup,
counts/RPM/distance estimation and optional RPM-mode PID control. There
are two private S2 semaphores: commands and coherent read-only feedback.
`subsystem2_set_rpm_x10()` requests wheel speeds; `subsystem2_set_duty()`
retains the open-loop API. `subsystem2_get_motion()` provides a snapshot
for future telemetry consumers. No S1/Wi-Fi/MQTT/dashboard code is changed.

GPIO IRQ13 on core 0 is shared through `app_program/gpio_irq.c`; callbacks
are registered per pin. S2 owns GP16/GP0 rising edges, using GP17/GP1 for
direction. S5 owns GP5 rise/fall events. S5 now masks only its echo pin
while idle instead of disabling wheel encoder interrupts. Its telemetry
code is unchanged. All GPIO IRQ13 users must use this dispatcher.

Normal analog tracking requests 35/35 RPM forward or 26/35 and 35/26 RPM
for corrections (`S2_LINE_USE_RPM_CONTROL=1`). The left IR remains barcode
only. Initial PID gains are provisional. The dedicated TEST=1 bench
requests 35 RPM per wheel after 5 seconds, runs for 10 seconds, then stops.
Its coordinator is priority 15/core 0; it buffers samples while moving.
Faults latch off until reboot, and the 100 ms command watchdog remains.
Latest user-confirmed 10-turn zero-start totals L=-6337/R=5409 set x1
counts/rev to **633.7 left / 540.9 right**, superseding the earlier
636.8/588.2 calibration. PID tuning remains provisional; the new scale
requires another raised-wheel and floor check.
See `testing/subsystem2/MOTOR_FEEDBACK.md` for API units and test details.

## Earlier standalone encoder check - 11 November 2026

User-confirmed wiring: left encoder A/B GP16/17, right encoder A/B GP0/1.
IR AO inputs are left/barcode GP28, centre GP27 and right GP26.
`TEST=1` with `S2_ENCODER_CALIBRATION=1` starts the standalone S2 encoder
bench, owning IO_BANK0 IRQ13 exclusively on core 0. Hand-turn mode is
`S2_ENCODER_POWERED_TEST=0`. The current powered mode (`=1`) starts the
existing motor task (priority 14, core 0) plus the timed command/RPM bench
(priority 15, core 0): wait 5 seconds, run left at 18% and right at 20% for 5 seconds,
then stop until reset. Its provisional x1 counts/rev are L=636.8/R=588.2,
with the left encoder sign inverted. The motor watchdog remains active.
Both tyres are provisionally treated as 65 mm outside diameter. The bench
now reports signed per-wheel distance and their average, using total
counts from run start through the stop window. This is encoder odometry
in the bench only; normal driving feedback/PID integration is pending.
The revised v2 bench buffers samples during motion, prints them after
stopping, and repeats supervisor status outside the run. It no longer
creates a separate core-1 reporter task.
Normal builds still enter the standalone analog IR/steering test. Legacy
normal-subsystem descriptions below are historical and are not the active
standalone wiring. See `testing/subsystem2/MOTOR_FEEDBACK.md` for the current setup.

## Current centre/right tracking and left barcode setup - 3 November 2026

Three IR inputs are used: left GP26 for barcode capture, centre GP6 and right
GP16 for tracking. Line patterns use centre/right bits 2/1; the left sensor
never contributes. Normal builds enable driving after 3 seconds. Both-black
moves forward, centre-only steers left, and right-only steers right. Both-white
searches toward the last detected side for up to 200 ms at 20% duty, then stays
stopped until the line returns. No side history means stop immediately.
Stale/invalid line data stops and clears history. TEST builds disable driving.
Barcode timer alarm 2 / IRQ2 and decoding remain active; decoded characters
print without issuing motor commands. Physical trials remain pending.
The barcode bench descriptions below are historical, not current settings.

## Earlier barcode bench update - 3 November 2026

Zayar is now developing barcode software; a teammate is handling hardware.
The user confirmed that the LEFT sensor will be repurposed. Current code uses
GP26 exclusively as the digital barcode input. Centre GP6/right GP16 remain
line inputs; their physical layout is undecided. The historical three-sensor
ownership/design entries below describe the previous arrangement.

S3 now reserves **timer alarm 2 / IRQ2 on core 0** for 500 us input sampling.
No other task, SDK alarm pool or subsystem may claim this alarm. GPIO IRQ13
remains with the existing ultrasonic/encoder bench code. S3's `ir_task` still
runs at priority 17/core 0 and owns the sensors, decoder, capture ring, line
snapshot semaphore and a 64-byte result message buffer (one-byte payload).
The shared `barcode_report` task is priority 16/core 0, consumes results using
`subsystem3_get_barcode()` and only prints characters. There is no agreed
letter-to-navigation-command mapping in this update.

`APP_LINE_FOLLOW_ENABLED=0` is the default for barcode bench testing; the old
controller is not validated for the new geometry. See
`testing/subsystem3/BARCODE_GUIDE.md` for input assumptions, sampling limits,
result format and the remaining hardware/integration checks. These changes
are a local review copy, not evidence of team approval or physical testing.

The tables are pre-filled with the **example** in `app_program/` (marked
*example*). Replace those rows with your own as you replace the example code.

Fill this in at the start of the project and keep it up to date. It is the
single place that says **who owns what**, so that five people can work in
one firmware image without clashing over pins, priorities, or kernel objects.

A **subsystem** is one student's part of the product, with its own folder.
It may contain several tasks and own several peripherals (see section 3).

Rule: **only the owner edits a subsystem's folders.** To change another
subsystem, ask its owner or open a pull request for them to review.

## 1. Members and subsystems

| # | Subsystem (what it does) | Student ID | GitHub username | Code | Tests |

| 1 | Wilson Teng Hao Feng — WiFi communication, permitted command handling and telemetry; integrate through the built-in network service | 2503002 | (UPDATE) | `app_program/subsystem1/` | `testing/subsystem1/` |

| 2 | Zayar Hpoun Myint — motor control, encoders, speed/distance estimation and turning | 2502688 | (UPDATE) | `app_program/subsystem2/` | `testing/subsystem2/` |

| 3 | Troy Ang Songze — digital IR sensing, line/junction detection and barcode decoding; report navigation results to controller | 2503451 | Troge21 | `app_program/subsystem3/` | `testing/subsystem3/` |

| 4 | Thaw Zin Htun — GY-511 sensing, tilt/hump detection, height estimation and run maximum | 2502712 | thawzin07 | `app_program/subsystem4/` | `testing/subsystem4/` |

| 5 | Siyona — servo/ultrasonic scanning, obstacle profile and clearance measurements | 2502714 | (UPDATE) | `app_program/subsystem5/` | `testing/subsystem5/` |

Shared files (`app_program/app_main.c`, `app_program/app.h`, this file)
are changed only by agreement of the team. The exception is claiming an empty
row in this file (a pin, a peripheral, an interface ID), which anyone may do
(section 2).

## 2. Pins

One owner per pin. Claim a pin here **before** you wire or code it.

**Why:** two subsystems using the same pin is one of the most common and
hardest-to-find mistakes in a team project. The code builds without any
error, but the hardware misbehaves. Each subsystem can pass its own tests
and then fail once they are combined, and two outputs driving the same pin
against each other can damage it. Writing the claim down first means a clash
is spotted when someone edits this table, not hours later on the bench.

**No instructor check needed.** The team keeps this table itself. You do not
need the instructor, or the rest of the team, to check or approve a claim:
if the row is empty, fill it in and commit. If two people claim the same
pin, they agree between themselves who moves. Only the *reserved* rows are
fixed.

| Pin | Owner | Used for |
|---|---|---|
| GP23, GP24, GP25, GP29 | *reserved* | Pico W radio |
| GP0 | S2 - Zayar | (taken whenever the team builds with `CRASH_REPORT=1`) | Right wheel encoder A
| GP1 | S2 - Zayar | (taken whenever the team builds with `CRASH_REPORT=1`) | Right wheel encoder B
| GP2 | S4 - Thaw Zin | GY-511 SDA — I2C1 |
| GP3 | S4 - Thaw Zin | GY-511 SCL — I2C1 |
| GP4 | S5 - Siyona | Ultrasonic TRIG — GPIO output |
| GP5 | S5 - Siyona | Ultrasonic ECHO — GPIO input |
| GP6 | | Former digital centre IR input; currently unused |
| GP7 | | Former left encoder A; currently unused |
| GP8 | S2 - Zayar | Left motor M1A — PWM/GPIO output |
| GP9 | S2 - Zayar | Left motor M1B — PWM/GPIO output |
| GP10 | S2 - Zayar | Right motor M2A — PWM/GPIO output |
| GP11 | S2 - Zayar | Right motor M2B — PWM/GPIO output |
| GP12 | S5 - Siyona | Servo control signal — PWM output |
| GP13 | | |
| GP14 | | |
| GP15 | | |
| GP16 | S2 - Zayar | Left wheel encoder A |
| GP17 | S2 - Zayar | Left wheel encoder B |
| GP18 | | |
| GP19 | | |
| GP20 | | |
| GP21 | | |
| GP22 | | |
| GP26 | S3 - Troy | Right IR AO - ADC0 |
| GP27 | S3 - Troy | Centre IR AO - ADC1 |
| GP28 | S3 - Troy | Left / barcode IR AO - ADC2 |

## 3. Peripherals

**Subsystems, tasks and peripherals are three different things:**

| Term | What it is | Example |
|---|---|---|
| **Subsystem** | one student's part of the product, one folder | "environment sensing", "motor control", "user interface" |
| **Task** | one thread of execution. A subsystem may have **as many tasks as it needs** | a sampling task and a filtering task, both in "environment sensing" |
| **Peripheral** | one hardware block on the RP2040 | I2C0, UART1, the ADC |

A driver is **not** a subsystem. It is code *inside* the subsystem that
needs that hardware: the "environment sensing" subsystem contains the I2C
sensor code, and owns I2C0. Write it as `drv_<name>.c` / `drv_<name>.h` in
that subsystem's folder, never under `device/`, and call it only from the
peripheral's owning task. The kernel's own drivers in `device/` (serial,
I2C, ADC) may be used instead; switching one on in `config/config_device.h`
changes shared configuration, so record it in the table below first.

**The rule: each peripheral is driven by exactly one task, its *owning
task*.** That task can be in any subsystem, and one subsystem may own
several peripherals (one owning task each, or one task owning all of them).
Every other task, in the same subsystem or another one, asks the owning task
through a kernel object (section 5) and never touches that peripheral itself.

*Why:* this port runs tasks on both cores at the same time. If two tasks
drive the same I2C or UART block at once, their transfers get mixed up, and
the drivers have no locking to stop it.

| Peripheral | Owner subsystem | Owning task | Pins |
|---|---|---|---|
| UART1 | Not used | | |
| I2C0 | Not used | | |
| I2C1 | S4 - Thaw Zin| IMU / Hump Task | GP2 SDA / GP3 SCL |
| SPI0 | Not used | | |
| SPI1 | Not used | | |
| ADC | S3 via kernel ADC device | ir_task requests conversions; kernel driver owns ADC IRQ | GP28/27/26 AO; never GP29 |
| Motor PWM slices 4 and 5 | S2 - Zayar | `motor_task` | GP8 / GP9 / GP10 / GP11 |
| Servo PWM slice — number TBD | S5 - Siyona | Servo / Ultrasonic Task | GP12 |
| IO_BANK0 IRQ13 dispatcher | S2 + S5, per-pin callbacks | `gpio_irq.c`, core 0 | S2: GP16/GP0 rising; S5: GP5 rise/fall via 5 V to 3.3 V divider |
| Timer alarm 2 / IRQ2 | Inactive in AO integrated mode | Legacy digital capture not started | No current barcode timer claim |
| WiFi radio, TCP/IP, MQTT | *kernel* | `cyw43_utk_task` (built in) | GP23–25, GP29 |

### WiFi and MQTT are special

The radio and the network stack belong to the kernel's built-in WiFi service
task, `cyw43_utk_task` (`lib/libwifi/sysdepend/pico_rp2040/`). **No student
task may call `cyw43_...`, lwIP or MQTT functions, not even from a
"WiFi subsystem".** The calls are not safe from any other task, on either core.

What students *can* do:

- **Read the connection state** from any task, which is always safe:
  ```c
  #include "cyw43_utk.h"
  T_CYW43_UTK_STATUS st;
  cyw43_utk_get_status(&st);   /* st.ready, st.link_status, st.ip_addr, ... */
  ```
- **Send or receive data over the network** by adding code that *runs
  inside the service task*. The MQTT client is in
  `lib/liblwip/sysdepend/pico_rp2040/mqtt_utk.c`. It publishes a heartbeat
  today. A "cloud / reporting" subsystem would add a kernel object
  (e.g. a message buffer) that application tasks fill. The MQTT code then
  reads it with `TMO_POL` (never blocking) and publishes it.
  Because this changes shared library code, **agree it with the team and
  the instructor first**, and record it as an interface in section 5.
  It needs the `NET=lwip MQTT=1` build (README section 3).

## 4. Task priorities

1 is the highest. 1–4 are used by the kernel (USB console 3, WiFi 4). Each subsystem gets its own band by default;
change it only by team agreement. Write down every task you create.

| Subsystem | Priority band | Tasks (name – priority – core) |
|---|---|---|
| 1 — Wilson | 12–13 | `wifi_status_task` – 12 – any core; telemetry gathering/status monitoring; MQTT remains in its network owner |
| 2 — Zayar | 14–15 | `motor_task` – 14 – core 0; optional `encoder_bench` – 15 – core 0 |
| 3 — Troy | 16–17 | ir_task – 17 – core 0; AO sampling and protected line publication |
| 4 — Thaw Zin | 18–19 | `imu_hump_task` – 18 – core 0 (`TP_PRC1`) |
| 5 — Siyona | 20–21 | `ultrasonic_task` – 20 – core 0 (`TP_PRC1`); servo not started |

Core: `TP_PRC1` = core 0, `TP_PRC2` = core 1, or "any".

Term	Meaning	Example
Priority band :	A range of priority numbers allocated to a subsystem.	S3 gets 16–17, so its tasks can use 16 or 17.
Priority :	The specific number assigned to one task. Smaller number = higher priority.	Your IR task uses 16.
Core :	Which processor core may run the task. The Pico’s RP2040 has two cores.	core 0, core 1, or any.

## 5. Interfaces between subsystems

Every arrow between two subsystems is listed here. The **provider** creates
the kernel object and publishes its ID in its own `subsystemN.h`; the **user** only
sends to it or waits on it. Agree on the data format before coding.

| ID | From → To | Mechanism | Object ID name | Data / meaning | Agreed by |
|---|---|---|---|---|---|
| IF-01 | S3 → Controller/S1 | Nonconsuming semaphore-protected ADC snapshot; S3 owns | subsystem3_get_line() | GP27/26 threshold pattern, validity, source time; nine packed shared bytes; GP28 barcode not decoded | Local user-authorized 12 Nov integration; owner/hardware review pending |
| IF-02 | S4 → Controller | Result queue; type TBD; controller owns | TBD (suggested `mbf_hump_result`) | Tilt/hump state, height, run maximum, validity and sensor faults | TBD: Thaw Zin + controller owner |
| IF-03 | S5 -> Controller/S1 | Protected nonconsuming navigation/scan snapshots | subsystem5_get_navigation() / telemetry_get_scan() | S5 phase expires after 200 ms; completed immutable three-angle scan; no echo is unknown | User-authorized sensor integration, 13 Nov; physical trajectory review pending |
| IF-04 | Controller → S2 | Existing latest-command channel owned by S2 | subsystem2_set_rpm_x10() / set_duty() | 35/35 RPM forward, 26/35 or 35/26 correction; zero STOP; controller latch takes priority; 100 ms S2 watchdog retained | Local user-authorized integration; hardware pass pending |
| IF-05 | S2 ? Controller/S1 | Protected nonblocking S2 snapshot | subsystem2_get_motion() | Fresh measured speed/counts/signed net distance/mode/reason/faults in v4; true path has separate optional getter | Implemented; current-image accuracy pending |
| IF-06 | Controller ? S2 (alternative proposal) | Separate stop event flag, not used by current latch | TBD flg_motor_stop | Existing inhibit/zero-duty API already implements STOP; separate wake/arbitration would need owner agreement | Optional future design, not an unfinished current S1 path |
| IF-07 | Controller/S5 scan coordination | Autonomous S5 scan loop; no request queue needed in current mode | S5 navigation publication | Centre <=250 mm triggers a scan; controller stops wheels during scanning and arbitrates escape intent | Implemented current integration; future requested/dense scans remain owner work |
| IF-08 | Controller → S4 | Run-reset queue; type TBD; S4 owns | TBD (suggested `mbf_run_reset`) | Confirmed new-run reset; temporary stops do not clear maximum hump height | TBD: Thaw Zin + controller owner |
| IF-09 | Controller ? S1 | Protected timestamped snapshot; controller owns semaphore | app_controller_snapshot() | Boot time32 + state4/latch1/TEST1/reserved2; 500 ms expiry; actual state in v4 main and STATUS | Implemented/offline checked; current-image physical check deferred |
| IF-10 | S1 → Controller | One protected request and result slot; controller owns | `app_controller_submit_stop()` / `app_controller_take_reply()` | STOP ID/session/deadline; controller owns persistent inhibit and zero-duty handoff to existing S2 API; success only after owner processing; no RESUME | Local user-authorized implementation, 11 Nov; team review/hardware validation pending |
| IF-11 | S1 ? kernel network service | Protected packed latest publication, nonblocking copy | s1_network_poll() / s1_network_aux() | 46-byte main, bounded scan and resource packets; explicit source/session/boot freshness; no S1 MBFs | Implemented/offline checked; current-image physical check deferred |
| IF-12 | Kernel network service ? S1/controller | Network-owner nonblocking dispatch | bounded 13-byte RX slot; S1 APIs | STATUS read-only; STOP session/ID/deadline/length/rate checks; correlated results, no beacon | Implemented/offline checked; current-image physical check deferred |
| IF-13 | S5 ? S1 | S5-owned passive range snapshot/semaphore | subsystem5_get_range() | Ten-byte packed sequence/time/distance12/present1 view; invalid echo unavailable; no trigger/IRQ behavior change | Implemented/offline checked; current-image physical check deferred |

Each interface is tested by at least one integration test
(`testing/integration/TEST_LOG.md`).

## 6. Kernel object budget

Limits are set in `config/config.h`. If the team needs more, raise the
limit there. The numbers below describe the current build; demo sample objects are retired.
Optional S2 calibration adds one task when enabled in a test build.

| Object | Limit | S1 | S2 | S3 | S4 | S5 |
|---|---|---|---|---|---|---|
| Task | 32 | 1 | 1 normal / 2 powered bench | 1 | | 1 |
| Semaphore | 16 | 1 | 2 when motor task is used | 1 | 1 | 2 |
| Event flag | 16 | | | | | |
| Mailbox | 12 | | | | | |
| Mutex | 12 | | | | | |
| Message buffer | 12 | 0 | | | | |
| Fixed memory pool | 12 | | | | | |
| Variable memory pool | 12 | | | | | |
| Cyclic handler | 12 | | | | | |
| Alarm handler | 12 | | | | | |

(The kernel's USB and WiFi service tasks also use a few of these.)

### S3 to app_main controller to S2 integration

S3 publishes `S3_LINE_DATA` through `subsystem3_get_line()` using its own
semaphore. `vehicle_controller_task` in app_main.c (priority 15, core 0) polls
S3 every 20 ms and owns line steering/recovery. Controller tuning is in app.h.
It publishes forward-only 0..100% wheel commands through
`subsystem2_set_duty()`. S2's motor_task (priority 14, core 0) alone writes PWM
and stops on missing or older-than-100-ms commands. S2 owns the command semaphore
and PWM slices 4/5; servo slice 6 is untouched. S3's ir_task uses priority 17.
Normal builds start following after 3 seconds. APP_TEST builds disable the
motor task but now run the supervisory controller without motor calls; encoder
calibration mode publishes controller unavailable. The controller owns a
persistent stop inhibit and uses the existing S2 zero-duty API. The proposed
separate S2 stop-event arbitration (IF-06) remains unimplemented.
See `testing/subsystem2/LINE_FOLLOW.md` for behavior, settings and bench checks.

## Current controller/S1 integration — 11 November 2026

This is the local implementation explicitly requested in the user session,
not evidence of teammate/instructor sign-off. S2–S5 source folders are unchanged.
Review the shared interfaces with their owners before the group merge/demo.

`vehicle_controller_task` remains priority 15/core 0. It now runs in stationary
builds too, publishing Control disabled and servicing a logical stop latch;
it never calls S2 in that mode. The controller adds **one semaphore** and no
kernel message buffer. Its 2048-byte dynamic task stack/kernel overhead are
additional in TEST=1; the normal driving image already had this task. The
object-budget table above is incomplete for other subsystems and is not a
whole-firmware allocation measurement.

The normal controller services STOP during startup and before each line
iteration, submits zero duty repeatedly while latched, and never clears it
on new line data or reconnect. S2 retains PWM ownership. If controller lock
access is busy, the normal loop requests zero duty for that iteration.
Published state uses a protected five-byte timestamp/state snapshot. Sensor
telemetry remains 11 bytes; state/session/latch beacons use six bytes on the
existing status topic. Full contracts, costs and the pending physical test:
[Controller commands](testing/subsystem1/CONTROLLER_COMMANDS.md).
