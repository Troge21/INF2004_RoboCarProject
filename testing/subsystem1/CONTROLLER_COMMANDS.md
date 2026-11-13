# Current controller / S1 command contract — Packed v4

S1 transports supervisory requests. The controller owns the persistent inhibit
and steering policy; S2 alone owns PWM/encoder/PID. No remote steering,
START or RESUME is implemented. The professor's mission remains autonomous.

## Wire and shared records

| Record | Layout |
| --- | --- |
| Controller word | state4, latch1, TEST1: six bits |
| Protected publication | boot uptime32 LE + control6/reserved2: five bytes |
| Pending request | ID32 + session32 + boot deadline32: twelve bytes |
| Pending owner reply | ID32 + session32 + control6/result3/reserved7: ten bytes |
| STATUS request | a0 + ID32: five bytes |
| STATUS reply | a1 + ID32 + boot32 + age11/control6/reserved7 + session32 + optional 46-byte frame: 16/62 bytes |
| STOP request | a2 + ID32 + session32 + boot deadline32: thirteen bytes |
| STOP result | a4 + ID32 + session32 + control6/result3/reserved7: eleven bytes |

One current decoder accepts these lengths. Earlier 15/26-byte STATUS and
ten-byte STOP packets are historical and rejected. Controller state is in
each main telemetry frame; the redundant six-byte beacon was removed.
All integers use explicit little endian operations, not native struct copies.

State codes 0..13 are Unavailable, Control disabled, Starting, Following line,
Recovering line, Waiting for line, Stop latched, Controller fault, Ready,
Turning left, Turning right, Scanning obstacle, Avoiding obstacle and Stopped.
Only actual owner publications select them. The sensor integration now
selects Scanning obstacle, Turning left/right and Stopped from fresh S5
intent; clear front uses unchanged line following. S5 never requests wheels.
See [sensor/controller integration](SENSOR_INTEGRATION.md). Latch may coexist only with
Disabled, Stop latched or Fault. Controller publications expire after 500 ms.

STOP result codes: 0 latched, 1 expired, 2 session changed, 3 replay,
4 busy, 5 controller/motor fault. A fault can never be successful STOP.
Requested IDs, session and deadline retain their full 32-bit ranges.

## Execution and freshness

The browser explicitly requests fresh STATUS before STOP. It uses a distinct
command ID and a two-second Pico-boot lease, allowing for handshake delay.
There is one pending request and no automatic actuator retry. A three-second
reply timeout reports unknown outcome; broker publish success alone is not
controller success. Disconnect/session change cancels pending requests.

The radio owner validates length/marker/fragments, rate limits and session.
It holds one bounded slot, routes STOP to the controller and publishes the
actual correlated result. Wrong sessions, expired/overlong leases, busy
slots and replays do not enter actuator execution. STATUS remains read-only.
Stock lwIP does not expose a retained bit to its incoming callback: the
firmware does not falsely claim retained-command rejection. Session/expiry/
idempotent STOP semantics provide the stated behavior; no remote resume exists.

The controller polls the latch during startup and before every line/obstacle steering
iteration, requests zero through S2, and only acknowledges accepted zero.
It preserves the inhibit on a fault. S2's existing command watchdog/PID
protections remain. A separate S2 stop-event flag was an earlier proposal,
not an additional S1 deliverable; the implemented controller inhibit is the
working handoff. Unknown vehicle state is never labelled Ready or fault-free.

The current stationary build has no motor task: success is a real logical
latch, not physical standstill. Its servo can still scan; the latch inhibits
wheels while sensing/telemetry continue. A normal build requires the raised-wheel
check. A Pico restart clears the latch and may resume normal movement.
The whole car has one USB/power-bank supply, not a separate motor battery.

Automatic STATUS also anchors the browser's Pico uptime/session checks.
Old buffered frames are rejected using this anchor with round-trip uncertainty.
Per-source age limits are enforced before serialization; the STATUS snapshot
does not replace the live stream or advance its sequence.

## Evidence

The preceding v3 normal image passed the user-observed physical stopping/
persistence check on 13 November, with independent STOP/result/IR captures.
Those records remain in [the test log](TEST_LOG.md). They do not certify this
new wire layout. Use [the combined final session](FINAL_ACCEPTANCE.md) after
the batch's software checks/builds. Actual stopping time, sensor accuracy and
final-image recovery/resource evidence still require physical observations.
