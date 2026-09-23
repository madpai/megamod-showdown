# Running log — work in progress

A step-by-step log kept WHILE working, so another agent (Codex or Claude)
can pick up mid-task. Newest entry at the bottom. When a task is finished,
its story moves to `JOURNAL.md` and `HANDOFF.md`; this file then says so.

Read `CLAUDE.md` and `docs/HANDOFF.md` first. Standing rules: verify.sh
green before publishing; push to GitHub after every commit
(`git push origin fp-animated-guns:main`, after the Trial-data check);
release guest APK for each published build.

---

## 2026-09-23 ~10:15 — start: objective 2, "driving in a fight"

State: `main` = 2c7adc6, clean tree. verify.sh 75/75 on a9ebcec.
Baseline (`HTA_MAP=... scripts/drivebench.sh ffa`): 15.1% of wheel time
blocked (team 10.1%, ctf 9.5%).

Plan:
1. Measure where FFA blocking happens (fight vs path) with temporary
   instrumentation (not committed).
2. Likely fixes in `src/game/brain.c` `drive()`: in a fight, when the
   straight line is open, still keep off walls (`open_line` uses nearest
   wide nodes within 1 wu — a target standing by a rock makes the line
   "closed" and the path goes to open ground near him; OK). Chase targets
   who ran behind cover: currently straight → rock.
3. Re-run drivebench team/ctf/ffa, keep only changes that help.

## ~10:25 — measured FFA (8 seeds, temporary counters, not committed)

Blocked seconds per match by what the driver was doing, averages:
fight straight ~29, fight on a path ~54, path (roam/chase) ~16,
HOLD (tank/Ghost at range) ~42. So the fight modes dominate.
Suspect: the Ghost strafes sideways (`b->strafe`, drive() bottom, SCOUT
branch) with no look at what is beside it -> into rocks, both in hold and
while closing. Fix being tried: strafe only toward a side with open car
ground ~3 wu away (hta_nav_nearest_wide), else flip, else none.

## ~10:40 — Ghost strafes only toward open ground: FFA 15.1% -> 8.7%

`side_open()` + `STRAFE_LOOK` 3 wu in brain.c (SCOUT branch of drive()).
FFA 8 seeds: 8.7% blocked, kills 56.6 (was 54.5). By mode now: straight
26, fight-path 34, path 8, hold 24 s/match. Next: check team/ctf did not
regress, strip the temporary `dbg_mode` counters (brain.c before
`/* At the wheel. Ours:` and the `static float lastb[64]` line; htamatch
`extern double dbg_mode` line), run verify, commit, push.

## ~10:55 — committed: Ghost strafe toward open ground

team 10.1% -> 7.4% (kills 20.1 -> 23.8), ctf 9.5% (same), ffa 15.1% -> 8.7%.
verify.sh 75/75. Counters stripped. HANDOFF numbers/objective updated.
Not yet published as an APK (next step: publish + release v0.2.2).
