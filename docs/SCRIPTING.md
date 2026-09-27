# Host-side gameplay scripting (X3)

**Status:** implemented 2026-09-27 (X3). This is **a narrow host-side
gameplay scripting layer**, not a modding API: two callbacks, six
functions, one API version. It describes the code as it is.

> Lua asks MegaMod Engine to do things. MegaMod Engine decides how those
> things actually happen.

Evidence labels below: **TESTED** (a test in this repo checks it),
**OBSERVED** (seen in a run, not asserted by a test), **INVENTED** (our
number, not taken from anything), **INFERRED**.

## What runs where

| | |
|---|---|
| Runtime | Lua **5.4.9** (2026-08-10), MIT, vendored unmodified in `third_party/lua/` (SHA-256 checked against lua.org; `third_party/lua/README.md`). Chosen for portability: plain C, the same sources on Android and desktop, no JIT. |
| Built | only the core and the base, string, table and math libraries (`hta_lua` in CMakeLists.txt). `io`, `os`, `package`/`require`/native modules, `debug`, `coroutine`, `utf8` are **not compiled in**. |
| Where | **the host only** (a LAN host, a headless `megamod-match --host`, or an offline game). A joiner never creates a Lua state (`hta_match_begin` makes one only when the world is not `remote`); it plays the host's replicated results. **TESTED** (`scripts/test_x3.sh` checks no joiner logs a script line; `megamod-join` never links a call to `hta_script_create`). |
| How many | one Lua state per match; one environment table per script. |
| Code | `src/script/script.{h,c}`; the package side `src/asset/world_def.{h,c}`; the seam in `src/engine/world_entities.c` (a scripted use is recorded, never run there). |

## Scripts are content

A script enters the runtime only through an Open Asset Lab package: OALMAP
v3, `world_entities` **schema 3** (OAL `docs/RUNTIME_PACKAGE.md`).

```json
"world_entities": {"schema": 3,
  "scripts": [{"id": "x3:script/button_logic", "api": "megamod.v1",
               "callbacks": ["on_used"], "source": "local door = world.entity(...)..."}],
  "ability_script": "x3:script/pulse_ability",
  "entities": [{"id": "x3:entity/button_script", "kind": "interactable",
                "script": "x3:script/button_logic", "links": [], ...}, ...]}
```

- **Identity:** `namespace:script/name`, a registered content-ID type
  (`script`, added for X3), in the world's namespace, unique. The file a
  script came from is never its identity.
- **Source, not bytecode:** the package carries Lua text (ASCII, at most
  32 KB per script, 64 KB and 16 scripts per world); the loader refuses
  anything else (`luaL_loadbufferx(..., "t")`). Lua bytecode is not
  portable between our ARM64 and x86-64 builds' configurations and can
  crash a VM; compiling 1-2 KB of text at match start costs nothing.
- **References resolve once, at load:** an interactable's `script` and the
  world's `ability_script` become indices; a script's `world.entity(id)`
  runs only while the script loads and returns a handle. Nothing looks a
  name up during play. **TESTED.**
- **Versioned:** each script declares `"api": "megamod.v1"`; any other
  version is refused ("unsupported script API 'megamod.v2' (this engine
  has megamod.v1)"). Callbacks are declared, and a declared callback the
  script does not define refuses the script. **TESTED.**
- Older packages (schema 1/2, v1/v2 maps, Trial) never touch Lua.

## API megamod.v1

Printed by **`megamod-script-api --json`** from the same tables that
register the Lua functions (`VERBS`, `CALLBACKS`, the library lists in
`script.c`), so it cannot drift from the code. **TESTED** (`test_script`
checks its output).

### Callbacks

| Callback | Arguments | When |
|---|---|---|
| `on_used(entity, player)` | entity handle, player handle | a player used an interactable whose package names this script. Its authored links (if any) still fire; the script adds, it does not replace. |
| `on_ability(player)` | player handle | a player **with no native (character) ability** pressed ability, and the world names this script as `ability_script`. Characters with an ability keep theirs. One press per player per second (`HTA_SCRIPT_ABILITY_COOLDOWN`, **INVENTED**). |

### Functions

