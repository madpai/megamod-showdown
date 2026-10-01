# Working in MegaMod Showdown and MegaMod Engine

**Product model:** Open Asset Lab compiles engine-ready content; MegaMod
Engine is the reusable native runtime; MegaMod Showdown is its official
reference game and aggressive stress test. Future original games are
separate engine consumers. **Showdown can be ridiculous. The engine must
remain clean.** Keep Showdown's roster, humor and compatibility cases out
of generic engine concepts. The two currently share this repository;
do not split or rename it solely to express this model.

Read `CLAUDE.md` (hard rules: public repository, no game data, author
Phase2, never push to an Open Halo remote) and `docs/HANDOFF.md` (the
briefing, the testing objective, every invented constant) before changing
anything. `docs/ENGINE_ARCHITECTURE.md` is the map of the engine.

The current checkout is `main` with `origin` pointing to
`https://github.com/madpai/megamod-showdown.git`; after inspecting the remote,
push with `git push origin main`. Historical `halo-sandbox`/`megamod` commands
in older skills and notes describe the previous checkout.

**Before major architectural work, read `docs/MEGAMOD_VISION.md`.** MegaMod
Engine is evolving from a Halo port into an independent content-driven runtime.
Halo is one compatibility layer; Source/GMod reaches the engine only as
packages from Open Asset Lab; Steam Workshop is one acquisition provider.
The runtime should understand MegaMod content; Asset Lab should understand
foreign content. For every new system ask (vision §20):

- Does it work for original MegaMod content? Could another importer use it?
- Is the runtime learning a generic capability, or one game's terminology?
  (`Door`, not `SourceDoor`; `WeaponDefinition`, not `HaloWeapon2`.)
- Could data or a script configure it later? Does it compose with
  unrelated systems? Can multiplayer replicate it cleanly?
- Can Asset Lab validate it before runtime? Does it keep provenance?
- Does it move MegaMod toward a platform rather than one hardcoded game?

And don't over-refactor (vision §21): a real product need, the minimum
change, tests, Android + Blood Gulch + imported-content checks, and write
down the coupling that remains. Seek a second use before generalizing.
The vision separates **[Now]**, **[Next]**
and **[Someday]**: never document or claim a [Next] capability as done.

Before proposing or implementing a major mechanic inspired by another
game or engine, also read `docs/GAMEPLAY_CAPABILITY_MODULES.md`,
`docs/research/README.md`, the relevant existing research reports, and
current production evidence such as `docs/night_shift/`. Search the
research corpus before starting new architectural research; build on what
it already established, recording **Fact / Inference / Unknown** and
license boundaries for new comparisons. **Learn the mechanic; do not
blindly copy the donor architecture.** Turn useful behavior into a generic
Engine capability that works with original content. This direction does
not authorize speculative rewrites or reorder the current roadmap.

The step-by-step procedures live as skills in `.claude/skills/` and apply
to any agent -- read the one that matches the task:

| Task | Procedure |
|---|---|
| Any architectural decision (new system, format, entity kind) | `docs/MEGAMOD_VISION.md` |
| Any code change, before commit/push/publish | `.claude/skills/verify/SKILL.md` |
| Merge or pull in another branch | `.claude/skills/merge-branch/SKILL.md` |
| Put a build on the owner's phone | `.claude/skills/publish/SKILL.md` |
| Convert or re-import a map | `.claude/skills/convert-map/SKILL.md` |
| Read what the phone measured (SEND REPORT, crashes) | `.claude/skills/read-report/SKILL.md` |
| Run / extend the dedicated server | `docs/DEDICATED_SERVER.md` |
| Desktop parity, headless runs, the agent harness (the plan) | `docs/DESKTOP_AGENT.md` |
| Drive a desktop build: control channel, events, report, playtests | `.claude/skills/playtest/SKILL.md` |
| Research or build a donor-inspired generic capability | `.agents/skills/megamod-capability/SKILL.md` |
| Choose and preserve runtime/playtest evidence | `.agents/skills/megamod-playtest/SKILL.md` |

These additional skills adapt Universal Modder's evidence workflow to our
existing tools; the study is `docs/research/UNIVERSAL_MODDER.md`. Content
preparation lives in Open Asset Lab's `.agents/skills/assetlab-content/`.

Match state goes in `src/app/session.h` (`HTA_SESSION_FIELDS`), Android-only
state in `hta_android` after the union (see merge-branch for conflicts).

Report with numbers -- tests passed, commits pushed, what was skipped and
why. "Done" without evidence is not done: a past merge was reported that
never happened.
