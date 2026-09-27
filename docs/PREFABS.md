# Prefabs (X6): reusable composed objects

**Status:** implemented 2026-09-27. This describes the code as it is. The
code: `src/asset/prefab.{h,c}` (the descriptor, local IDs, transforms),
`src/asset/package.c` (prefab libraries, references resolved from the
provider's view), `src/asset/world_def.c` (`prefab_instances`, expansion),
`src/engine/world_entities.c` (oriented collision), `src/game/
world_entities_gpu.c` (transformed drawing), `src/asset/resource_contract.c`
and `src/tools/resources.c` (introspection). Open Asset Lab:
`assetlab/prefabs.py`, `dependencies.py`, `world.py`, `fixtures.py`.

> How do several existing resources and world objects become one reusable
> authored object?

**A prefab is special during authoring and world construction, not during
gameplay.** A library describes the composition once; the engine validates
it once, when the package set loads; a world places instances; each
instance expands, at load, into the same ordinary placed entities a world
could have written by hand. After that nothing -- simulation, collision,
replication, Lua, round reset, handles -- knows a prefab existed. There is
no prefab runtime, no prefab network message, no prefab Lua, no prefab
collision.

```
library x6.facility: "prefabs" member      (parse, check: prefab.c)
  -> the package set loads                 (child model/sound/script resolved
                                            from x6.facility's view: package.c)
world x6.prefab_world: "prefab_instances"  (instance IDs, transforms: world_def.c)
  -> each instance's prefab resolved       (typed; the world must import it)
  -> expanded into ordinary hta_wdef       (IDs, links, transforms, a mover
     entities, links, mover definitions     definition each, scripts copied in)
  -> hta_world_defs_check                  (every world rule, again)
  -> normal runtime                        (world_entities.c, renderer, net, Lua)
```

Evidence labels: **TESTED** (a test asserts it), **OBSERVED** (seen in a
run), **INVENTED** (our number).

## Resource vs instance vs child

| | Identity | Grammar | Example |
|---|---|---|---|
| prefab | a RESOURCE a library provides | `namespace:prefab/name` (the X4 grammar) | `x6:prefab/security_door` |
| instance | a PLACEMENT in one world | a local ID | `north_door` |
| child | a part of a prefab | a local ID | `button` |
| expanded child | an ordinary placed entity | `namespace:entity/name` | `x6:entity/north_door__button` |

A **local ID** is `[a-z][a-z0-9]*(_[a-z0-9]+)*` -- lowercase segments joined
by single underscores, no `__`, no trailing `_` -- at most 23 bytes, never a
resource ID. A child of an instance becomes

```
<the world's namespace>:entity/<instance>__<child>
```

- **injective**: neither part may hold `__`, so `a__b` splits one way only;
- **bounded**: 23 + 2 + 23 = 48, the grammar's name limit;
- **collision-free by construction**: instances are unique in a world,
  children in a prefab, and a schema 5 world's own placed IDs may not hold
  `__` (refused: `x6:entity/north_door__door: '__' is reserved for prefab
  children (<instance>__<child>)`); the loader checks again anyway;
- **portable and deterministic**: text from canonical package bytes, not a
  pointer, hash or load order; the same on desktop and Android, host and
  joiner (**TESTED**: both log the same expansion; the world key agrees).

The resource grammar is untouched: `x6:entity/north_door/button` is still
malformed. The `/` path (`north_door/button`) appears only in
introspection output, never in a package or at run time.

A world **link** or a world **script** may name an expanded child by its
entity ID -- it is an ordinary placed ID (**TESTED**: the X6 world's
lockdown script does `world.entity('x6:entity/south_door__door')`). A
prefab's children can name only their siblings.

## The descriptor (a library's `prefabs` member, schema 1)

```json
"prefabs": {"prefabs": [{"children": [
    {"id": "button", "kind": "interactable", "links": [{"event": "used", "input": "toggle", "target": "door"}],
     "position": [-0.16, -0.7, 0.9], "reach": 1.2, "script": "x6:script/security_door_log"},
    {"id": "button_panel", "kind": "prop", "links": [], "model": "x6shared:model/button_panel", "position": [-0.13, -0.7, 0.9]},
    {"id": "door", "kind": "mover", "links": [], "model": "x6shared:model/door_panel", "move": [0.0, 1.3, 0.0],
     "position": [0.0, 0.0, 0.6], "size": [0.1, 1.2, 1.2], "sound": "x6shared:sound/door_hiss", "speed": 1.2},
    {"id": "frame_left", "kind": "prop", "links": [], "model": "x6shared:model/frame_post", "position": [0.0, -0.7, 0.7]},
    ...],
  "id": "x6:prefab/security_door"}], "schema": 1}
```

- **Children are the world's kinds**, with the world's parameter meanings,
  in PREFAB SPACE (+z up, the instance's origin at 0):

  | Kind | Takes | Needs |
  |---|---|---|
  | interactable | position, reach, script | position, reach |
  | relay | - | - |
  | mover | position, size, move, speed, yaw_degrees, sound, model | position, size, move, speed |
  | trigger | bounds | bounds |
  | teleport | position, yaw_degrees | position |
  | prop | position, model, yaw_degrees | position, model |

  Plus `id`, `kind`, `links` on every child. A mover carries its size,
  move and speed inline and becomes a mover definition of its own when
  expanded; its optional `model` draws it (the model's origin is the box's
  centre). Anything else is refused (`a prop does not take 'reach'`).
- **Canonical order**: prefabs by ID, children by local ID, each once --
  one byte form per prefab, and the expansion order. Refused otherwise
  (`contains duplicate local child id 'button'`, `children are not in
  canonical (byte) order of local id at 'a'`).
- **Links** are `{event, input, target}` with a sibling's local ID. The
  source must emit the event, the target accept the input; no self-link;
  no cycle (`prefab x6:prefab/a: link cycle: d1 -> d2 -> d1`), chains of at
  most 16. Checked iteratively (explicit stack), bounded by 16 children.
- **Numbers**: finite; child positions within 256 wu of the origin; yaw
  within 360 degrees; reach, size, move, speed within the world's limits.
  A non-finite number never parses (`1e999` is refused by the strict
  manifest reader).
- **No nesting** (schema 1): a child of kind `prefab`, or naming a prefab,
  is refused -- `prefab x6:prefab/security_door child 'frame_top' contains a
  nested prefab reference, which is not supported in prefab schema 1`.
- **No inheritance, no overrides**: `extends` is an unknown field; an
  instance is the prefab, an ID and a transform. Another configuration is
  another prefab.
- **Provenance** (creator, source package, Workshop item, conversion
  history) lives in the library's `provenance` member, keyed by prefab ID
  -- never in `prefabs`, never hashed, never consulted.

Limits (all **INVENTED**, sized for a phone): 32 prefabs per library, 16
children and 64 links per prefab, 8 links per child, 32 instances per
world, 64 entities and 64 mover definitions in the expanded world (the
world's own limits: `HTA_WDEF_MAX_ENTITIES`, which WORLD_STATE's codec
bounds (entity index < 64) under protocol v10), local IDs of 23 bytes, scale 0.25..4. Every count
is checked before anything is written past it
(`prefab instance n10 expands the world to 67 entities, exceeding limit 64`).

## References and visibility

Every child reference is a typed reference (docs/RESOURCES.md), resolved
through `hta_res_resolve` **once, when the package set loads, from the
PROVIDER's point of view**:

| Field | Expects | Resolves from |
|---|---|---|
| `prefabs.prefabs[].children[].model` | model | the prefab's library: its own, or what IT imports |
| `prefabs.prefabs[].children[].sound` | sound | the same |
| `prefabs.prefabs[].children[].script` | script (declaring `on_used`) | the same |
| `world_entities.prefab_instances[].prefab` | prefab | the world: its own (none) or what IT imports |

So, for `x6.prefab_world -> x6.facility -> x6.shared_assets`:

- **the provider** (`x6.facility`) sees its own resources and the art it
  imports from `x6.shared_assets`; its prefab can name nothing else
  (`...child 'frame_top': model x6shared:model/frame_top is provided by
  package x6.shared_assets, which package x6.facility requires but does not
  import it from`);
- **the consumer** (`x6.prefab_world`) imports only
  `x6:prefab/security_door`. It gains **no** visibility of
  `x6shared:model/door_panel`: a world field naming it is refused exactly
  as in X5 (not required, not imported);
- **transitively**, the package set still LOADS `x6.shared_assets` (X4's
  closure), its assets join the world's asset table, and its digest joins
  the world key. The expanded children hold the table indices the provider
  resolved -- a library boundary: the world links against the prefab, the
  prefab against its art.

The same rules and words are Open Asset Lab's (`link_prefabs`,
`expand_instances`); the engine checks everything again.

## Placing instances (world_entities schema 5)

```json
"world_entities": {"schema": 5, "entities": [...],
  "prefab_instances": [
    {"id": "north_door", "position": [0.0, 3.0, 0.0], "prefab": "x6:prefab/security_door"},
    {"id": "south_door", "position": [-3.0, -2.0, 0.0], "prefab": "x6:prefab/security_door", "yaw_degrees": -90.0}]}
```

`id`, `position`, `prefab` required; `yaw_degrees` (0) and `scale` (1)
optional; canonical ID order, each once; at most 32. The world must declare
a package (its namespace names the children). **Why schema 5**: an X5
engine must not load a world whose instances it would ignore; `world_entities`
is explicitly versioned, so a new list is a new schema (the X2 precedent),
and X5 refuses it ("unsupported schema"). OALMAP stays v3. Schemas 1-4 load
exactly as before (the parser's paths are untouched), and schema 5 also
lets a world's own props carry `yaw_degrees` and `scale`.

## Transforms

A placement transform is translation (wu), a rotation about +z
(`yaw_degrees`: counter-clockwise seen from above, +x turns toward +y;
right-handed, +z up, the engine's world) and a **uniform** scale.

```
world point  = pos + scale * Rz(yaw) * local point
child under instance I:
  position   = I.pos + I.scale * Rz(I.yaw) * child.position
  yaw        = I.yaw + child.yaw
  scale      = I.scale
  mover move = I.scale * Rz(I.yaw) * child.move    (its box also turns by child.yaw)
  reach, size, speed  scale with I.scale           (a door twice the size opens in the same time)
  trigger    = the world-axis box of the turned corners; I.yaw must be a multiple of 90
  teleport   = facing I.yaw + child.yaw
```

- `cos`/`sin` are **exact at every multiple of 90** (`hta_yaw_cossin`), so
  axis-aligned placements are bit-identical on every platform; other angles
  are computed in double from the canonical degrees. Compatibility never
  depends on these floats: the world key hashes package bytes (below).
- Limits: `|yaw| <= 360`; scale in `[0.25, 4]`; zero, negative, NaN,
  infinite or huge scales are refused (`prefab instance south_door: scale 0
  out of range (uniform, 0.25 to 4)`). A scale that pushes a child past a
  world limit is refused naming it (`...child 'button': reach 4.8 after
  scale 4 exceeds 4 wu`). **Non-uniform scale is not offered**: the
  renderer's lighting and the box collision would disagree.
- **Collision follows the picture.** A prop or prefab mover collides as its
  ORIENTED box -- the model's bounds, turned and scaled -- through the
  existing rotated collision instance (`hta_collision_instance.rot`, the
  path vehicles use), not the axis box around it. `min/max` keep the world
  axis box for anything coarse (bots' nav). **TESTED** (`test_prefab`: rays
  at a 45-degree, 2x prop hit its face, and miss beside its corner inside
  its axis box) and **OBSERVED** (`test_x6.sh`: the second world's 45-degree
  door stops a walker 0.26 wu in front of its centre along its facing =
  half its thickness + a body radius).
- **Drawing** uses the same matrix (`world_entities_gpu.c`); a scaled
  instance's light direction is renormalised so lighting is unchanged
  (unscaled instances bit for bit as before).
- Triggers are axis-aligned boxes in the engine, so a prefab holding one
  may be placed only at a multiple of 90 degrees (refused otherwise, naming
  the child). Movers slide linearly along their turned `move`.

## Expansion

`expand_prefabs` (`world_def.c`), after the world's own resources are known
and **before** any authored link resolves:

1. each instance, in canonical ID order: its prefab resolved (typed,
   imported) to the provider's compiled table;
2. counts checked (entities, links, mover definitions) before anything is
   written;
3. each child, in canonical local-ID order, appended to the SAME
   `hta_world_defs.entity[]` array: its entity ID (above), registered in the
   world's resource set (so world links and `world.entity` see it), links
   to `first + sibling index`, parameters transformed, a mover's own
   generated definition in `mover_def[]` (unnamed, `generated`), a script
   copied once into the world's script table (marked with its provider,
   like an import), props placed with their oriented box;
4. the world's own links resolve (they may name children), the usual
   resolvers skip generated entities, and `hta_world_defs_check` checks the
   whole world again.

Order is fixed by canonical bytes: the world's own entities, then
instances by ID, then children by local ID -- the same indices on every
peer (**TESTED**: north_door's children are entities 1..6, south_door's
7..12, on host and joiners). There is **no parallel prefab storage**: the
one thing kept is `prefab_instance[]` (ID, prefab, provider, first entity,
count, transform) for logs and `megamod-resources`; play never reads it.
The compiled prefab tables live in the package set, which is freed after
load: the prefab "disappears".

Prefab descriptors are parsed once per library load, never per instance or
per tick; nothing looks a name up during play. **Measured** (`test_prefab`,
desktop x86-64): a world of 32 instances (64 entities, the most a world
holds) loads, expands and builds collision in about 8 ms.

## Runtime: ordinary entities

- **Handles**: expanded children are slots in the same arrays with the same
  slot + generation handles; stale-handle and wrong-kind rejection,
  round-reset invalidation all apply unchanged (**TESTED**: north's and
  south's door handles differ; a reset stales both and closes both doors).
- **Collision**: the same prop and mover instances as hand-placed ones.
- **Replication**: WORLD_STATE carries each mover's phase and progress by
  entity index, as since X1 -- **protocol v10, no new message**; peers
  already hold identical content (the world key), so they expanded the same
  indices. Late join is state, not history.
- **Round reset**: `hta_went_reset` as before -- doors close, generations
  move on; no prefab-specific path.
- **Sounds** (X5): a prefab mover's sound cues exactly as a world mover's.

## Lua: nothing new

No new callback, no Lua API, no prefab environment, no spawning. A prefab
button's script is an ordinary megamod.v1 script (the provider's own, or
one it imports), copied into the world's table once however many
instances use it; its `on_used(entity, player)` receives the INSTANTIATED
button -- `x6:entity/north_door__button` or `...south_door__button`
(**OBSERVED**, host log). The button's own link (`used -> door.toggle`,
resolved per instance to its sibling) moves its door. One script, one
environment: a counter in it is shared by every instance (X3's rule: one
environment per script). World scripts address a child by its placed ID.
Joiners never run Lua (**TESTED**).

## Compatibility identity

- The world's `world_entities` bytes -- instance IDs, prefab references,
  transforms -- are already a played member: an instance's ID is gameplay
  (links and scripts may name its children), so renaming one is a new key
  (**TESTED**).
- A library's digest covers its `prefabs` member (added to the library's
  played members beside `assets`, `package`, `scripts`); the world key
  covers every library of the closure in package-ID order -- so a changed
  child, link, number or reference in `x6.facility`, and one texel of
  `x6.shared_assets` that the world never imported, are each a new world
  (**TESTED**: unit, OAL, and refused before spawn in `test_x6.sh`).