| Function | Does | Engine path |
|---|---|---|
| `world.entity(id)` | a placed entity's handle; **load time only** | `hta_world_defs_find` once |
| `world.send(entity, input [, actor])` | requests `activate`, `open`, `close`, `toggle` or `teleport`; returns whether it was queued | `hta_went_send` -> **the host's bounded world-event queue**, exactly as a link: handle re-checked at dispatch, queue and chain limits, `accepts` checked, diagnostics |
| `world.state(entity)` | a mover's phase (`closed`, `opening`, `open`, `closing`), nil for other kinds | read-only |
| `game.near(player, radius)` | other living players within radius (<= 16 wu), nearest first | read-only |
| `game.damage(target, amount [, by])` | requests damage (0 < amount <= 500) | `hta_game_hurt` -> **the native damage pipeline**: shields, health, spawn protection, teams, death, credit, kill feed, score |
| `log(...)` | one line in the host log, prefixed with the script ID (4 per callback) | `[script] ...` |

`megamod.api` is `"megamod.v1"`. There is nothing else: no timers, no
events, no spawning, no transforms, no health or mover fields, no network,
no cross-script calls.

### Handles

Lua holds entities and players only as opaque userdata the engine made
(`megamod.handle`): the engine's own **slot + generation** handles, not a
Lua-specific scheme -- `hta_went_handle` for world entities, and
`hta_unit.generation` (new in X3: moves on when a unit is added, removed
or a round starts) for players. Every call re-checks the handle:

- a number, table or the wrong kind of handle: `argument 1: expected an
  entity handle, got number`;
- a stale entity (the world reset since): `stale entity handle
  x3:entity/door_b (from an earlier round?)`;
- a stale player (round restart, or the player left and someone else took
  the slot): `stale player handle (slot 0: left, or an earlier round)`;
- a field write (`p.health = 0`): `attempt to index a megamod.handle value`.

Lua cannot build or alter one (no `setmetatable`, no `debug`). **TESTED**
(each case).

## When scripts run (the phase)

One host tick (`hta_session_tick`), in order:

1. host interaction: use presses at interactables (`world_interact`) --
   a scripted interactable **records** a call (`hta_went_call`);
2. `hta_game_update` (players, bots, projectiles, deaths, score); its
   events go out;
