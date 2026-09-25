# Working in Megamod Showdown

Read `CLAUDE.md` (hard rules: public repository, no game data, author
Phase2, never push to Open Halo's `origin`) and `docs/HANDOFF.md` (the
briefing, the testing objective, every invented constant) before changing
anything. `docs/ENGINE_ARCHITECTURE.md` is the map of the engine.

The step-by-step procedures live as skills in `.claude/skills/` and apply
to any agent -- read the one that matches the task:

| Task | Procedure |
|---|---|
| Any code change, before commit/push/publish | `.claude/skills/verify/SKILL.md` |
| Merge or pull in another branch | `.claude/skills/merge-branch/SKILL.md` |
| Put a build on the owner's phone | `.claude/skills/publish/SKILL.md` |
| Convert or re-import a map | `.claude/skills/convert-map/SKILL.md` |
| Read what the phone measured (SEND REPORT, crashes) | `.claude/skills/read-report/SKILL.md` |
| Run / extend the dedicated server | `docs/DEDICATED_SERVER.md` |
| Desktop parity, headless runs, the agent harness (the plan) | `docs/DESKTOP_AGENT.md` |

Match state goes in `src/app/session.h` (`HTA_SESSION_FIELDS`), Android-only
state in `hta_android` after the union (see merge-branch for conflicts).

Report with numbers -- tests passed, commits pushed, what was skipped and
why. "Done" without evidence is not done: a past merge was reported that
never happened.
