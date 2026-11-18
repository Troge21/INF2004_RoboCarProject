# Current controller / S1 command contract - Packed v4

S1 transports supervisory requests; the controller owns the STOP inhibit and
steering policy; S2 alone owns PWM/encoders/PID. **Resume** explicitly releases
a latched STOP and returns control to the existing autonomous policy. It does
not select a direction, reset run data or clear motor faults. Normal startup
is still autonomous after three seconds. No new startup gate was added.

## Wire and shared records

| Record | Layout |
| --- | --- |
| Published controller word | state4, latch1, TEST1, reserved2: one byte |
| Protected publication | boot uptime32 LE + published word: five bytes |
| Pending controller request | ID32 + session32 + boot deadline32 + action1/result3/spare4: thirteen bytes |
| Pending owner reply | ID32 + session32 + control6/result3/Resume-kind1/spare6: ten bytes |
| Resume authorization | ticket32 + expiry32: eight bytes; zero ticket means absent |
| STATUS request | a0 + ID32: five bytes |
| STATUS reply | a1 + ID32 + boot32 + age11/control6/reserved7 + session32 + optional 46-byte frame: 16/62 bytes |
| STOP request | a2 + ID32 + session32 + boot deadline32: thirteen bytes |
| STOP result | a4 + ID32 + session32 + control6/result3/reserved7: eleven bytes |
| Resume arm request | a5 + client ID32 + session32 + boot deadline32: thirteen bytes |
| Resume authorization reply | a6 + the existing 15 STATUS header bytes + ticket32 + optional 46-byte frame: 20/66 bytes total |
| Resume commit | a7 + server ticket32 + session32 + boot deadline32: thirteen bytes |
| Resume result | a8 + ID32 + session32 + control6/result3/reserved7: eleven bytes |

For a6, offsets 1..15 are identical to a1, ticket occupies bytes 16..19,
and an optional frame starts at byte 20. An arm rejection is a8 with the
client arm ID; a commit result is a8 with the server ticket as its ID.
All integers use explicit little endian operations, not native struct copies.
STATUS age 0..2000 includes the snapshot; age 2047 means header only. Main
telemetry remains 46 bytes, heartbeat five bytes, STATUS 16/62 and STOP 13/11.
The redundant controller beacon and obsolete layouts remain removed.

State codes 0..13 are Unavailable, Control disabled, Starting, Following line,
Recovering line, Waiting for line, Stop latched, Controller fault, Ready,
Turning left, Turning right, Scanning obstacle, Avoiding obstacle and Stopped.
Only actual owner publications select them. Latch may coexist only with
Disabled, Stop latched or Fault. Publications expire after 500 ms on the Pico;
the browser also requires current received data. The IMU state is separate.

STOP results: 0 latched, 1 expired, 2 session changed, 3 replay, 4 busy,
5 controller/motor fault. Resume results: 0 released, 1 expired, 2 session
changed, 3 replay, 4 busy, 5 fault, 6 not ready, 7 authorization changed.
Fault is never success. IDs, tickets, sessions and clocks retain 32 bits.

## Explicit Resume flow

1. With a fresh normal-mode Stop latched publication, the user clicks Resume
   in Overview or Communications. The browser sends a5 using its synchronized
   Pico clock and session. Resume is disabled in TEST=1.
2. The controller checks its recent latch/fault state and bounded slot, then
   issues a new one-use server ticket with a two-second expiry. Issuing a
   ticket does not release STOP. The a6 reply supplies current uptime/status.
3. Only that pending browser action accepts the matching nonretained a6 and
   submits a7 within the remaining lease. Delayed, unmatched or session-changed
   replies cannot start a new Resume flow. No command is automatically retried.
4. The controller consumes the ticket, then checks actual sources on its
   next iteration. Startup must be complete; line and navigation must be
   valid/fresh; S5 must report CLEAR. Encoder/speed feedback must be valid,
   fresh and fault-free, with STOP mode, inactive command, zero applied duties
   and zero measured RPM on both wheels. Otherwise the latch remains set.
5. If ready, it releases the inhibit and runs the existing line policy through
   the existing S2 API. The matching a8 confirms the controller handoff. A
   failed motor handoff restores the latch, including during publication-lock
   contention, and reports fault. Run distance/counts/IMU peaks are retained.

There is one packed pending controller request and one completion slot, using
the existing semaphore. The owner word packs two private Resume progress/
failure bits alongside state/latch/TEST; those bits are never transmitted.
The authorization uses an eight-byte record and a 32-bit ticket counter.
There are no new tasks, queues or semaphores. Network TX capacity is 66 bytes;
the incoming command slot remains 13 bytes with one packed metadata byte.
Normal linked static RAM is 84148 bytes, +24 from the preceding 84124-byte
normal build. These are linked sizes, not runtime peak RAM measurements.

## STOP priority, expiry and connection changes

STOP remains an explicit, correlated controller request. Normally it first
gets fresh STATUS. It can promote an already pending STATUS or use the valid
current session/clock while interrupting Resume, rather than waiting for a
STATUS cooldown. STOP is available even if the last displayed state is still
latched, including after an unknown Resume outcome.

The radio validates framing/session/deadline before admission. Arm and commit
are each limited to one per 250 ms; STOP bypasses this rate gate. A new STOP
can replace a pending Resume in the radio slot and controller slot. It
invalidates unused tickets. A queued STOP or already occupied completion
slot remains bounded; busy is reported instead of claiming execution.
The controller polls STOP again immediately before submitting wheel targets.
It preserves the inhibit on motor failure; S2 watchdog/PID protections remain.

All actuator leases are at most two seconds in Pico boot time. Wrong session,
expired requests and consumed/replaced tickets do not release the latch.
STOP, faults and a new radio session invalidate unused authorizations.
Radio loss cancels unused/queued Resume when its nonblocking controller lock
is available. It cannot undo a Resume already executed before the loss.
A three-second missing result reports **unknown outcome**; check live state
or issue STOP. Reconnect never automatically resends or releases anything.
A browser-only reconnect preserves the latch; a full Pico restart clears it.

Stock lwIP's incoming callback does not expose the retained flag. Firmware
therefore does not claim retained-command rejection. The browser rejects
retained replies; server-issued, short-lived, one-use tickets avoid treating
an old arm request alone as release. This is freshness/replay handling on the
trusted local bench broker, not client authentication. Use QoS 0, retain off.

## Hardware and evidence

The whole car has one USB/power-bank supply, not a separate motor battery.
Raise BOTH drive wheels before flashing or powering normal firmware, and
leave the servo free. Resume can immediately restart autonomous movement.
TEST=1 disables wheel PWM and rejects Resume; its servo can still scan.

The preceding normal image 9c0dbfd5 passed user-observed two-wheel STOP and
latch persistence through IR/scan changes and dashboard reconnect. It is
archived in [the rollback record](logs/2026-11-17-before-resume-backup.json).
Those older observations do not test Resume. On 18 November the user reported
that the requested Resume test works. Record the basic functional pass as
[user-observed evidence](logs/2026-11-18-resume-user-bench-observation.md),
without claiming independently captured timing or negative-case coverage.
The current image also passed offline checks and both ARM builds.
See [current fingerprint](logs/2026-11-17-resume-fingerprint.json),
[raised-wheel procedure](MOTOR_STOP_TEST.md) and [test log](TEST_LOG.md).
Longer normal telemetry gaps, final recovery, calibration and runtime stack/
heap peaks remain open; do not count host tests as physical acceptance.
