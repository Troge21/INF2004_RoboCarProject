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

---

## 3. Physical Bench Calibration Records

* **Sensor:** GY-511 (LSM303DLHC Accelerometer)
* **Scale Configured:** $\pm 2g$, High Resolution mode ($1\text{ mg/LSB}$)
* **Nominal Ground Speed:** 150 mm/s (updated dynamically from Subsystem 2 encoder telemetry)
* **Sampling Rate:** 50 Hz (20 ms period)
* **Moving Average Filter Window:** 5 samples

*Detailed hardware bench test procedures and log saving steps are documented in [BENCH_TEST_GUIDE.md](BENCH_TEST_GUIDE.md).*
