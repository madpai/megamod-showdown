# Desktop parity and the agent harness

**Goal (owner, 2026-09-25):** a desktop build that plays the same game as
the Android build -- same maps, packages, modes, bots, hosting and joining
-- and that agents can **run, read, drive and debug** without the phone.
The phone stays the confirmation for what only it can show (touch, the
Java HUD, its GPU, its performance); everything else is found on the
desktop first.

This is the one plan for the loop extraction (docs/ENGINE_ARCHITECTURE.md
stages 2-6) and the dedicated server (docs/DEDICATED_SERVER.md S2-S3): they
are the same work. A headless server falls out of a desktop build that can
run without a window.

## What an agent must be able to do

| Need | Means | Command |
|---|---|---|
| **Run** it anywhere, with or without a display | offscreen Vulkan (lavapipe on CI and in the cloud), fixed timestep, seedable | `megamod --map bloodgulch --bots 6 --headless --seconds 120` |
| **Drive** it | a timed input script, and a live control channel | `--script play.txt`; `--control 127.0.0.1:27999` |
| **Read** it | the SEND REPORT JSON (same code as the phone), a JSON-lines event log, screenshots on demand | `--report out.json --events out.jsonl --shot-every 10` |
| **Connect** it | host or join over the real protocol: PC hosts and phones join, phones host and PCs join, two PCs | `--host`, `--join 100.x.y.z` |
| **Debug** it | ASan/UBSan build, crash record with symbols, input record and replay | `cmake -DHTA_SANITIZE=ON`; `--record r.bin` / `--replay r.bin` |

### The control channel (agent-facing)
JSON lines over a localhost TCP socket (off unless `--control` is given;
refuses anything but 127.0.0.1). One request per line, one reply per line:

```
{"cmd":"state"}                    -> map, mode, time, scores, me, units[{id,kind,team,pos,health,alive}]
{"cmd":"input","forward":1,"fire":true,"yaw":1.2,"frames":30}
{"cmd":"teleport","pos":[10,-4,1]}      {"cmd":"spawn_bot","team":1}
{"cmd":"set","key":"video.preset","value":"low"}
{"cmd":"shot","path":"scratch/a.ppm"}   {"cmd":"report"}   {"cmd":"step","frames":60}
{"cmd":"quit"}
```

`step` makes it deterministic: with `--paused`, time only moves when an
agent says so. Cheats (`teleport`, `spawn_bot`, `set`) exist only here,
never in the network protocol.

### The event log
One JSON object per line, as things happen: `match_start`, `spawn`,
`kill` (killer, victim, weapon, gibbed), `prop_broken`, `score`,
`net_join` / `net_leave` / `net_reject`, `hitch` (ms, what was running),
`warning`, `error`, `match_end`. The first thing to grep when a playtest
goes wrong.

### Scripted playtests in CI
`tests/playtest/*.txt`: short input scripts with assertions
(`expect units_alive >= 2`, `expect kills > 0 within 60s`,
`expect p99_ms < 20`). CI runs them headless on every push against a
generated arena (no game data). The local agent runs them against Blood
Gulch and the bundle's maps. That is the regression net the phone can
never be.

## Order of work

Each step keeps Android working and has one phone check before the next
(the owner tests every build). Desktop gains are checked on the desktop
by the agent that made them.

| # | Step | Phone check | Desktop gains |
|---|---|---|---|
| 0 | S1 + rate limiting (done in cloud, `5caa7e6`, `95e4933`) | hosted LAN match | -- |
| 1 | **Input in** (stage 2): `hta_session_frame(s, dt, const hta_input *)`; Android fills it from touch and the Java HUD | every control, driving, menus | the input struct scripts and the control channel will drive |
| 2 | **Files** (stage 4): `hta_fs` -- APK asset, app storage, desktop path; the Trial dir and bundle dir on desktop | built-in maps and a picked `.oalmap` | same content on both |
| 3 | **Loading without a GPU** (server S2): data half / presentation half of `load_map` and `start_game` | solo + hosted, Blood Gulch + one import | a headless match can load |
| 4 | **The loop out** (stage 5 / server S3): `src/app/session.c`, `me = -1` valid | full LAN match, both phones | the whole match runs off-phone |
| 5 | **`megamod` on desktop** (stage 6): `src/app/main_desktop.c` on `desktop_sdl`, a native menu, `--host` / `--join`, audio via SDL (stage 3) | a phone joins a PC host | **parity** |
| 6 | **Harness**: headless, `--script`, `--control`, `--events`, `--report`, `--record`/`--replay`, sanitizer build | -- | agents test locally and in CI |
| 7 | **Playtests in CI** and a `playtest` skill for agents | -- | every push plays a match |

**Status 2026-09-25: the harness exists, in `megamod-sandbox`.**
`src/app/agent.{h,c}` (event log, control channel, request parser;
`tests/test_agent.c`), wired into the sandbox with `--headless`,
`--no-render`, `--control`, `--events`, `--report`, `--seconds`; `step` and
`input` block until their frames have run. `tests/playtest/sandbox_barrels.py`
drives it like an agent (aim, grenade, walk; checks state, report and
events) and runs in ctest as `playtest_sandbox` with no GPU. The
`playtest` skill says how to use it. Not yet: `--script` files, record /
replay, `--netsim`, the sanitizer build, and the harness in `megamod-join`.

Steps 6-7 can start partly before 5: `megamod-sandbox` and `megamod-join`
already run headless (`--demo`, `--shot`) and can grow `--events`,
`--control` and `--report` first, so the harness is proven before the
full game lands in it.

Estimate: steps 1-5 are several focused sessions and five phone checks.
Freeze gameplay changes inside `platform_android.c` while steps 1-4 are
in flight (content and `src/game/` work are fine).

## What the desktop will not tell you

- **Phone GPU behaviour.** Mali/Adreno driver quirks, tile memory,
  thermal throttling. Desktop frame times say nothing about the phone's.
- **Touch and the Java HUD / menus.** Only the phone runs them.
- **Bit-exact replays across machines.** ARM and x86 float differences
  mean a replay recorded on a phone may drift on a PC; replays are for
  same-machine debugging.
- **Wi-Fi/cellular network conditions.** Localhost is perfect; a
  `--netsim loss=5,lag=80` option on the client socket is cheap and worth
  adding in step 6.