- A provenance-only change is admitted (**TESTED**).
- A library without `prefabs` digests exactly as before, so no X1-X5 key
  moved (x1 `552c1757`, x2 `73bd2d8b`, x3 `512a1fc3`, x4 `46bee75f`, x5
  `1b067045`/`6748f47e`); `HTA_WORLD_KEY_SCHEMA` stays 1. X6 keys:
  `x6_prefab_world` `55b83b8b`, `x6_second_world` `f99b737d` -- engine ==
  Open Asset Lab == the Android emulator (below).
- Expanded runtime memory is never hashed.

## Older engines

Measured with the real X5 binary (`scripts/test_cross_version.sh`, commit
`1b5a43e`):

| Engine | Package | Result |
|---|---|---|
| X5 | X6 prefab world | refused at load: `requires x6.facility resources 'x6:prefab/security_door': resource type 'prefab' is reserved, not loadable by this engine` |
| X5 | X6 prefab library | refused: `package x6.facility: provides 'x6:prefab/security_door': resource type 'prefab' is reserved` |
| X5 | schema 5 world without a prefab (a turned prop) | refused at load: `unsupported schema` |
| X5 and X6 | X5 worlds | the same keys (`1b067045`); an X5 joiner is admitted by an X6 host |

No older engine can ignore a prefab and play a semantically incomplete
world: the prefab type was reserved from X4 on precisely so it would refuse.

