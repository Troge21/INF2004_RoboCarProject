# Subsystem 4: Bench Testing & Evidence Capture Guide

This guide explains how to wire the GY-511 sensor, build the test firmware, capture serial output logs, and record hardware evidence into `testing/subsystem4/TEST_LOG.md`.

---

## 1. Hardware Setup & Wiring

The GY-511 (LSM303DLHC accelerometer/magnetometer) communicates over **I2C1**:

| GY-511 Pin | Cytron Robo Pico / Pico W Pin | Description |
|:---|:---|:---|
| **VIN** | **3V3** (Pin 36 or Grove VCC) | 3.3 V Power Supply |
| **GND** | **GND** (Pin 38 or Grove GND) | Common Ground |
| **SDA** | **GP2** (Grove Port 2 SDA) | I2C1 Data line |
| **SCL** | **GP3** (Grove Port 2 SCL) | I2C1 Clock line |

> **Note:** The Cytron Robo Pico Grove Port 2 connects directly to `GP2` (SDA) and `GP3` (SCL) with onboard pull-up resistors.

---

## 2. Building the Test Firmware

Build the image using WSL (Ubuntu) or your cross-compiler environment:

```sh
cd build_make

# Clean previous build artifacts
make clean

# Build test firmware with on-target assertions enabled (TEST=1)
make TEST=1 -j8
```

The resulting firmware binary will be generated at:
`build_make/mtkernel_3.uf2` (or `mtk3pico_smp1_usb_cdc_wifi.uf2`).

### Flashing to Pico W:
1. Hold the white **BOOTSEL** button on the Pico W while plugging in the USB cable.
2. Release the button when the **`RPI-RP2`** drive appears on your computer.
3. Drag and drop the `.uf2` file onto the `RPI-RP2` drive.
4. The board will automatically reboot and start running.

---

## 3. Serial Monitor & Log Capture

The firmware outputs real-time telemetry and test results over USB serial at **115200 baud**.

### Method A: PuTTY (Windows) — Recommended for Auto-Logging
1. Open PuTTY.
2. Select **Serial**, enter your COM port (e.g. `COM5`), and set Speed to `115200`.
3. In the left panel, navigate to **Session** $\rightarrow$ **Logging**:
   * Select **All session output**.
   * Set **Log file name** to:
     `D:\SIT_Y2T1_RootFolder\Embedded Systems Programming_INF2004\INF2004_RoboCarProject\testing\subsystem4\logs\2026-10-xx-gy511-bench.txt`
4. Click **Open** to start monitoring and recording.

### Method B: VS Code Serial Monitor
1. Open the VS Code **Serial Monitor** tab.
2. Select the Pico COM port and `115200` baud.
3. Click **Start Monitoring**.
4. Right-click the terminal and choose **Select All**, then copy and paste into a new file in `testing/subsystem4/logs/`.

---

## 4. Step-by-Step Bench Test Verification

### Test 1: Sensor Probe & Baseline Calibration (S4-U01, S4-U03)
1. Place the robot car on a flat, level table.
2. Press the **RESET** button on the Robo Pico.
3. Observe the console log. You should see:
   ```text
   [S4] IMU Hump Task started (priority=18, period=20 ms, I2C1 GP2/GP3)
   [S4] GY-511 sensor detected. Entering stationary calibration...
   [S4] Calibration complete. Baseline pitch: 0.1 deg
   ```
4. If communication fails, check the wiring and ensure GP2 and GP3 are firmly plugged into Grove Port 2.

### Test 2: On-Target Math & NC3 Unit Checks (S4-U02, S4-U05, S4-U06)
In `TEST=1` builds, `usermain()` executes the automated unit checks:
```text
[TEST] S4-U02a PASS  flat acceleration vector computes 0.0 +/- 0.2 deg
[TEST] S4-U02b PASS  +15 deg incline vector computes +15.0 +/- 0.5 deg
[TEST] S4-U02c PASS  -15 deg decline vector computes -15.0 +/- 0.5 deg
[TEST] S4-U02d PASS  +30 deg steep vector computes +30.0 +/- 0.5 deg
[TEST] S4-U05a PASS  subsystem4_get_telemetry returns E_OK and valid snapshot
[TEST] S4-U06a PASS  subsystem4_reset_run clears run maximum to 0 mm
[TEST] subsystem4 SUMMARY 6 passed, 0 failed
```

### Test 3: Physical Hump Height & Maximum Retention (S4-U04, S4-U05)
1. Prepare a block of known height (e.g. wooden block or book of 25 mm height).
2. Move the car forward over the block at roughly 150 mm/s.
3. Check the console output:
   ```text
   [S4] HUMP DETECTED! Ascent pitch: 6.8 deg (start=12400 ms)
   [S4] VALID HUMP: Height=24 mm (duration=360 ms) | Run Max=24 mm
   ```
   *Expected result (NFR7):* $24\text{ mm}$ is within $\pm 20\%$ of $25\text{ mm}$ ($20\text{ mm}$ to $30\text{ mm}$).
4. Next, traverse a smaller 15 mm obstacle:
   ```text
   [S4] HUMP DETECTED! Ascent pitch: 4.2 deg (start=18200 ms)
   [S4] VALID HUMP: Height=14 mm (duration=320 ms) | Run Max=24 mm
   ```
   *Expected result (Decision NC3):* The recorded `Run Max` remains at **24 mm** and is **not** overwritten by the smaller 14 mm hump!

---

## 5. Saving and Committing Evidence

1. Save your raw console capture file as:
   `testing/subsystem4/logs/YYYY-MM-DD-gy511-bench.txt`
2. Open `testing/subsystem4/TEST_LOG.md` and add a new row under **Section 2: Results**:
   ```markdown
   | YYYY-MM-DD | S4-U04 | <commit-hash> | Thaw Zin Htun | PASS | logs/YYYY-MM-DD-gy511-bench.txt | Measured 24 mm on 25 mm test block (NFR7 met) |
   ```
3. Commit and push:
   ```sh
   git add testing/subsystem4/
   git commit -m "docs(subsystem4): add bench test log evidence for hump estimation"
   git push origin main
   ```
