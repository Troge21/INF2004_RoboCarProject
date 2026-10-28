# Team and subsystems

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
| GP6 | S3 - Troy | Centre IR sensor DO — GPIO input |
| GP7 | S2 - Zayar | Left wheel encoder A |
| GP8 | S2 - Zayar | Left motor M1A — PWM/GPIO output |
| GP9 | S2 - Zayar | Left motor M1B — PWM/GPIO output |
| GP10 | S2 - Zayar | Right motor M2A — PWM/GPIO output |
| GP11 | S2 - Zayar | Right motor M2B — PWM/GPIO output |
| GP12 | S5 - Siyona | Servo control signal — PWM output |
| GP13 | | |
| GP14 | | |
| GP15 | | |
| GP16 | S3 - Troy | Right IR sensor DO — GPIO input |
| GP17 | | |
| GP18 | | |
| GP19 | | |
| GP20 | | |
| GP21 | | |
| GP22 | | |
| GP26 | S3 - Troy | Left IR sensor DO — GPIO input |
| GP27 | | |
| GP28 | S2 - Zayar | Left wheel encoder B |

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
| ADC | Not used | | |
| Motor PWM slices — numbers TBD | S2 - Zayar | Motor Control Task | GP8 / GP9 / GP10 / GP11 |
| Servo PWM slice — number TBD | S5 - Siyona | Servo / Ultrasonic Task | GP12 |
| IO_BANK0 IRQ13, GP5 echo edges | S5 - Siyona (current bench build) | `ultrasonic_task`, core 0 | GP5 via 5 V to 3.3 V divider; incompatible with optional S2 encoder calibration |
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
| 1 — Wilson | 12–13(TBD) | Telemetry / Command Task – TBD (Low intended) – TBD |
| 2 — Zayar | 14–15(TBD) | Motor Control Task – TBD (High intended) – TBD  |
| 3 — Troy | 16–17(TBD) | IR / Barcode Task – TBD (High intended) – TBD |
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
| IF-01 | S3 → Controller | Result queue; concrete kernel type TBD; controller owns | TBD (suggested `mbf_line_barcode`) | IR/line status, barcode result validity and supported action; reject invalid/incomplete captures; report each valid barcode once | TBD: Troy + controller owner |
| IF-02 | S4 → Controller | Result queue; type TBD; controller owns | TBD (suggested `mbf_hump_result`) | Tilt/hump state, height, run maximum, validity and sensor faults | TBD: Thaw Zin + controller owner |
| IF-03 | S5 → Controller | Result queue; type TBD; controller owns | TBD (suggested `mbf_scan_result`) | Distance, scan profile/clearance, validity and failure; missing echo is not clear space | TBD: Siyona + controller owner |
| IF-04 | Controller → S2 | Latest motor-command channel; type TBD; S2 owns | TBD (suggested `mbf_motor_cmd`) | Requested left/right movement, bounds and freshness; exact units/ranges TBD | TBD: Zayar + controller owner |
| IF-05 | S2 → Controller | Feedback queue; type TBD; controller owns | TBD (suggested `mbf_motion_feedback`) | Encoder counts, wheel speed, distance, motion/fault status and validity | TBD: Zayar + controller owner |
| IF-06 | Controller → S2 | Separate stop event flag, owned by S2 | TBD (suggested `flg_motor_stop`) | Stop overrides movement queue; wake path, latch/clear rules and stale-command invalidation TBD | TBD: Zayar + controller owner |
| IF-07 | Controller → S5 | Scan-request queue; type TBD; S5 owns | TBD (suggested `mbf_scan_request`) | Coarse/fine scan request and angle parameters; units, bounds and timing TBD | TBD: Siyona + controller owner |
| IF-08 | Controller → S4 | Run-reset queue; type TBD; S4 owns | TBD (suggested `mbf_run_reset`) | Confirmed new-run reset; temporary stops do not clear maximum hump height | TBD: Thaw Zin + controller owner |
| IF-09 | Controller → S1 | Telemetry queue; type TBD; S1 owns | TBD (suggested `mbf_telemetry`) | Operating state and available motion/line/barcode/hump/obstacle measurements with validity | TBD: Wilson + controller owner |
| IF-10 | S1 → Controller | Request/status queue; type TBD; controller owns | TBD (suggested `mbf_supervisory`) | Validated permitted commands and connection state; command list/topics TBD; reject stale replay | TBD: Wilson + controller owner |
| IF-11 | S1 → kernel network service | Proposed outbound message buffer owned by S1; service polls with `TMO_POL` | TBD (suggested `mbf_network_tx`) | Encoded telemetry/heartbeat for publication inside network task; no blocking network-service wait | TBD: Wilson + team + instructor |
| IF-12 | Kernel network service → S1 | Proposed bounded nonblocking inbound channel owned by S1; type TBD | TBD (suggested `mbf_network_rx`) | Received command payload/connection events for application handling; overflow policy TBD | TBD: Wilson + team + instructor |

Each interface is tested by at least one integration test
(`testing/integration/TEST_LOG.md`).

## 6. Kernel object budget

Limits are set in `config/config.h`. If the team needs more, raise the
limit there. The numbers below describe the current build; demo sample objects are retired.
Optional S2 calibration adds one task when enabled in a test build.

| Object | Limit | S1 | S2 | S3 | S4 | S5 |
|---|---|---|---|---|---|---|
| Task | 32 | | | 1 | | 1 |
| Semaphore | 16 | | | | | 1 |
| Event flag | 16 | | | | | |
| Mailbox | 12 | | | | | |
| Mutex | 12 | | | | | |
| Message buffer | 12 | | | | | |
| Fixed memory pool | 12 | | | | | |
| Variable memory pool | 12 | | | | | |
| Cyclic handler | 12 | | | | | |
| Alarm handler | 12 | | | | | |

(The kernel's USB and WiFi service tasks also use a few of these.)