## Introspection

- `megamod-resources --json`: `prefab` supported and importable; the four
  new reference fields; a `prefabs` section (schema, per-kind fields, local
  ID grammar, the child-entity form, order, visibility, nesting (none),
  inheritance (none), transform rules, limits); `world_entities` schema 5
  with `prefab_instances`; `prefabs` in the library's key members.
- `--markdown`: the type and reference tables (docs/RESOURCES.md).
- `--conformance`: `local_ids` (a corpus of child/instance IDs with the
  engine's verdict and words) and prefab resource IDs.
- `--bundle DIR --world NAME`: `prefab_instances` -- each instance, its
  prefab and provider, transform, and every child's path, entity ID, kind,
  index and position -- and in `resolved` the instance -> prefab reference
  and every child's model/sound/script with its provider package.
- Host and joiner log `[world] prefab instance north_door:
  x6:prefab/security_door from x6.facility -> entities 1..6 (...)`.

## The X6 proof

`scripts/test_x6.sh` (in `verify.sh`) with Open Asset Lab's
`x6.shared_assets` (art), `x6.facility` (the prefab and its script) and the
worlds `x6_prefab_world` (north_door untransformed in the x = 0 wall,
south_door turned -90 in the y = -2 wall, a lockdown button) and
`x6_second_world` (one freestanding gate, 45 degrees, 1.25x):

1. OAL's contract and conformance copies are this build's.
2. Keys engine == OAL; the expansion and every provider-view reference in
   `megamod-resources`; both worlds share one `x6.facility` digest; neither
   world carries a prefab or asset byte; the world requires only the prefab.
3. 21 refusals, OAL's checker and the engine alike: unimported, absent
   provider, absent art library, missing, wrong type, malformed prefab,
   bad instance ID, zero and negative scale, duplicate instance, the
   entity limit, a child's missing / wrong-type / unimported model,
   duplicate child, missing sibling, nesting, unknown kind, provides, a
   link cycle.
4. Joiner A is blocked by north's door, presses north's button: north
   opens, **south stays shut**, A walks through; the host's prefab script
   receives north's own button. Late joiner B finds north open and south
   shut, is blocked by the turned south door, presses south's button (the
   script receives south's button), walks through, then presses the
   lockdown button: the world's script closes **south only**. Joiner C's
   picture shows the turned door; the second world's turned, scaled door
   blocks at its oriented face, slides along its own axis
   (offset -1.15, 1.15), and the joiner walks through. No joiner runs Lua.
5. A moved prefab child and one art texel are refused before spawn; a
   provenance-only change is admitted; a joiner without `x6.facility` says
   what it lacks.

Unit: `tests/test_prefab.c` (ctest `prefab`) -- expansion, isolation,
late join, reset, turned and scaled collision, 55 refusal cases, the world key,
limits and the stress world, 4000 mutated prefab libraries (each also
loaded under a world) and 3000 mutated worlds (loaded and stepped), 20000
random local IDs, each twice with the same verdict and printable words, and
200 load/free cycles with failures mid-expansion (ASan/UBSan/LSan in
build-asan). Open Asset Lab: `tests/test_prefabs.py`.

## Emulator evidence (Android 14 x86_64)

**OBSERVED**, 2026-09-27, with the uncommitted X6 build: the emulator APK
carried `assets/maps/x6_prefab_world.oalmap`, `x6_second_world.oalmap` and
`assets/packages/x6.facility.oalasset`, `x6.shared_assets.oalasset` (bytes
identical to a fresh Open Asset Lab build). Hosting `x6_prefab_world`
(Create Game, 0 bots) it logged `[world] 13 world entities, 2 links`,
`[world] prefab instance north_door: x6:prefab/security_door from
x6.facility -> entities 1..6`, `south_door ... -> entities 7..12`, `[world]
assets: 3 textures, 3 materials, 4 models, 1 sounds (20 KB), 8 props`,
`[script] 2 scripts loaded (megamod.v1, host only)`, and drew north_door
(untransformed) and the edge of the turned south_door from the phone
player's start. Desktop joiners through `scripts/emu/udprelay.py`, with the
routes of `test_x6.sh`: A was admitted (map check `b258b7a9` on both sides:
the phone's key equals the desktop's), blocked at (-0.25, 3.00) by north's
door, pressed north's button -- the phone's Lua logged `security door
button entity x6:entity/north_door__button use 1`, north opened, south
stayed shut, A walked through. Late joiner B found north open and south
shut, was blocked at (-3.00, -1.75) by the turned door, pressed south's
button (the phone's Lua: `...south_door__button use 2`), walked through,
pressed the lockdown button (`lockdown toggles entity
x6:entity/south_door__door which was open`); B ended north open, south
shut. The phone then showed north's doorway open. The changed-prefab and
changed-texel joiners were refused (`not the host's map`), the
provenance-only one admitted, and a joiner without `x6.facility` refused
to load the world. No joiner logged a script line.

## Not in X6

- Nested prefabs, inheritance, variants, per-instance overrides.
- Runtime spawning (`world.spawn_prefab`), prefab lifecycle callbacks or any
  new Lua; per-instance script state (one environment per script).
- Non-uniform scale; rotation about any axis but +z; triggers at angles
  that are not multiples of 90; world-authored movers turning (their
  triangles are world geometry -- only prefab movers, drawn by a model,
  turn).
- More than 64 entities in a world (WORLD_STATE's index; widening it is a
  protocol change, v11 is reserved).
- Worlds providing prefabs (libraries do, as with X5 assets).
- GPU texture sharing: each model's GPU mesh uploads its slot's texture
  (the X6 world: 4 models, 4 texture uploads of 1 KB; the CPU copy is
  single). Not changed here.
- A visual prefab editor (Open Asset Lab has the data model and CLI only).
