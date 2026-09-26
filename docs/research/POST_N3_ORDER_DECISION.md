# Post-N3 order: X1 and desktop gameplay

**Status:** reconciled after protocol v10 tests at MegaMod `ac1d010`. The emulator hosted the real APK, `megamod-join` joined and exercised mismatch/late-join paths, and the match runs through shared `hta_session_tick`. The earlier recommendation for a separate small Step 5 testing shell is superseded by this evidence.

| Order | Test convenience | Cost before engine architecture is proven |
| --- | --- | --- |
| Full Step 5 desktop client first | Best manual two-window UI/audio testing | Moves the remaining local-player presentation and menu work before X1's event/state contract; likely rework after X1. |
| **X1 first with current tools** | Emulator host, desktop joiner, headless/unit tests and late-join checks already work | Add only a bounded interaction input to `megamod-join` and the required CONTROL/state protocol fields during X1. |
| Separate desktop testing shell first | Could script more diagnostics | Its original acceptance criteria are largely met by v10 tooling; an extra shell phase would delay X1 without a demonstrated blocker. |

**Recommend X1 next.** Give `megamod-join` the smallest interact action needed to press the button, and expose enough host/client state in existing logs/tests to assert the door and teleport results. Use the real OAL-compiled original world for two-client and late-join tests, a C runtime fixture for fast isolated tests, and the Android emulator for the APK path. Keep event dispatch and authority in shared engine/session code. Full Step 5 desktop gameplay follows X1; a windowed desktop client is not an architectural prerequisite.

If X1 reveals a concrete untestable action or state transition, add that single testing seam then. Do not schedule a general desktop shell on speculation.
