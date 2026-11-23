# Integrated analog line and motor control — 12 November 2026

S3 owns ADC sampling. Left/centre line IR is GP27 / ADC1, right line IR
GP26 / ADC0; GP28 / ADC2 is barcode-reserved. GP16/17 and GP0/1 are wheel
encoders. The old GP6/GP16 digital steering path and GPIO barcode sampler
are inactive.

Each steering channel discards one conversion and averages two. Black is
>=455 at centre (GP27, from bench readings) and >=1420 at right (GP26) on
raw 0..4095. Recheck the centre cutoff on the track. Failed, short, out-of-range
or >100 ms ADC sampling is invalid. An eight-byte protected snapshot stores
timestamp:32, two raw values:12 each, validity:1 and reserved:7. Readers
derive pattern and check source age. GP28 is sampled by the standalone
diagnostic for calibration. The integrated steering loop reads GP27 and GP26.

The controller uses the teammate's RPM API/PID without
changing its gains, polarity, calibration or encoder driver:

Both wheels normally target 26 RPM on white and when both sensors detect a junction.
Steering scales the raw GP27 and GP26 readings separately, using the ranges
250–750 and 450–2650 respectively. The difference reduces the wheel target
on the tape side. The weak-edge response applies up to 60% contrast. Above
that, the correction increases to 14 RPM at full contrast for a tighter arc.
The correction follows the current sample and fades as it returns to white.
When both sensors return to their white baseline, both wheels target 26 RPM
even after a sustained bend.

Missing/invalid/stale sensor or motor feedback requests zero. A known S2
fault is reported as Controller fault. The controller checks its latched
STOP before steering, repeatedly requests zero and never clears the inhibit
because of new line input or reconnection. The S2 task alone writes PWM;
its watchdog and faults remain active. Command acceptance does not establish
physical wheel standstill.

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
