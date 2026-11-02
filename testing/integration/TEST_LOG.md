# Integration — test log

Tests that two or more subsystems work together. There is one test (or more) for every interface listed in TEAM.md section 5. Both owners agree on the test and sign off the result.

See `testing/README.md` for how to run tests and record results.

## Test cases

Define a test here before running it. Test IDs never change.

| ID | What is tested | Requirement / interface | Method | Steps / how | Expected result |
|---|---|---|---|---|---|
| INT-01 | samples flow from S2 to S3 (≥ 5 in 2 s) | IF-01, IF-02 | auto | `subsystem3_test.c` | `[TEST] INT-01 PASS` |
| INT-02 | summary lines flow from S3 to S4 (≥ 2 in 3.5 s) | IF-03 | auto | `subsystem4_test.c` | `[TEST] INT-02 PASS` |
| INT-03 | Live S5 distance reaches external MQTT subscriber | IF-13, IF-11 (temporary bench route) | manual | Build NET=lwip MQTT=1 TEST=1; run broker/subscriber per S1 guide; move target between measured distances | Received telemetry changes with target distance, sequence advances; no fake readings |
| INT-04 | Failed echo is published as invalid | IF-13, IF-11 | manual | Cause a no-echo/invalid-width measurement without changing powered wiring; compare ULTRA fault output and MQTT subscriber | JSON has obstacle_mm=null and obstacle_valid=false; no clear-space interpretation |
| INT-05 | Broker outage/recovery does not block sensing | IF-13, IF-11 | manual | Stop broker, move target, wait >2 s, restart broker and observe S5/serial/subscriber | Measurements continue; MQTT reconnects; fresh readings resume; queued samples older than 2 s are discarded; no retained telemetry |

*Method:* **auto** = `TEST_CHECK` in code, result printed on the console;
**manual** = a person observes the result (LED, scope, meter, phone app…).

## Results

Add a new row for every run. Never edit or delete old rows.

| Date | Test ID | Commit | Tester | Result | Log / evidence | Notes |
|---|---|---|---|---|---|---|
| YYYY-MM-DD | INT-01 | `abc1234` | | PASS / FAIL | `logs/YYYY-MM-DD_….txt` | |
