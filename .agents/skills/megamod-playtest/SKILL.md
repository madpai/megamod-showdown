---
name: megamod-playtest
description: Prove Megamod Engine and Showdown behavior with repeatable desktop, headless, multiplayer and Android playtests and durable evidence. Use when confirming a feature, reproducing a bug or measuring an actual runtime cost.
---

# Prove a Megamod slice

Read the checkout's `AGENTS.md`, current `docs/HANDOFF.md`, and
`.claude/skills/playtest/SKILL.md`. Read `.claude/skills/verify/SKILL.md`
for the required code/release gate. Reuse those tools and their actual
commands rather than introducing another input protocol.

## Choose the evidence that answers the question

| Question | Existing evidence path |
|---|---|
| Do compiled original packages match the loader? | Asset Lab `project verify` with built `megamod-resources` |
| Does a physics/effects change behave correctly? | `megamod-sandbox` control channel and `tests/playtest/` |
| Does Night Shift's scenario and authority work? | `scripts/test_night_shift.sh`, `docs/night_shift/` |
| Does Racing work through a complete race and a peer? | `scripts/test_racing.sh`, `docs/RACING.md` |
| Does a shared gameplay change preserve actual content? | `megamod-match` with the private Trial/bundle inputs, required verify gate |
| Does it feel and perform well on the phone? | Owner's physical-device playtest and SEND REPORT, read-report skill |

Create a unique ignored/private run directory. Record source revisions,
content world key, executable/package hashes where available, commands,
seed, settings, environment and the specific outcome being tested. Keep
logs/events and machine reports with their screenshots. Restore a test
world/state before repeating destructive scenarios.

Use frame stepping for the paused headless sandbox; a command reply is
the synchronization point. Check an outcome such as movement, a break,
completion, ordered gates or late-join state. For multiplayer mechanics,
exercise a matching peer and incompatible-content refusal where relevant.
Never report a skipped precondition as a passed gameplay test.

For appearance questions, render through the actual Engine and inspect
the image. Record gameplay only when demonstration is useful or requested.
Window capture should run outside the simulation thread; stop recorders
without blocking it. Kill only the subprocess PID the test owns.

Report package agreement, simulation behavior, rendered appearance and
physical-device performance as separate evidence. Lavapipe CPU timings
and emulator FPS cannot establish S24+ performance or touch feel. Update
the current testing objective with the next phone action and failure
symptom; preserve unresolved checks.

The source lesson is repeatable runtime evidence; see
[Universal Modder study](../../../docs/research/UNIVERSAL_MODDER.md).

For donor-content claims, check that the session actually loads the imported
roster before starting; actor names and package presence do not prove donor
meshes. Exercise a matching host/peer fingerprint and inspect native actor
images. For Android presentation, inspect real screenshots of the HUD,
casting control and inventory categories, including font scaling, overflow,
lighting and first-person weapon orientation/optional attachments. A full
regression gate cannot establish these appearance requirements.
