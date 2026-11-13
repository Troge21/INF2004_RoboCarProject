# Motor STOP validation

The current implementation is Packed v4. Use [the combined final hardware
checklist](FINAL_ACCEPTANCE.md#motion-and-stop-in-normal-firmware) and
[the current controller contract](CONTROLLER_COMMANDS.md). The single current
UF2 is stationary TEST=1 and cannot demonstrate moving motors stopping.

The car has one USB/power-bank supply and no separate battery switch. Before
flashing or powering normal TEST=0, securely raise both drive wheels.
Normal driving can start after three seconds; unplugging clears the latch
on the next boot. A browser reconnect does not clear it.

The preceding v3 normal image (701952 bytes, SHA-256
1a2687d1c3b642626fb8c81b355587f8327e6d32a82e08e6829ae0d02dd1fc08)
passed the user-observed 13 November trial: both wheels rotated, STOP stopped
both, and they stayed stopped through all IR changes and dashboard reconnect
with USB continuously connected. Its local source/image identities and
captures are preserved in [the observation summary](logs/2026-11-13-normal-motor-observation-summary.json)
and [the test history](TEST_LOG.md). These are historical passes, not a
physical pass for the new v4 image. No new hardware test ran in this batch.
