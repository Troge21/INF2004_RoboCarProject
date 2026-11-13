# Integrated analog line and motor control — 12 November 2026

S3 owns ADC sampling. Left/centre line IR is GP27 / ADC1, right line IR
GP26 / ADC0; GP28 / ADC2 is barcode-reserved. GP16/17 and GP0/1 are wheel
encoders. The old GP6/GP16 digital steering path and GPIO barcode sampler
are inactive.

Each channel discards one conversion and averages two. Centre/left black
threshold is >=950 and right >=1420 on raw 0..4095; these are the teammate's
existing thresholds and need physical validation. Failed, short, out-of-range
or >100 ms ADC sampling is invalid. A nine-byte protected snapshot stores
timestamp:32, three raw values:12 each, validity:1 and reserved:3. Readers
derive pattern and check source age; only S3 accesses the ADC.

The controller polls every 20 ms and uses the teammate's RPM API/PID without
changing its gains, polarity, calibration or encoder driver:

| GP27 black | GP26 black | Pattern | Left/right target |
| --- | --- | ---: | --- |
| No | No | 0 | 35 / 35 RPM, centered forward |
| No | Yes | 1 | 35 / 26 RPM, correct right |
| Yes | No | 2 | 26 / 35 RPM, correct left |
| Yes | Yes | 3 | 35 / 35 RPM, cross junction |

Missing/invalid/stale sensor or motor feedback requests zero. A known S2
fault is reported as Controller fault. The controller checks its latched
STOP before steering, repeatedly requests zero and never clears the inhibit
because of new line input or reconnection. The S2 task alone writes PWM;
its watchdog and faults remain active. Command acceptance does not establish
physical wheel standstill. White/white is not a lost-line stop condition.

Default APP_MODE=integrated runs all subsystem lifecycles. TEST=1 integrated
is stationary (no motor task); TEST=0 integrated enables driving after the
3000 ms startup delay. APP_MODE=ir retains the separate diagnostic. TEST=1
APP_MODE=encoder selects the explicit powered encoder bench. These modes
never start competing application owners together.

Host checks:

~~~sh
python testing/subsystem2/test_analog_policy.py
python testing/subsystem1/test_motor_stop_path.py
python testing/subsystem3/test_barcode.py
python testing/subsystem1/test_gpio_sharing.py
~~~

The controller/PID test executes actual S2 command/control/motor code with
RTOS/encoder/PWM stubs; it does not operate a car. All new physical results
remain pending. The current artifact and two-phase stationary/motor check
are in [MOTOR_STOP_TEST.md](../subsystem1/MOTOR_STOP_TEST.md).