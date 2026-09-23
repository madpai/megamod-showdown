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
