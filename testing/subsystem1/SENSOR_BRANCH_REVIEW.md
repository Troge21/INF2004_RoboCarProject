# Sensor branch review ? 13 November 2026

**Historical review.** The user subsequently authorized integration. The
adapted current implementation and checks are in [Sensor integration](SENSOR_INTEGRATION.md).
The unmodified branch findings below explain the changes required.


Fetched **origin/sensor**, commit **c8a789b075ba62b714f92b65a8c671cb3767e1cd**
(**Add servo obstacle avoidance**), without checking out or merging it.
Current main remains f37055f plus the existing local S1/integration changes.
The source files, corrected startup-stack fix and current 390c5893 UF2 are
hash-verified unchanged. No car operation, flash, commit, stash or push occurred.

## What the teammate added

Five changed files: app.h, app_main.c and S5 C/header/tests. The branch drives
the servo on existing GP12 / PWM slice 6, checks forward at 90 degrees, and
when distance is at most 250 mm samples left 150 / right 30 degrees.
It chooses a side, asks S2 for RPM targets and refreshes requests every 50 ms
during delays. No new sensor wiring assignments are present in this commit.

This can supply a useful three-angle profile for the existing S1 scan
transport/display. Our contract accepts 1..181 sorted points; a complete
three-point scan does not require another decoder version.

## Why it is not merged into the working car yet

| Issue | Direct evidence / required integration |
| --- | --- |
| Old S1 handoff | Branch S5 uses S1_TELEMETRY, s1_make_telemetry and s1_submit_telemetry. Actual ARM compile-only probes against current headers fail in TEST=0 and TEST=1. Keep the current passive range publication when integrating. |
| Conflicting steering ownership | Branch app.h sets APP_LINE_FOLLOW_ENABLED=0. S5 calls S2 directly instead of the current controller. Resolve who owns navigation before enabling this mode. |
| STOP would be bypassed | Branch S5 calls no app_controller interface and refreshes movement requests continuously. The current controller latch cannot reliably inhibit an independent S5 command producer. Route movement requests through the controller/inhibit arbitration. |
| Stationary behavior changes | New servo initialization/movement is not guarded by APP_TEST. The existing motor task may stay disabled, but the servo would move in the current stationary profile. Use an explicit agreed scan/avoidance test mode. |
| Scan telemetry still absent | The branch has local centre/left/right variables but no protected completed profile or telemetry_get_scan implementation. The weak S1 getter would still report unavailable. S5 can implement the prepared getter without changing the v4 format. |
| Three merge conflicts | A temporary three-way simulation finds conflicts in app.h, app_main.c and subsystem5.c. The h/test additions merge textually; that alone does not prove integrated behavior. |

The local explicit APP_MODE startup already fixes the earlier forced standalone
IR bench problem that the branch also tries to address. Do not reintroduce a
second bench-selection mechanism.

## S5 policy issue for its owner

An isolated execution of the branch's actual s5_choose_motion returns LEFT
for centre=100, left=240, right=260 mm (all valid). With its 250 mm threshold,
left is blocked and right is clear, but the 30 mm left preference wins.
The same happens at left=250/right=251. Record this edge case for the owner;
no S5 algorithm or threshold was silently changed during this review.

## Recommended handoff

Retain the teammate's sensor/servo ownership and ask S5 to publish an immutable
completed profile using [the existing passive getter](PRODUCER_HANDOFF.md#scans).
Integrate motion intent through the shared controller so latched STOP remains
authoritative and line/avoidance modes have one owner. Keep current range
snapshots, integrated startup and the fixed on-board test stack.

This review downloads and evaluates the branch; it does not enable its driving
policy. The currently working firmware and decoder are unchanged.

Evidence: [merge simulation and preservation](logs/2026-11-13-sensor-branch-merge-review.json),
[actual policy execution](logs/2026-11-13-sensor-policy-review.txt),
[ARM current-S1 compatibility probe](logs/2026-11-13-sensor-current-s1-compile.txt).
The compile probe tests remote S5 against local v4 headers; it does not claim
the clean remote sensor branch fails against its own older S1 headers.
