# World entities (X1, X2): generic, source-independent world behaviour

**Status:** X1 implemented 2026-09-26; X2 (reusable mover definitions and
the world key) the same day, [below](#x2-reusable-mover-definitions). This
describes the code as it is.
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
check covers the whole `world_entities` section (the world key, X2), so two admitted peers built the
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

**Compatibility.** The map check covers every definition field (kinds,
links, bounds, move, speed, destinations), so a different X1 package is
refused before spawn (`scripts/test_x1.sh` step C). Since X2 it also covers
the drawn triangles and every other played byte of the package: see
[World compatibility](#world-compatibility-x2).

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
| `tests/test_external_map.c` | v3 loads entities; entity group rules; a broken link refuses the package; v2 ignores the section; v4 refused; the world key's coverage (X2, below) |
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
- No Source import of these kinds yet (below).

## X2: reusable mover definitions

X1 put every mover's parameters on the placement. X2 gives movers one
shared, immutable **definition** that any number of placed movers name --
the first real second use of definition vs instance. It is **only for
movers**: there is no generic definition registry, no definitions for any
other kind, and nothing shared with weapons or characters (X1 found no
overlap worth abstracting).

### The boundary

| | Where | What |
|---|---|---|
| Definition (immutable) | `hta_wmover_def` in `hta_world_defs.mover_def[]` (`asset/world_def.h`) | `id` (`namespace:mover/name`), `size` (the box's extent), `move` (offset when open), `speed` |
| Placement (immutable) | `hta_wdef` | its placed ID, `pos` (the closed box's centre), `def` (the definition's index), its links |
| Instance (mutable) | `hta_went_state` + `inst[i]` (`engine/world_entities.h`) | phase, progress `t`, generation, its collision instance's current position |
| Built once per definition | `mover_coll[def]` | the definition's box grid; every placement's collision instance points at it |

The runtime holds the definitions through a `const` pointer and never
writes them (a test compares every byte after opening, closing, resetting
and replicating). Opening `door_a` moves only `door_a`'s instance, even
though `door_b` and `door_c` place the same grid.

### Identity and resolution

`x2:mover/basic_slide_door`: the content-ID grammar with a new registered
type **`mover`** (added deliberately; OAL `docs/CONTENT_IDS.md`), lowercase
segments, in the world's namespace, unique among the world's definitions.
At load (`hta_world_defs_parse`):

```
"mover_definitions": [{"id","size","move","speed"}]  -> mover_def[k]
placed mover {"definition": "<id>", "position": [...]} -> entity.def = k   (once)
```

Nothing looks a name up during play: a test scribbles every entity and
definition ID after loading and the button -> relay -> door chain still
runs. Refusals name the placement, the reference and the reason:

- `x2:entity/door_b references missing mover definition x2:mover/basic_slide_dor`
- `x2:entity/door_a: definition x2:entity/relay_a is a placed entity, expected a mover definition`
- `x2:entity/door_a: definition 'x2:weapon/basic_slide_door' is not a mover definition ID (namespace:mover/name)`
- `x2:entity/relay_a: link target x2:mover/basic_slide_door is a mover definition, expected a placed entity`
- `x2:mover/basic_slide_door: duplicate mover definition ID`, `'x2:mover/Basic-Door': malformed mover definition ID`
- `x2:entity/relay_b: only a mover takes a definition (it is a relay)`
- `x2:entity/door_b: a mover takes its size, move and speed from its definition (schema 2)`
- `x2:mover/basic_slide_door: mover speed out of range` (and size, move)

At most 64 definitions (`HTA_WDEF_MAX_MOVER_DEFS`); 64 entities as before.

### Package format: OALMAP v3 kept, `world_entities` schema 2

The addition fits v3's explicitly versioned section: `world_entities`
carries a `schema` number, which X1 runtimes already check. Schema 2 adds
`mover_definitions` and movers written as `definition` + `position`; a
schema 2 mover with inline `bounds`/`move`/`speed` is refused, and so is a
definition in schema 1. An X1 runtime refuses a schema 2 world
("unsupported schema" / unknown field) instead of misreading it, and the
binary layout is unchanged, so no OALMAP bump. **Schema 1 still loads**:
each inline mover becomes an unnamed definition of its own at parse time
(`size = max - min`, `pos` = the box's centre), so the runtime has one
path. OAL writes schema 1 byte-for-byte as before when a world has no
definitions (the `x1_event_lab` package is unchanged, sha256 b6654d0d...).

### The X2 world

OAL `x2_definition_lab` (`assetlab fixture x2_definition_lab`): a dividing
wall with three doorways at y = -3, 0, 3, each closed by a placement of
`x2:mover/basic_slide_door` (0.1 x 1.2 x 1.1, slides +1.25 y at 1 wu/s):

```
button_a -> relay_a -> door_a      button_b -> relay_b -> door_b
door_c: the same definition, nothing opens it
teleport_trigger -> teleport_destination
```

240 triangles, 30 KB. OAL draws each placement as one box of the
definition's material; the definition itself carries no mesh.

## World compatibility (X2)

Two peers may share a match only if their gameplay and spatial world
agrees. X1's map check hashed the manifest text: binary geometry could
differ unseen, and provenance could split identical worlds. X2 replaces the
package half of the map check with the **world key**
(`src/asset/external_map.c`, schema `HTA_WORLD_KEY_SCHEMA` 1), computed at
load from the package's own canonical bytes -- Open Asset Lab writes them
deterministically, so nothing is re-derived:

```
FNV-1a 64 over:
  "OALW", u32 key schema, u32 OALMAP version,
  u32 vertex, index, group and spawn counts,
  header world bounds (24 bytes),
  every vertex record (position, normal, uv, lightmap uv), as stored,
  every index, as stored,
  every material-group record (range, texture slot, flags incl. collision,
    alpha, breakable/entity owner, lightmap slot), as stored,
  every spawn record (position, facing), as stored,
  each PLAYED manifest member in manifest order:
    u32 key length, key, u32 value length, the value's exact bytes
PLAYED = spawn_points, flag_points, breakables, weather, world_entities
key (v10 map check) = low 32 bits XOR high 32 bits
```

**Covered:** geometry, collision (solid/non-solid, breakable and entity
ownership), world bounds, spawns and their teams, flags, breakables,
weather, every world entity (placement, kind, links, trigger volume,
teleport destination and facing, interactable reach) and every mover
definition. A manifest the reader cannot walk is hashed whole (stricter,
never looser).

**Excluded:** every other manifest member -- `source_provenance`,
`source_reference`, `source_sha256`, `importer_version`, `display_name`,
`id`/`namespace`, geometry statistics, `compatibility` reports,
`warnings`/`conversion_warnings`, dependency lists -- and texture pixels
(appearance only; an alpha *flag* change is in the group records). A
manifest member the runtime starts to play by must be added to `PLAYED`
here and in OAL's `assetlab/worldkey.py` together, with a schema bump.

**Two implementations, one value.** OAL's `assetlab/worldkey.py` is the
reference (`assetlab world-key PKG`; `compile_world` reports it);
`scripts/test_x2.sh` checks the engine's key equals OAL's, and they agree
on the imported maps too (de_dust2 `52fb3b08`, gm_construct `c7500401`,
de_aztec `8efd01d2`).

**Cost:** one pass over the geometry at load; all seven imported maps
(2fort's 1.24 M triangles included) load and key in 0.85 s total on the
desktop.

**Integrity vs compatibility.** The key answers "would these worlds play
the same", not "are these bytes intact": a package has no stored digest to
verify, and the loader's own structural checks remain the integrity guard.
Content-addressing or a stored integrity digest is left for later; the
64-bit digest (`hta_external_map.digest`) is there for it.

**Protocol v10 kept.** The map check field and its meaning ("both peers
stand in the same world") are unchanged; only how the value is computed
is stronger. An X1-era build and an X2 build compute different keys for
the same world and refuse each other ("not the host's map") -- the safe
direction; false compatibility would not be. v11 stays reserved.

**Width.** The wire carries 32 bits (a HELLO field); accidental collision
between two different worlds is ~2^-32 per pair. Widening needs a protocol
change and was not worth one here. Not a security measure: a peer can
report any value.

### X2 tests

| Test | Covers |
|---|---|
| `tests/test_world_entities.c` (x2 cases) | schema 2 parse and resolution (either key order); every refusal above; runtime check refuses a corrupted `def`; three doors share one grid; A opens, B and C stay shut, B opens independently, closing A leaves B; round reset closes all and stales handles; definitions byte-identical after play; IDs scribbled after load and the chain still runs; bounded queue; late joiner gets A open / B shut / C shut, then everyone converges; every truncation refused |
| `tests/test_external_map.c` (world key) | same bytes same key; vertex, index, group flag, spawn record, header bounds change it; texture pixels do not; manifest edits: provenance, importer version, warnings order, display name, source path do not; spawn team, placement, definition move/size/speed, link input, trigger volume, teleport position/yaw do |
| `tests/test_compat.c` | exact-float fingerprint: 1.2 vs 1.20001 and one ulp differ, -0 == 0 |
| `scripts/test_x2.sh` (in `verify.sh`) | OAL builds the X2 world; engine key == OAL key; host + joiner A (both shut on joining; B blocks; opens A; walks through A; teleported once) + late joiner B (finds A open, B shut, C shut; opens B; walks through) -> host, A and B all end A open, B open, C shut; a changed-definition package and a changed-geometry package **with a byte-identical manifest** are refused before spawn; a provenance-only package is admitted |
| OAL `tests/test_world.py` | X2 fixture (schema 2, determinism, groups), every definition diagnostic, X1 still schema 1; world key: same/rebuild equal, 14 gameplay edits change it, name/colour/provenance do not |
| Emulator (Android 14 x86_64, build a55dc33) | the APK hosts X2: renders the three doors; its own player opens door A with the action button; desktop joiners through `scripts/emu/udprelay.py` (now one upstream socket per joiner): A opens A, late B finds A open / B shut / C shut and opens B, both end A+B open, C shut; definition and geometry variants refused, provenance variant admitted |

### X2 limitations

- Definitions exist for movers only; placements cannot rotate (a
  definition's `move` is in world axes, so doors sharing one slide the same
  way).
- A definition carries no mesh: OAL draws each placement as a box of its
  material.
- Everything in X1's list still applies (linear movers, box collision, no
  pushing, no `opened` events, no line-of-sight for buttons).
- The world key is 32 bits on the wire.

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
