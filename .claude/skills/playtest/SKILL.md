---
name: playtest
description: Run and drive a desktop Megamod build like a player -- headless or windowed -- through its control channel, event log and report, to check a change actually plays. Use to reproduce a bug, confirm a fix, measure frame cost, or write a scripted playtest, without the phone.
---

# Playtest a desktop build

The harness is `src/app/agent.{h,c}` (docs/DESKTOP_AGENT.md). Today it is in
**`megamod-sandbox`** (the engine's physics, props, gibs, weather and video
presets in a generated arena, no game data). The full game joins it at
DESKTOP_AGENT.md step 5; the commands stay the same.

## Run
```sh
cmake --build build-host --target megamod-sandbox
build-host/megamod-sandbox --headless --size 640x360 --preset low \
    --control 0 --events scratch/ev.jsonl --report scratch/report.json
# prints: sandbox: control on 127.0.0.1:<port>
```
- `--headless` renders offscreen (lavapipe is fine); `--no-render` skips the
  GPU entirely (CI, or when only the simulation matters).
- With `--control`, headless starts **paused**: time moves only on `step`
  and `input`, 1/60 s per frame. Deterministic on one machine.
- Without `--control`, headless runs the built-in demo for `--seconds`.
- Windowed (no `--headless`) also takes `--control`, in real time: an agent
  can drive while the owner watches.

## Drive: JSON lines on 127.0.0.1
One request per line, one reply per line. `step` and `input` reply only once
their frames have run, so `step` then `state` sees the result.

| Request | Does |
|---|---|
| `{"cmd":"state"}` | position, yaw/pitch, on_ground, dummies, props / broken, debris, sprites, weather, preset |
| `{"cmd":"step","frames":60}` | advance N frames (headless) |
| `{"cmd":"input","forward":1,"right":0,"yaw":1.2,"pitch":0.1,"fire":true,"grenade":true,"jump":true,"frames":30}` | hold that input N frames; yaw/pitch are absolute; fire/grenade fire once |
| `{"cmd":"teleport","pos":[x,y,z]}` | move the player (world units, ~3 m each) |
| `{"cmd":"set","key":"preset","value":"ultra"}` | also `weather` (name), `gore` (0-2) |
| `{"cmd":"shot","path":"scratch/a.ppm"}` | screenshot (not with --no-render) |
| `{"cmd":"report"}` | frame cost p50-p99, hitches, totals (kills, breaks, blasts) |
| `{"cmd":"reset"}` / `pause` / `resume` / `quit` | |

Errors come back as `{"ok":false,"error":"..."}`; nothing crashes the game.

## Read
- **Event log** (`--events`), one object per line with `t`, `frame`, `ev`:
  `run_start`, `grenade`, `explosion`, `prop_broken`, `prop_exploded`,
  `kill` (dummy, by, gibbed), `hitch` (ms), `command`, `run_end`. Grep it first.
- **Report** (`--report`, or `{"cmd":"report"}`): `frame_cost` is wall time
  per simulated frame -- on lavapipe that is CPU cost, not the phone's.
- **Screenshots** are PPM; `python3 -c "from PIL import Image; Image.open('a.ppm').save('a.png')"`.

## Write a playtest
Copy `tests/playtest/sandbox_barrels.py`: launch, drive, `check(...)` what
state / report / events say. Register it in CMakeLists.txt next to
`playtest_sandbox` so ctest and CI run it. Assert on outcomes (a prop
broke, the player moved), not exact floats.

## The phone's content on the desktop
Set once per shell (this machine's folders; the content itself never goes in git):
```sh
export HTA_TRIAL_DIR=/home/commander/halo-trial-data/extract/maps HTA_BUNDLE_DIR=$HOME/assetlab-private/bundle
build-host/megamod-content                 # Trial maps, every bundled world, character, weapon
build-host/megamod-join <phone IP> --world ctf_2fort --auto 120   # join a phone hosting that map
build-host/megamod-match --world ctf_2fort --mode ctf --seconds 60 --cache scratch/navcache
                                           # a real match, bots only, no GPU: the phone's own load
```
`--world` takes the host's map by name (bloodgulch, ctf_2fort, ...), so the
map check matches the phone without paths. The phone drops a hosted match
when its app is backgrounded: start a retry loop before asking the owner
to host, and ask them not to switch apps until it has joined.

## Limits
Desktop frame cost says nothing about the phone's GPU; touch and the Java
HUD are phone-only. See "What the desktop will not tell you" in
docs/DESKTOP_AGENT.md.
