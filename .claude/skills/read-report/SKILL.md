---
name: read-report
description: Read what the owner's phone measured -- a SEND REPORT or an automatic crash report from the Megamod app -- and turn it into findings (performance, crashes, audio, network, effects). Use whenever the owner says they sent a report, something crashed, or asks how the game is running on their device.
---

# Read a device report

The app's pause screen has SEND REPORT, and a run that crashed sends one
automatically the next time the app starts. Reports land on the sideload
server: `scratch/serve-megamod/reports/` (`latest.json` is the newest).

1. Summary and crash frames in one go:
   ```sh
   scripts/symbolize_report.py scratch/serve-megamod/reports/latest.json --symbols scratch/serve-megamod/symbols
   ```
   The symbols folder holds each published build's unstripped library
   (publish_apk.sh keeps it). If the report's build has none, say so: the
   crash offsets cannot be trusted against another build's library.
2. Then read the JSON itself for what the summary leaves out:
   - `native.frames.{session,last_minute,this_minute}`: p50/p95/p99, hitches.
     60 fps is 16.7 ms; a p99 far above p95 means stutter, not slowness.
     `native.frames.hitches` (builds after 13dfb40) says when each of the
     last 8 frames over 50 ms began, in seconds since the match started:
     all in the first second or two is loading (pipelines, uploads), not
     gameplay.
   - First, what was the phone doing? `native.net`: `hosting` false with an
     `id` means it had joined someone -- often the scripted `megamod-server`
     (test 13), a near-empty scene that is no performance baseline. A
     `player_pos` far below the map's ground while airborne means it fell
     out of the world.
   - `native.video`: preset, `settings` (the exact key=value in effect),
     composed or direct, render scale (dynamic resolution lowers it).
   - `device.thermal` / `power_save`: a throttling phone explains a slow run.
   - `native.audio`: `dropped` or many `stolen` voices mean the mixer is
     overloaded; `running` false means no sound at all.
   - `native.net`: `invalid`/`dropped` packets, ping, `reject_reason`
     (1 full, 2 wrong map package).
   - `native.effects`: debris/sprites against their caps, props broken,
     `collision_indexed` (the broad phase), nav blocked nodes.
   - `log`: the app's last ~600 log lines, including `[perf]` every 2 s,
     `[video]`, `[net]`, `[wfx]`, `[report]`.
3. Compare with an earlier report of the same build and preset when the
   question is "did it get worse".

Report findings with the numbers they rest on, and say what would confirm
a guess (another report after a change, a specific test on the phone).
