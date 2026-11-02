# Subsystem 4 — Test Log: IMU Terrain Monitoring & Hump Detection

* **Subsystem:** Subsystem 4 (GY-511 Sensing, Tilt/Hump Detection, Height Estimation & Run Maximum)
* **Student Name:** Thaw Zin Htun
* **Student ID:** 2502712
* **GitHub Username:** thawzin07
* **Code:** `app_program/subsystem4/`
* **Hardware:** GY-511 (LSM303DLHC) on I2C1 (`GP2` SDA, `GP3` SCL)

---

## 1. Test Cases

Test cases trace directly to the **Week 6 Requirements**, **Tribunal 1 Agreements**, and **Integration Test Plan T5**.

| ID | What is Tested | Requirement / Interface | Method | Steps / How | Expected Result |
|---|---|---|---|---|---|
| **S4-U01** | GY-511 I2C1 probe & initialization | FR7, NFR13 | auto + manual | Flash image with GY-511 wired to GP2/GP3. Check serial log. | Driver prints `[S4] GY-511 sensor detected` and enters calibration. |
| **S4-U02** | Pitch calculation math ($0^\circ, \pm 15^\circ, +30^\circ$) | FR7, NFR7 | auto | Execute `subsystem4_test()` (`make TEST=1`). | PASS on S4-U02a, S4-U02b, S4-U02c, S4-U02d within $\pm 0.5^\circ$. |
| **S4-U03** | Stationary baseline calibration stability | A4, FR7 | auto + manual | Keep car stationary for 1.0 s during startup. | Baseline pitch logged within $\pm 1.0^\circ$ of level; noise $< \pm 0.3^\circ$. |
| **S4-U04** | Hump peak height estimation accuracy ($\pm 20\%$) | NFR7 | manual | Traverse 20 mm, 25 mm, and 35 mm test blocks at nominal 150 mm/s. | Estimated height within $\pm 20\%$ of physical reference block height. |
| **S4-U05** | Decision NC3: Valid-only run maximum retention | NC3, FR8, IF-02 | auto + manual | Traverse a 30 mm hump, then a 15 mm hump, then pause car. | Max height updates to 30 mm, ignores 15 mm, and is retained through stopping. |
| **S4-U06** | Interface IF-08: Confirmed new-run reset | NC3, IF-08 | auto | Call `subsystem4_reset_run()`. Check telemetry snapshot. | `max_hump_mm` cleared to 0 mm; previous run data reset. |
| **S4-U07** | Vehicle mounting axis and sign | FR7 | auto + manual | Synthetic vectors S4-U07a-d; then lift front wheels after flat startup calibration. | Negative sensor Y maps to positive vehicle pitch; flat and side tilt produce zero longitudinal pitch for ideal vectors. |
| **S4-U08** | Ascent timeout recovery | FR7 | manual | Lift front beyond ascent threshold and hold without completing descent. | Timeout after 3000 ms; return to monitoring without accepting a hump. |
| **S4-U09** | Complete detection sequence and undersized-hump rejection | FR7 | manual | Hand tilt front-up, rear-up, then flat within 3000 ms. | Enter ascent and descent; reject estimate below 6 mm without updating current/run maximum. |

Current mounting was identified from user-provided front-lift serial output:
X remained near -24 mg while Y changed from about -82 to -274 mg.
The task now uses negative-Y vehicle pitch for calibration and detection.
Front-lift response after this change has been observed (see results).
S4-U07 adds four checks, bringing the expected automated summary to 10
passed, 0 failed; their actual on-target output has not yet been captured.

*Method:*
* **auto**: Verified through `TEST_CHECK()` automated unit tests on-target or synthetic vectors.
* **manual**: Physical bench or track trials with measured blocks and console output.

---

## 2. Results

| Date | Test ID | Commit | Tester | Result | Log / Evidence | Notes |
|---|---|---|---|---|---|---|
| 2026-10-28 | S4-U02 | `PENDING` | Thaw Zin Htun | PASS | `logs/2026-10-28-gy511-unit-test.txt` | On-target unit test passed 4/4 pitch vectors. |
| 2026-10-28 | S4-U05 | `PENDING` | Thaw Zin Htun | PASS | `logs/2026-10-28-gy511-unit-test.txt` | Telemetry snapshot returned E_OK. |
| 2026-10-28 | S4-U06 | `PENDING` | Thaw Zin Htun | PASS | `logs/2026-10-28-gy511-unit-test.txt` | New-run reset zeroes maximum height. |
| 2026-11-02 | S4-U07 | Not captured; flashed revision unknown | User-provided COM3 output | PARTIAL: front-lift response verified | [Hand-tilt excerpts](logs/2026-11-02-gy511-hand-tilt.txt) | Baseline 4.9 deg; front-up pitch 8.7 deg; ascent detected. Automated S4-U07a-d results and side-tilt isolation remain unverified on hardware. |
| 2026-11-02 | S4-U08 | Not captured; flashed revision unknown | User-provided COM3 output | PASS (observed) | [Hand-tilt excerpts, attempt A](logs/2026-11-02-gy511-hand-tilt.txt) | Ascent entered State=3; holding tilt caused >3000 ms timeout. A subsequent attempt starts in State=2. No valid hump accepted. |
| 2026-11-02 | S4-U09 | Not captured; flashed revision unknown | User-provided COM3 output | PASS (observed) | [Hand-tilt excerpts, attempt B](logs/2026-11-02-gy511-hand-tilt.txt) | Entered State=4, then rejected 2 mm estimate at 2880 ms. Returned to State=2 with CurrentHump=0 and RunMax=0. This is a sequence/rejection check, not height-accuracy evidence. |

The new entries use the date these user-provided serial excerpts were recorded
from chat; exact capture timestamps, operator name and flashed commit were
not supplied. Preserve these entries and append repeat results with the exact
tested commit and operator for reproducible evidence.

Still pending for the current changes:

- On-target automated summary (expected 10 passed, 0 failed).
- Full S4-U01 startup probe/calibration output and S4-U03 level-reference
  accuracy/stability check. Monitoring was observed, but that alone does not
  establish the specified calibration tolerances; the displayed baseline was 4.9 deg.
- S4-U04 height accuracy using measured humps at measured/controlled speed.
- Physical S4-U05 maximum retention after a smaller valid hump; both manual
  attempts retained zero, so positive maximum retention has not been tested.
- Repeat S4-U06 reset verification for this revision and speed/telemetry integration.

---

## 3. Physical Bench Calibration Records

* **Sensor:** GY-511 (LSM303DLHC Accelerometer)
* **Scale Configured:** $\pm 2g$, High Resolution mode ($1\text{ mg/LSB}$)
* **Nominal Ground Speed:** 150 mm/s default; an update API exists, but actual S2 speed integration is not verified by these tests.
* **Sampling Rate:** 50 Hz (20 ms period)
* **Moving Average Filter Window:** 5 samples

*Detailed hardware bench test procedures and log saving steps are documented in [BENCH_TEST_GUIDE.md](BENCH_TEST_GUIDE.md).*
