# World entities (X1): generic, source-independent world behaviour

**Status:** implemented 2026-09-26 (X1). This describes the code as it is.
The design question it answers is in
[research/WORLD_EVENT_SLICE_RECOMMENDATION.md](research/WORLD_EVENT_SLICE_RECOMMENDATION.md);
where the implementation differs from that proposal it says so below.

X1 proves one chain end to end:

```
original content (built in code by Open Asset Lab, no Source map)
  -> Open Asset Lab validator + the normal OALMAP packager
  -> OALMAP v3 with a "world_entities" manifest section
  -> MegaMod: parsed and checked at load, links resolved to checked handles
  -> the HOST runs a bounded event queue in the shared session tick
  -> resulting state (door position, player position) replicated to joiners
```

with `button -> relay -> door` and `trigger -> teleport`. Nothing in the
runtime knows a Source or Halo entity class.

## The model

| Layer | File | What it holds |
|---|---|---|
| Definitions (immutable) | `src/asset/world_def.{h,c}` | `hta_world_defs`: up to 64 entities, 256 links; kind, placed ID, parameters, links resolved to entity indices. Parsed from the manifest, or built in C by tests. |
| Instances (mutable) | `src/engine/world_entities.{h,c}` | `hta_world_entities`: per entity a generation, mover phase/progress, trigger occupancy, interactable cooldown; the event queue; movers' collision instances. Portable, no allocation, no platform. |
| Session glue | `src/app/session_tick.c`, `match_load.c`, `host_net.c` | loads at match begin; host interaction, trigger sensing, dispatch, teleports; WORLD_STATE out. |
| Renderer | `src/game/world_entities_gpu.{h,c}` | each mover's own triangles as a small mesh, drawn as a rigid instance at its offset; the world mesh skips them. |
| Clients | `src/game/net_view.c` (desktop), `platform_android.c` (phone) | apply WORLD_STATE; animate between snapshots. |

### Kinds, events, inputs (closed vocabulary)

| Kind | Emits | Accepts | Parameters |
|---|---|---|---|
| `interactable` | `used` | - | `position`, `reach` (wu, from the user's eye) |
| `relay` | `fired` | `activate` | - |
| `mover` | - | `open`, `close`, `toggle` | `bounds` (closed box), `move` (offset when open), `speed` (wu/s) |
| `trigger` | `entered` | - | `bounds` |
| `teleport` | - | `teleport` | `position`, `yaw_degrees` |

A link is `(event, target placed ID, input)`. Names are text in the package
(auditable JSON) and become small enums at load (`hta_wdef_event`,
`hta_wdef_input`); nothing compares strings during play. Every event carries
the **actor** who began its chain (the button's user, the trigger's
enterer; relays pass it on), which is who `teleport` moves.

No script, no property bag, no arbitrary event names, no delays.

### Placed IDs

Authored identity is the content-ID grammar `namespace:type/name` with a new
registered type **`entity`** (a placement, not a reusable definition),
e.g. `x1:entity/door_main`. Rules (both OAL and the runtime check them):
lowercase `[a-z][a-z0-9_]*` segments, namespace <= 40, name <= 48, whole
<= 96 bytes; every placed ID is in the world's own namespace
(`x1:world/event_lab` -> `x1:entity/...`); unique within the world.

Placed IDs are **load-time only**. They do not cross the network: the map
check already hashes the whole manifest, so two admitted peers built the
same definition table in the same order, and a dense entity index is a safe
wire name for the *current* world (`CONTENT_COMPATIBILITY.md`).

### Runtime handles

`hta_went_handle` = generation (high 16 bits) | slot (low 16). Generation 0
never occurs, so 0 means none. Links are resolved at load into handles;
every dequeued event resolves its handle again and a stale or invalid one is
dropped and counted (`dropped_stale`). `hta_went_reset` (a new round) moves
every generation on, so an event queued before a restart can never act on
the next round's entities. There is no entity creation/destruction in X1;
generations exist for the round boundary now and for spawning later.

### Event queue and limits

Host only. A FIFO ring processed in `hta_went_step`, called once per tick
in `hta_session_tick` after the game moved everyone and after trigger
sensing, before the snapshot goes out. Interaction presses are taken
*before* `hta_game_update` so a press at a button does not also board a
vehicle.

Links are zero-delay: a chain started this tick completes this tick
(`button -> relay -> door` is two dispatches in one step).

| Limit | Value | On excess |
|---|---|---|
| queue capacity | 512 (`HTA_WENT_QUEUE`) | event dropped, `dropped_full`++, logged with its source |
| dispatches per step | 256 (`HTA_WENT_BUDGET`) | the rest wait for the next step, in order (`deferred`) |
| chain depth | 16 links (`HTA_WDEF_MAX_CHAIN`) | dropped, `dropped_depth`++, "chain too long (a cycle?)" |
| links per entity / total / entities | 8 / 256 / 64 | refused at load (and by OAL) |
| teleports per actor per step | 1 | later requests ignored |
| interactable cooldown | 0.5 s (ours) | press consumed, nothing emitted |

Static cycles are refused by OAL and again by the loader, so the depth cap
only matters for corrupted data; `tests/test_world_entities.c` plants a
relay feeding itself after load and shows it ends after 17 dispatches.
Every drop sets `diag` (the session logs it as `[world] ...`).

*Differs from the research proposal:* that proposed treating queue overflow
as a match error that stops dispatch. The implementation drops the
overflowing event and says so; with OAL's per-root bound (a single event
can cause at most 256 more) overflow needs many simultaneous roots or bad
data, and halting every door in the match was judged worse than one
visible, counted drop.