3. round restart, if due (`hta_went_reset`, then `hta_script_reset`);
4. world step (`world_entities`): trigger sensing, then
   **`host.world.script`**: every recorded `on_used`, then every queued
   `on_ability` -- then `hta_went_step` dispatches the queue (so a
   script's `open` moves the door **this tick**), movers move, teleports
   apply;
5. world effects;
6. network: joiners' packets in (their ability presses are queued for the
   **next** tick's script phase), snapshots and WORLD_STATE out.

`game.damage` changes vitals at once (the snapshot of this tick carries
it); the death is processed by the next tick's `hta_game_update`.

**Observable:** every callback logs its phase and the host tick:
`[script] x3:script/button_logic on_used(x3:entity/button_script, unit 1)
phase host.world.script tick 729: ok, 1 requests, under 1000
instructions`; `megamod-script-api --json` reports the phase and what it
is after and before. **OBSERVED** in `test_x3.sh` and asserted there.

## Sandbox

Each script's globals are its own table; reads fall through to one shared,
**read-only** base (a write to `math`, `world`... is an error). Available:

- base: `assert error ipairs next pairs select tonumber tostring type
  rawequal rawlen`;
- `string`: `byte char format len lower rep reverse sub upper` (also via
  `("x"):rep(3)`; the string metatable points at this subset);
- `table`: `concat insert move pack remove sort unpack`;
- `math`: all but `random`/`randomseed` (no uncontrolled randomness);
- `world`, `game`, `log`, `megamod`.

Removed from what is built: `print load loadstring dofile loadfile
require collectgarbage pcall xpcall getmetatable setmetatable rawget
rawset _G`, `string.dump/find/match/gmatch/gsub` (pattern matching runs in
C, outside the instruction budget), and all of `io os package debug
coroutine utf8`. No wall clock is exposed. **TESTED** (a script asserts
each absence at load).

`pcall` is absent on purpose: a script cannot catch its own budget error
and loop on.

## Limits

| Limit | Value | On excess |
|---|---|---|
| instructions per callback (and per script load) | 200 000 (checked every 1 000 by a count hook) | the callback is aborted: `exceeded instruction budget (200000)` |
| memory, the whole Lua state | 4 MB (its allocator) | allocation fails, the callback aborts: `out of memory (4 MB cap)` |
| engine requests per callback | 16 | `world.send: more than 16 engine requests in one callback`; the world queue's own 512/256/16-chain limits still apply below it |
| log lines per callback | 4 | dropped |
| errors per script per round | 8 | the script is disabled until the next round (logged) |
| scripted uses per tick | 16 (`HTA_WENT_MAX_CALLS`) | dropped, counted in the world diagnostics |
| ability presses per tick | 16 | ignored |

All values are **INVENTED** (ours), sized for a phone. A `while true do
end` is aborted after 200 000 instructions (a millisecond or so on a
desktop core: **INFERRED**, not measured); the match goes on.
**TESTED** (infinite loop, request flood, memory hog, `error("oops")`,
disable after 8, re-enable next round).

## Error policy

A syntax error, a failing chunk, a missing declared callback or a load
over budget: that match runs **without scripts** (logged `[script] scripts
refused, the world runs without them: <script>: <reason>`); the world
itself still loads. A runtime error or budget abort in a callback aborts
that callback only (engine requests it already made stay made -- each was
a validated, bounded request), logs script ID, callback, arguments and
reason, and the host continues. Eight in one round disable the script
until the next round.

## Lifecycle

- **Match begin (host):** one Lua state, every script's chunk run once in
  a fresh environment.
- **Round restart:** `hta_script_reset` runs every chunk again in a fresh
  environment (script state resets with the round); the world's and the
  game's own resets have already moved every generation, so a handle kept
  from the last round is stale. No state survives a round or the app.
- **Next match / tool exit:** `hta_script_destroy`; it returns the bytes
  the allocator still counted after `lua_close` -- 0 in every test and run.
  **TESTED** (25 load/run/reset/destroy cycles; ASan with leak detection
  on `test_script`).

## Networking and compatibility

Nothing new on the wire; protocol **v10** kept, v11 untouched. A script's
effects travel as the state they change: WORLD_STATE for movers, WORLD
for players, GAME for kills. Late joiners get current state, never script
history. **TESTED** (`test_x3.sh`: joiner B finds door A open).

Scripts live inside `world_entities`, which the world key already covers
byte for byte (docs/WORLD_ENTITIES.md "World compatibility"). So: same
scripts, same key; a one-character change, **a comment-only change**, a
different callback list or a different script on a button: a different
key, refused before spawn ("not the host's map"). Comments count because
the key hashes the packaged bytes (simple and exact; a comment is not
worth a Lua parser in the key). Provenance-only changes: same key,
admitted. **TESTED** (unit, OAL, and the real host/join path on desktop and
the emulator).

## The X3 proofs (`x3_script_lab`, OAL)

- **World:** `button_script` has **no links**; its `on_used` checks
  `world.state(door_a)` and asks `world.send(door_a, 'open', player)`.
  Without the script phase the press opens nothing (**TESTED**, unit); with
  it, the queue opens door A the same tick, doors B and C unchanged.
- **Gameplay:** `ability_script` `x3:script/pulse_ability`: `on_ability`
  takes `game.near(player, 2.5)` and asks `game.damage(t, 150, player)`
  for each. In `test_x3.sh` the target dies by the native pipeline ("Player
  1 was killed by Player 2"), sees its own death from the host, and the
  second pulse finds nobody alive.

## Emulator evidence (Android 14 x86_64, build 6be2ddb + HUD fix)

**OBSERVED**, 2026-09-27: the APK hosting `x3_script_lab` logs `[script] 2
scripts loaded (megamod.v1, host only); ability script on` (map check
b6ca93e1, the desktop's). Desktop joiners through `scripts/emu/udprelay.py`:
A found door A shut and blocked, pressed the scripted button (the phone's
Lua: `on_used ... phase host.world.script ... ok, 1 requests`), pressed
ability (`pulse by player 2 hit 1`, then the phone's own kill feed
`Player 2 was killed by Player 3`); target T saw its own death; late
joiner B found door A open, B and C shut; the one-character and
comment-only packages were refused ("different map"), the provenance-only
one admitted; no joiner logged a script line. The phone host's **own**
player pressed the purple button (door A opened) and its ABILITY button
(`on_ability ... hit 0`: nobody near).

## Limitations

- Two callbacks; interactables are the only scripted entities; no timers,
  delays, per-tick callbacks, coroutines or cross-script calls.
- `game.near` measures body positions only (no line of sight).
- Scripted abilities need no cooldown UI; the phone shows a generic
  ABILITY button when the world has one.
- A phone **joiner's** scripted ability goes through the existing CONTROL
  ability counter (the same path megamod-join's `Q` uses): **UNVERIFIED**
  on a phone joiner; the emulator was the host.
- A script that fails to load disables all of that world's scripts for the
  match (the world still plays).
- Not implemented and not planned for v1: foreign scripting (GMod/Source
  Lua), client-side scripts, save persistence, hot reload, a debugger.
