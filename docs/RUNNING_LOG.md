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

## ~11:05 — published 5d51b76, release v0.2.2 (guest APK, no maps)

HANDOFF testing objective item E added.

### Where to pick up (next agent)

Objective 2 ("driving in a fight") is PARTLY done. Remaining blocked time
per FFA match (8 seeds, `scripts/drivebench.sh ffa`, 8.7% total):
- ~34 s chasing an enemy along a path (`drive()` in brain.c: `to == fight`
  and `open_line` false -> `plan_drive` every time the target moves 3 wu).
  Idea: when the target is on foot and not reachable on open ground
  (behind a rock/in a base), stop chasing: hold at range and let the gun
  work, or roam; a Warthog without a gunner cannot hurt him there anyway.
- ~26 s going straight at a man across "open" ground: `open_line` checks
  only the nav line; the Warthog's run-over lead point (`tof` up to 1.5 s)
  can sit against a rock. Idea: pull `goal` back onto the nearest wide
  node (`hta_nav_nearest_wide`) before `open_line`.
- ~24 s tank/Ghost holding at range (`hold`): the tank squares its hull
  by turning in place into posts; the jam test now backs it out, but it
  keeps trying. Idea: in hold, skip the hull turn when `blocked` rose in
  the last second.
To measure by mode, re-add the temporary counters described at ~10:25
(accumulate `car->blocked` deltas per mode in drive(); print in htamatch).
Keep only changes that lower team/ctf/ffa together; verify.sh; commit;
push; publish; release.