### Interaction

The desktop joiner's E (and the phone's action/swap button) already sends
the CONTROL `action_count` counter used for vehicles. The host interprets it:
if an interactable is within its `reach` of the unit's eye and within 60
degrees of where it looks, the press is the interactable's; otherwise it is
a vehicle action as before. The client never names a target and never
states an outcome. Nearest wins. No line-of-sight test in X1.

### Movers and collision

A mover's collision is its `bounds` box, a grid built once in local space
and placed as an `hta_collision_instance` (the same path as vehicles and
breakable props) at `centre + move * t`. It is merged into the world grid's
instance list every tick, on the host and on every client, so players,
bullets and debris all collide with the door where it is. The visible
triangles (package groups flagged entity) are drawn with the same offset.
Movement is linear at `speed`; `open` on an open door and `close` on a
closed one do nothing. A mover does not push or stop on players (X1's door
only opens).

### Triggers and teleports

The host senses every live unit not in a vehicle each tick: feet + 0.3 wu
inside the box. Only the outside -> inside transition emits `entered`;
staying inside does not; leaving, dying or disconnecting re-arms. A
teleport moves the actor's body (and, for the host's own player, the
platform's body and camera) to the destination with zero velocity. OAL
refuses a destination inside a trigger.

## Networking (protocol v10 kept)

