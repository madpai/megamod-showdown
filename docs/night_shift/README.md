# MegaMod: Night Shift

A 2-4 player co-op immersive-horror slice: a recovery crew restores power
in a silent research annex, pulls its data core, survives the lockdown and
rides the freight lift out. MegaMod's first production vertical slice
(2026-09-27), built from the X3-X7 platform as it is -- no engine change.

- Content source: Open Asset Lab `projects/night_shift/`
  (`assetlab project build projects/night_shift --output BUNDLE`)
- Test: `scripts/test_night_shift.sh` (in `verify.sh`)

| Doc | What |
|---|---|
| [GAME_FLOW](GAME_FLOW.md) | the authoritative design: areas, objectives, escalation, exit |
| [STATE_GRAPH](STATE_GRAPH.md) | every binding, condition and action; the Lua hook; real traces |
| [PACKAGE_GRAPH](PACKAGE_GRAPH.md) | packages, resources, dependencies, compatibility |
| [ENTITY_BUDGET](ENTITY_BUDGET.md) | 62 of 64, what was cut to fit |
| [PERFORMANCE](PERFORMANCE.md) | load, tick, cascade, Android |
| [PLAYTEST_NOTES](PLAYTEST_NOTES.md) | what worked, confused, bored, broke |
| [ENGINE_FRICTION](ENGINE_FRICTION.md) | engine limits hit, by severity |
| [OAL_FRICTION](OAL_FRICTION.md) | creator-tool friction |
| [AUTHORING_WORKFLOW](AUTHORING_WORKFLOW.md) | how it was built, command by command |
| [AI_READINESS](AI_READINESS.md) | the map as a future NPC test world |
| [FINDINGS](FINDINGS.md) | ranked pain and the recommended next milestone |