- **Client -> host:** nothing new (CONTROL's existing action counter).
- **Host -> client:** `HTA_NET_WORLD_STATE` (type 20), sent with the other
  snapshots (20 Hz) when the world has movers: count, then per mover
  `entity index, phase, t (u16)`; 1 + 4n bytes (5 for X1). Decoder:
  exact length, phase <= 3, indices strictly increasing, canonical
  re-encode; fuzzed with the others (`test_net_fuzz`). The client applies
  only indices that are movers in its own definitions.
- **Teleports:** nothing new; the unit's position is in WORLD and the
  joiner's reconciliation snaps past 0.5 wu. (Facing on arrival applies to
  the host's own player; a joiner keeps its own look direction.)
- **Late join:** state, not history. The first WORLD_STATE a client applies
  snaps every mover; later ones ease (a client animates between snapshots
  and takes the host's value at rest or when 0.1 apart).

**Why not v11.** v11 is reserved for join passwords. WORLD_STATE is a new
packet type, which a v10 build without it would count as invalid -- but no
such build can ever be in an X1 match: world entities arrive only in OALMAP
**v3**, which an older runtime refuses to load ("unsupported OALMAP package
version"), so it can neither host nor join such a world. On every world an
older build *can* load (v1/v2) nothing new is sent and CONTROL means what it
meant. So no two peers that can share a match disagree about the wire;
the handshake, the map check and the fingerprint are unchanged. A future
change that alters an existing packet, or sends something on v1/v2 worlds,
needs a real version bump.

**Compatibility.** The world check (`cache CRC ^ FNV(manifest)`) covers
every definition field (kinds, links, bounds, move, speed, destinations),
so a different X1 package is refused before spawn
(`scripts/test_x1.sh` step C). The door's *drawn* triangles are binary and
not in the manifest hash (the known v10 limitation); its collision comes
from the manifest `bounds`, so gameplay stays covered.

## Open Asset Lab side

`assetlab/world.py` (normalized original world, validator, compiler into
the shared packaging stage), `assetlab/fixtures.py` (`x1_event_lab`), CLI
`assetlab fixture x1_event_lab --output x1_event_lab.oalmap`. See OAL's
`docs/ORIGINAL_WORLDS.md` and `docs/RUNTIME_PACKAGE.md` (v3).

The X1 world: two rooms split by a wall with a 1.2 x 1.1 doorway closed by
a sliding door (orange, slides 1.25 wu into the wall at 1 wu/s), a red
button beside it, a green trigger pad in the east room, and a teleport
destination on a blue platform back in the west room. 156 triangles, 22 KB.

## Tests

| Test | Covers |
|---|---|
| `tests/test_world_entities.c` (ctest `world_entities`) | the C fixture (B-style, no package): manifest parsing incl. a decoy key and every truncation, each load-time refusal (duplicate/malformed ID, namespace, missing target, unknown kind/event/input, input not accepted, self-link, cycle, bad mover/trigger/destination, schema, unknown field, trailing bytes), placement reordering, button->relay->door in one step, door collision before/after, repeated and cooldown presses, trigger once-per-entry, re-arm, one teleport per actor, stale handles across a reset, runtime cycle/overflow/budget/bad input, client never dispatching, replication and late-join snap, garbage state rejected |
| `tests/test_external_map.c` | v3 loads entities; entity group rules; a broken link refuses the package; v2 ignores the section; v4 refused |
| `tests/test_net.c`, `test_net_fuzz.c` | WORLD_STATE codec, loopback, malformed input |
| `scripts/test_x1.sh` | OAL builds the world; `megamod-match --host` + `megamod-join --route`: closed door blocks, button opens it on the host, joiner walks through and is teleported once, late joiner sees it open and walks through, different package refused |
| OAL `tests/test_world.py` | fixture compile, determinism, reorder, ID audit, every validator diagnostic |

## Testing seams added

- `megamod-match --host PORT`: the shared session as a headless LAN host
  with no player, on the wall clock.
- `megamod-join --route "x,y;Lx,y;E;wS;Bx,y"`: scripted walking, looking,
  use, waits and expected-blocked walks; prints mover states and host moves.

## Known limitations

- Five kinds only; no delays, no outputs from movers (`opened`/`closed`),
  no enable/disable, no per-link values.
- Mover collision is its bounding box; movement is linear; no blocking or
  crushing; no rotation (hinges).
- Interaction has no line-of-sight check.
- A joiner's facing is not changed by a teleport (look is client input).
- The scripted route must pause briefly after `E` before turning: CONTROL
  carries the look direction at its 20 Hz send.
- Trigger occupancy covers 64 actors (unit slots 0..63); units are fewer.
- The door's rendered triangles are outside the manifest hash (v10 limit).
- No Source import of these kinds yet (below).

## Future Source translation (documentation only)

| Source | Generic |
|---|---|
| `func_button` | `interactable` (+ its brush as static or mover geometry); `OnPressed` -> `used` |
| `logic_relay` | `relay`; `Trigger` -> `activate`, `OnTrigger` -> `fired` |
| `func_door`, `func_movelinear` | `mover` (brush triangles tagged, `movedir`/`lip` -> `move`, `speed`); `Open`/`Close`/`Toggle` |
| `func_door_rotating`, `prop_door_rotating` | needs a rotating mover (not in X1) |
| `trigger_teleport` + `info_teleport_destination` | `trigger` with an `entered -> teleport` link to a `teleport` |
| `trigger_multiple`/`trigger_once` | `trigger` (once needs an enable/disable input) |

Source I/O's delays, parameters, `!activator` targets, wildcards and
`FireUser` would map only where a generic concept exists; the rest must be
reported as unsupported by the importer, never coerced. The importer would
generate placed IDs from `targetname` (canonicalized, collisions reported)
in the map's namespace.
