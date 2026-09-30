# Resource identity, package dependencies and asset resources (X4, X5)

**X10 Racing extension (2026-09-29):** Original worlds may carry a bounded
`world_entities` schema 8 `racing` section. It identifies a typed vehicle
model, finite tune, ordered checkpoint planes and recovery anchors, start
grid and boost pads. OAL and the engine validate the same limits (eight
racers, 32 gates, 16 pads). The section and its dependencies affect the
compatibility world key; older schemas and fixture keys remain valid. See
[RACING.md](RACING.md) for the mode and host authority.

**Status:** X4 implemented 2026-09-27; **X5 (package-backed asset
resources) implemented 2026-09-27** -- see "Asset resources (X5)" below.
**X6 (prefabs) implemented 2026-09-27**: `prefab` is a supported,
importable type; a library's `prefabs` member composes resources and entity
kinds, and worlds place instances that expand into ordinary entities --
[PREFABS.md](PREFABS.md). This file's rules (grammar, typed references,
provides, the graph, the world key) are what prefabs build on.
This is **infrastructure, not a gameplay feature**: one grammar for content
IDs, one registry of resource types, typed references resolved once at
load, explicit package declarations with provides and requires, a checked
dependency graph, a world key that covers what a world depends on, and
(X5) textures, materials, models and sounds that live in library packages
behind those IDs. It describes the code as it is. The code:
`src/asset/resource.{h,c}` (grammar, registry, typed resolution),
`src/asset/package.{h,c}` (declarations, libraries, the graph, asset
linking), `src/asset/asset_res.{h,c}` (X5: asset descriptors, member paths,
payloads), `src/asset/resource_contract.c` (the contract, printed),
`src/asset/world_def.c` (the first consumer; props and mover sounds),
`src/asset/external_map.c` (loading, the world key, the asset table),
`src/engine/world_entities.c` (props' collision, mover sound cues),
`src/game/world_entities_gpu.c` (props drawn), `src/game/world_sounds.c`
(sound resources in the mixer), `src/app/content.c` (where required
packages come from), `src/tools/resources.c` (`megamod-resources`).

> Any piece of content should be able to identify, reference, depend on and
> validate another piece of content deterministically.

Evidence labels: **TESTED** (a test in this repository or Open Asset Lab's
asserts it), **OBSERVED** (seen in a run), **INVENTED** (our number).

## Two identities

| | Package | Resource |
|---|---|---|
| What | the unit that is built, shipped, required and loaded | a named item a package provides |
| ID | `x4.resource_lab` -- `segment(.segment)*` | `x4:script/open_door` -- `namespace:type/name` |
| Examples | `x4.shared`, `showdown.industrial_pack` | `x4:world/resource_lab`, `x4:mover/basic_slide_door`, `x4shared:script/pulse_ability` |
| Declares | provides, requires (its manifest's `package` member) | its type (in its ID) |
| Future | versions, signatures, caching, installation, acquisition | lookup, binding, typed references |

A package ID has no `:` or `/`, so the two can never be mistaken for each
other. A namespace is not a package: several packages may provide into one
namespace (`showdown`), and one package may provide into several; what may
not happen is two providers of the same resource (below). File names are
never identity: a library is *looked for* at `packages/<package id>.oalasset`
and must declare the ID it was looked for by.

## The resource ID grammar (version 1)

```
resource-id = namespace ":" type "/" name
segment     = [a-z] [a-z0-9_]*
namespace   = segment          at most 40 bytes
type        = segment          a registered type (below), at most 24 bytes
name        = segment          at most 48 bytes
whole                          at most 96 bytes
```

- **Lowercase ASCII only**, compared byte for byte. Capitals, hyphens,
  dots, spaces, tabs, non-ASCII bytes, escapes, an empty part, a second `:`
  or `/` (so no paths, no `..`) are **refused, never rewritten**. There is
  no normalisation and no case folding, so two IDs that differ at all are
  different resources on every filesystem and host, and every valid ID has
  exactly one spelling: the canonical serialisation is the ID itself.
- Unchanged from the grammar X1-X3 used (`x3:script/button_logic` is valid
  as it was); X4 made it one implementation with one set of messages.
- The type must be registered. A **reserved** type (`animation`,
  `ruleset`; `model`... until X5, `prefab` until X6) parses but nothing may
  provide or reference one yet ("resource type 'ruleset' is reserved, not
  loadable by this engine"), distinct from an
  **unknown** type ("unknown resource type 'widget'").
- Reserved namespaces, for built-in content: `halo_trial`, `megamod`. No
  package may provide into them.
- A package ID is `segment(.segment)*`, at most 64 bytes and 8 segments:
  `x4.resource_lab`. The same segment rules.

Every refusal says which rule: `namespace has capital 'X' (IDs are
lowercase; nothing is folded)`, `name has '-', not [a-z0-9_]`, `name has
'/' (':' and '/' each separate once)`, `no ':' (namespace:type/name)`,
`longer than 96 bytes`. The full corpus with the engine's verdicts is
`megamod-resources --conformance`.

## The type registry

One table in `src/asset/resource.c` (`TYPES`). The loader, the resolver,
`megamod-resources` and Open Asset Lab all read it; the table below is
printed by `megamod-resources --markdown` and `test_resource` fails if this
file differs from it.

<!-- megamod-resources --markdown: begin -->
| Type | Noun | Status | Scope | Resolved at load | Importable | Since | What |
|---|---|---|---|---|---|---|---|
| `world` | world | supported | package | - | - | N2 | a playable world: the content of an OALMAP package, listed in its provides |
| `entity` | placed entity | supported | placement | yes | - | X1 | a placement inside one world (button, relay, door, trigger, teleport); links and world.entity() name it |
| `mover` | mover definition | supported | definition | yes | - | X2 | a reusable mover (size, travel, speed) that placed movers name; inside its world only |
| `script` | script | supported | definition | yes | yes | X3 | host-side Lua source (megamod.v1); a world's own, or imported from a library package (X4) |
| `character` | character | supported | package | - | - | N2 | an imported character (.oalasset); still loaded by file name, its ID is audit-only |
| `weapon` | weapon | supported | package | - | - | N2 | an imported weapon (.oalasset); still loaded by file name, its ID is audit-only |
| `sounds` | sound pack | supported | package | - | - | N2 | the UI sound pack: a container of role-named clips (.oalasset kind sounds); loaded by file name, its ID is audit-only -- one addressable clip is a 'sound' |
| `model` | model | supported | definition | yes | yes | X5 | a static mesh (mesh1) drawn with its material slots; a library provides it, a world's props place it |
| `material` | material | supported | definition | yes | yes | X5 | a surface: one texture and a draw mode (opaque, alpha); a model's slots name it |
| `texture` | texture | supported | definition | yes | yes | X5 | an RGBA8 image in a library package; a material names it |
| `sound` | sound | supported | definition | yes | yes | X5 | one clip of 16-bit PCM in a library package; a mover definition may name it (not the UI 'sounds' pack) |
| `animation` | animation | reserved | definition | - | - | X4 | reserved: a clip for a skeleton |
| `prefab` | prefab | supported | definition | yes | yes | X6 | a reusable composition of entity kinds and resources; a library provides it, a world places instances that expand into ordinary entities at load |
| `ruleset` | ruleset | reserved | definition | - | - | X4 | reserved: game rules (team deathmatch...) |

| Reference field | Expects | Resolves to | Since |
|---|---|---|---|
| `world_entities.entities[].links[].target` | placed entity | the same package | X1 |
| `world_entities.entities[].definition` | mover definition | the same package | X2 |
| `world_entities.entities[].script` | script | the same package or a declared import | X3 |
| `world_entities.ability_script` | script | the same package or a declared import | X3 |
| `world.entity(id)` | placed entity | the same package | X3 |
| `package.requires[].resources[]` | any importable type | the required package it is listed under | X4 |
| `assets.materials[].texture` | texture | the same package or a declared import | X5 |
| `assets.models[].materials[]` | material | the same package or a declared import | X5 |
| `world_entities.entities[].model` | model | the same package or a declared import | X5 |
| `world_entities.mover_definitions[].sound` | sound | the same package or a declared import | X5 |
| `prefabs.prefabs[].children[].model` | model | the same package or a declared import | X6 |
| `prefabs.prefabs[].children[].sound` | sound | the same package or a declared import | X6 |
| `prefabs.prefabs[].children[].script` | script | the same package or a declared import | X6 |
| `world_entities.prefab_instances[].prefab` | prefab | the same package or a declared import | X6 |
| `world_entities.bindings[].source` | placed entity | the same package | X7 |
| `world_entities.bindings[].conditions[].entity` | placed entity | the same package | X7 |
| `world_entities.bindings[].actions[].target` | placed entity | the same package | X7 |
| `world_entities.bindings[].actions[].at` | placed entity | the same package | X7 |
| `world_entities.bindings[].actions[].sound` | sound | the same package or a declared import | X7 |
| `prefabs.prefabs[].bindings[].actions[].sound` | sound | the same package or a declared import | X7 |

| Event | Sources | Actor | X1 link name | When |
|---|---|---|---|---|
| `used` | interactable | always | used | a player pressed use at it (the host's interaction, before the game update); the actor is that player |
| `activated` | relay | the chain's | fired | it was told to activate (a link, a binding or Lua's world.send), when the queue dispatches that; the X1 link name is 'fired' |
| `entered` | trigger | always | entered | a player's body went from outside its box to inside (the host's trigger sensing); the actor is that player |
| `deactivated` | relay | the chain's | - | it was told to deactivate, when the queue dispatches that |
| `opened` | mover | - | - | its phase became open (it arrived, or was told to open while already at the end of its travel) |
| `closed` | mover | - | - | its phase became closed (it arrived, or was told to close while already at the start of its travel) |

| Condition | Reads | Values |
|---|---|---|
| `mover_state` | mover | `closed`, `opening`, `open`, `closing` |
| `relay_state` | relay | `inactive`, `active` |

| Action | Targets | Needs | Takes | Acts on |
|---|---|---|---|---|
| `open` | mover | target | target | the target |
| `close` | mover | target | target | the target |
| `toggle` | mover | target | target | the target |
| `activate` | relay | target | target | the target |
| `deactivate` | relay | target | target | the target |
| `teleport` | teleport | target | target | the event's actor |
| `damage` | - | amount | amount | the event's actor |
| `play_sound` | - | sound | sound, at | a place |
| `use` | interactable | target | target | the target |
<!-- megamod-resources --markdown: end -->

- **Scope.** *package*: identifies what a whole package is. *placement*: a
  thing placed in one world, never listed in provides, never taken from
  another package. *definition*: reusable content a package defines and
  lists.
- **Importable** (another package may require it): `script` (X4) and, since
  X5, `model`, `material`, `texture` and `sound`. Mover definitions stay
  inside their world (a library has no geometry to draw them with). A
  future type becomes importable by flipping the registry row and adding
  its runtime binding; the declaration, graph and key already handle any
  importable type -- X5 did exactly that.
- `character`, `weapon`, `sounds` IDs are recognised (Open Asset Lab's `ids`
  audit proposes them) but those packages are still loaded by file name
  and matched by the content fingerprint (CONTENT_COMPATIBILITY.md); they do
  not declare packages yet.
- **`sounds` is not `sound`.** `sounds` is the UI sound PACK: one
  `.oalasset` of kind `sounds` holding role-named clips (the hit ding, the
  kill ding), found by file name. `sound` (X5) is ONE addressable clip a
  library provides as a resource, `x5shared:sound/test_impact`, which a
  mover definition names. Both stay; the pack is legacy, the resource is
  the model new content uses.
- The type numbers (`hta_rtype`) are internal: never in a package, never on
  the wire.

## Typed references

Every field that names a resource is in the reference table above (the
`REFS` table in `resource.c`), with the type it expects and where it may
resolve. They all go through one function, `hta_res_resolve`, against the
world's `hta_res_set`:

```
packaged text  "script": "x4:script/open_door"
  -> parse      the grammar; malformed / unknown type / reserved type refused
  -> type       the ID's type must be the field's: a script field never takes a
                mover, even one called open_door
  -> lookup     exact bytes, in the set: the package's own resources, then what
                its dependencies provide
  -> visibility provided by this package, or by one it requires AND lists the ID
                under (its imports); a same-package field never leaves the package
  -> index      entity.script = 1 (the script table's index + 1)
```

That happens once, while the world loads (`hta_world_defs_parse_env`).
Gameplay holds the indices (and the X1-X3 generation-checked handles built
from them); nothing looks a name up during play. **TESTED** (X2's test
scribbles every ID after load and the world still runs; `test_resource`
resolves 512 references in a full set in about half a millisecond).

What each refusal says (the words are the engine's and Open Asset Lab's
alike):

| Case | Message |
|---|---|
| malformed | `x4:entity/button: script 'X4:script/open_door' is not a resource ID: namespace has capital 'X' (IDs are lowercase; nothing is folded) (expected namespace:script/name)` |
| wrong type, exists | `x4:entity/button_script: script x4:mover/basic_slide_door is a mover definition, expected a script` |
| wrong type, absent | `x2:entity/door_a: definition 'x2:weapon/basic_slide_door' is not a mover definition ID (namespace:mover/name)` |
| reserved / unknown type | `...: script 'x4:model/door': resource type 'model' is reserved, not loadable by this engine (expected a script)` |
| missing | `x4:entity/button_script references missing script x4:script/open_dor` |
| missing, same name elsewhere | `... references missing script x4:script/door (x4:mover/door is a mover definition)` -- a hint, never a substitution |
| not imported | `ability script x4shared:script/other is provided by package x4.shared, which package x4.resource_lab requires but does not import it from (list it in requires[].resources)` |
| not required | `... is provided by package x4.other, which package x4.resource_lab does not require` |
| same-package field | `... link target x4shared:entity/far is provided by package x4.shared; a link target must be in the same package` |
| duplicate provider | `x4:script/open_door: provided by both package x4.resource_lab and package x4.shared (duplicate providers are refused, never picked)` |
| script declares no callback | `x4:entity/button_script: script x4shared:script/pulse_ability does not declare on_used` |

`world.entity(id)` (a script, while it loads) says the same kind of thing:
`world.entity: 'x3:script/button_logic' is a script ID, expected a placed
entity (namespace:entity/name)`.

## Packages: provides and requires

A package declares itself in its manifest's `package` member (canonical JSON,
keys sorted as Open Asset Lab writes every manifest):

```json
"package": {
  "id": "x4.resource_lab",
  "provides": ["x4:mover/basic_slide_door", "x4:script/open_door", "x4:world/resource_lab"],
  "requires": [{"package": "x4.shared", "resources": ["x4shared:script/pulse_ability"]}],
  "schema": 1
}
```

- **provides**: every resource the package defines except placements --
  for a world, its world ID (the manifest's `id`), its mover definitions and
  its own scripts; for a library, its scripts. Canonical byte order, each
  once. **The loader checks it equals the content** ("package x4.resource_lab
  defines script x4:script/open_door but does not list it in provides",
  "... lists x4:script/zzz in provides, but the world defines no such
  script"): a declaration cannot lie, and a tool can read what a package
  offers without parsing its content.
- **requires**: the packages it needs, canonical by package ID, each with
  the resources it takes from it (its **imports**), canonical. Both
  package-level and resource-level: the package must be present, and every
  import must be provided by *that* package ("package x4.resource_lab
  requires x4shared:script/nope from package x4.shared, but package
  x4.shared does not provide it"). A requirement may list no imports.
- **schema** 1. Every field is required and nothing else is allowed
  (`version`, `signature`... are refused until a schema says what they
  mean).
- A manifest with no `package` member is an **implicit package**: every
  pre-X4 world. It provides what it defines, requires nothing, and loads
  exactly as before -- no migration.

Two kinds of package:

| Kind | Container | Provides | Found |
|---|---|---|---|
| world | OALMAP v3 (unchanged format; the `package` member is additive) | its world, mover definitions, own scripts | `maps/<name>.oalmap`, picked by file name as before |
| library | OALASSET v1 of kind `library`: the manifest, then (X5) the bytes of its asset members; no OALASSET model or sound records | its scripts and its asset resources | `packages/<package id>.oalasset`, by the ID a requirement names |

Only libraries can be required (a world is what a match plays, not
something another package includes). A library may require other
libraries. Scripts a world imports are copied into its script table at
load -- the world's own first, then imports in canonical order (by package
ID, then resource ID) -- marked with their provider (`hta_wscript_def.
provider`); from there they run exactly as the world's own. The X3 limits
apply to the total (16 scripts, 64 KB of source).

### Where packages come from

`hta_content_load_world` (`src/app/content.c`) asks the same content roots
the world came from: the Asset Lab bundle on the desktop
(`<bundle>/packages/`), the APK's assets on the phone (`assets/packages/`,
bundled by `publish_apk.sh` from `$HTA_IMPORTED/packages/`). Package IDs pass
the grammar before they become a file name, so no path can be formed from
one. The engine never downloads or acquires anything: a missing package is
refused ("package x4.resource_lab requires package x4.shared, but it is not
present (looked for packages/x4.shared.oalasset)").

## The dependency graph

`hta_pkg_set_load` walks requirements from the world, depth first with an
explicit stack (no recursion):

- **each package loads once** (fan-in is fine: a diamond loads its shared
  package once);
- **package requirements must be acyclic**: a package is validated after
  what it needs, and a cycle has no first package. Refused with its path:
  `package cycle: x4.a -> x4.b -> x4.a` (a library requiring the world
  back: `x4.resource_lab -> c.r -> x4.resource_lab`);
- **bounded**: at most 8 requirement links deep, 16 packages in a set, 16
  requirements and 64 imports per package, 256 provides, 512 resources in
  a set (all **INVENTED**, sized far above anything real);
- **canonical order**: the loaded set is sorted by package ID, whatever
  order requirements were followed in or the files were found in
  (**TESTED**: two traversal orders, one set, one key);
- **the file must be what was asked for**: `... but packages/x4.shared.
  oalasset declares package x4.other`.

**Resource references may be cyclic.** The substrate imposes no rule on
them; a relationship that cannot allow a cycle says so itself. Entity links
do (X1): a zero-delay chain that loops would run forever in one tick, so
`link cycle through ...` is refused. Prefabs (X6) avoid the question: a
prefab may not contain another (no nesting in schema 1), and its children's
links are checked acyclic like a world's.

## Compatibility identity

The world key (docs/WORLD_ENTITIES.md "World compatibility") is what two
peers compare before a joiner spawns. X4 extends it additively:

- **`package` is a played manifest member.** Its bytes -- the package ID,
  provides and requires -- are hashed exactly as stored, so a world that
  requires a different package or imports a different script is a different
  world.
- **Dependencies are hashed.** After the members, for every package in the
  closure, sorted by package ID: `"OALD"`, u32 ID length, the ID, and that
  library's own 64-bit digest -- FNV-1a 64 over `"OALL"`, u32 schema, and its
  `assets` (X5), `package` and `scripts` members exactly as stored, then,
  when it declares assets, `"OALP"`, u32 payload length and **every
  payload byte**. A one-character change to a library's Lua, or one texel,
  vertex or sample of its assets, changes the key of every world requiring
  it, though the world's own bytes did not change. **TESTED** (unit, OAL,
  and `test_x4.sh`/`test_x5.sh` through the real join: refused before
  spawn). A library without assets has no `assets` member and no payload,
  so X4 libraries digest exactly as before.
- **Not hashed:** provenance, display names, importer versions -- the
  world's and the library's (`source_provenance`, `importer_version`,
  `display_name`, `asset_version`, and X5's per-resource `provenance`
  member). A provenance-only change is admitted. **TESTED**.
- **Texture pixels.** A world's OWN baked textures stay out of its key, as
  they always were (the pre-X4 rule: pixels change how a world looks, not
  what it does) -- so every existing key is unchanged. A LIBRARY's asset
  payload is hashed whole: an asset resource is content other packages
  build on, and a peer drawing a different model at the same place is a
  different world. Two rules, both deliberate, both documented here.
- **Ordering.** Lists in the declaration must be canonical, so there is one
  byte form per declaration and nothing to normalise; the closure is hashed
  in package-ID order, so the order requirements were walked in or packages
  found in cannot change the key. Two declarations that differ at all
  (another requirement, another import) are different keys, deliberately.
- **Pre-X4 packages: keys unchanged.** None carries a `package` member, and
  a world without one has no dependencies to append, so the stream is
  byte-identical to X2/X3's and so is every key (x1 `552c1757`, x2
  `73bd2d8b`, x3 `512a1fc3`, the imported maps). No key schema bump:
  `HTA_WORLD_KEY_SCHEMA` stays 1, through X5 too (x4 `46bee75f` unchanged).
  What older engines do with newer packages is measured, not assumed:
  "Older engines and newer packages" below.
- **Desktop and Android** compute the key from the same bytes with the same
  portable C; the emulator's key equals the desktop's and Open Asset Lab's
  (below).

## Provenance is not authority

Where content came from -- a Workshop item ID, a source path, an importer
version, a licence -- is provenance: kept in manifests for people and
tools, never part of a resource ID, never hashed into the world key, never
consulted to load or resolve anything. Resource IDs name normalised
MegaMod content; a Source `targetname` or a Workshop number becomes one only
through an importer's explicit mapping, recorded as provenance. The engine
never needs Steam, Source or any provider to run a package.

## Introspection: `megamod-resources`

```
megamod-resources [--json]       the contract: grammar, types, reference fields,
                                 package schema and limits, world key members
megamod-resources --markdown     the tables above
megamod-resources --conformance  a corpus of resource and package IDs with this
                                 engine's verdict and reason for each
megamod-resources --bundle DIR --world NAME
                                 load a world with everything it requires, as a
                                 match does; print its package set, where every
                                 script came from, what every reference resolved
                                 to (field, target, index) and its world key; or
                                 the loader's refusal (exit 1)
```

X5 extends each: `--json` gains `assets` (the member, descriptor fields
per type, formats, payload layouts, member-path rules, limits) and
`world_entities` (schema 4, the `prop` kind, a mover definition's `sound`),
and the four asset types and four new reference fields in `types` and
`references`; `--markdown` prints them in the tables above;
`--conformance` gains `member_paths` (a corpus of package-local paths with
this engine's verdict and words) and asset-type IDs; `--bundle` shows every
asset reference (prop -> model, mover definition -> sound, material ->
texture, model slot -> material) with its provider package and asset table
index, and the world's asset table.

All of it is printed from the tables and code the loader uses. Drift is
caught three ways: `test_resource` compares the tables in this file with
`--markdown`; Open Asset Lab keeps `--json` and `--conformance` as
`assetlab/data/megamod_resources.json` and `megamod_id_conformance.json`,
reads its registry and limits from the first and checks its own grammar
(and, X5, its member-path rules) against every verdict in the second;
`scripts/test_x4.sh` and `test_x5.sh` check both copies equal this build's
output. (Like `megamod-script-api` for scripting.) After a contract change:
`megamod-resources --json > assetlab/data/megamod_resources.json`, the same
for `--conformance`, and `scripts/regen_resource_tables.sh` for this file.

## Open Asset Lab

Open Asset Lab validates the same rules before a package is written, and
never runs a script (only `luac5.4 -p` parses one, when present):

- `assetlab/resources.py`: the grammar (verdicts and words identical to the
  engine's), the registry read from the contract, `ResourceSet` with the
  same resolution and refusals, package declarations;
- `assetlab/dependencies.py`: library packages (`compile_library`), the
  graph (`load_set`, the engine's walk and messages), library digests, and
  `check_package` for built packages;
- `assetlab/world.py`: a world with `package=` and `requires=` declares
  itself; every reference goes through the resolver;
- `assetlab resources check PKG... [--packages-dir D]` and `assetlab
  world-key PKG --packages-dir D`.

The runtime stays authoritative: it checks everything again, whatever wrote
the package.

## The X4 proof

`scripts/test_x4.sh` (in `verify.sh`), with Open Asset Lab's
`x4_resource_lab` and its library `x4.shared`:

1. OAL's copies of the contract and conformance corpus are this build's.
2. The engine's world key equals OAL's (`46bee75f`); `megamod-resources`
   shows the button resolved to the world's own `x4:script/open_door` and
   `ability_script` to `x4shared:script/pulse_ability` from `x4.shared`.
3. Refused by OAL's checker **and** the engine, with the same message:
   dependency omitted, missing script, wrong type (a mover in a script
   field), malformed ID, an import not declared, a duplicate provider, a
   file that declares another package.
4. Host + joiners: the button's own script opens door A through the world
   queue; the imported ability kills a target through native damage (kill
   credited); a late joiner finds door A open; no joiner runs Lua.
5. A one-character change to the library (world bytes identical) is refused
   before spawn; a provenance-only library change is admitted; a joiner
   without the library cannot load the world and says what it lacks.

## Emulator evidence (Android 14 x86_64, build fc5f587)

**OBSERVED**, 2026-09-27: the emulator APK carried `assets/maps/
x4_resource_lab.oalmap` and `assets/packages/x4.shared.oalasset`; hosting
X4 it logged `[script] 2 scripts loaded (megamod.v1, host only); ability
script on` -- the world's own script and the library's, loaded from the APK
through the same fs path as the desktop. Desktop joiners through
`scripts/emu/udprelay.py` were admitted (so the phone's world key, library
digest included, equals the desktop's): A found door A shut and blocked,
pressed the button (the phone's Lua: `x4:script/open_door on_used ... ok, 1
requests`), pressed ability (`x4shared:script/pulse_ability: pulse by
player 2 hit 1`, then `Player 2 was killed by Player 3`); target T saw its
death; late joiner B found door A open; the library-modified joiner was
refused ("different map"), the provenance-only one admitted, and a joiner
without the library refused to load the world ("requires package x4.shared,
but it is not present"). No joiner logged a script line.

## Tests

| Test | Covers |
|---|---|
| `tests/test_resource.c` (ctest `resource`) | grammar: every valid and malformed shape, edge lengths, reserved and unknown types, package IDs; the registry; typed resolution (types, imports, hints, duplicates, same-package fields, a full set's timing); declarations (every refusal and limit, nesting); libraries (load, digest, refusals); graphs (chain, fan-out, fan-in, missing, import not provided, cycles with paths, depth 8/9, width 16/17, a lying file); a world importing a script (table order, providers, typed refusals across packages, provides checks); the world key over dependencies (library change, provenance, a library's own requirement, traversal order); 3000 mutated worlds, 2000 mutated libraries and 20000 random IDs (same answer twice, printable messages, no crash; ASan/UBSan); the contract and this file's tables |
| `tests/test_world_entities.c`, `test_script.c`, `test_external_map.c` | X1-X3 unchanged through the new resolver; `world.entity` typed refusals |
| `scripts/test_x4.sh` | above |
| OAL `tests/test_resources.py` | conformance with the engine's corpus, the contract as the registry's source, resolution, declarations, graphs, libraries, world packages, keys, X1-X3 fixtures byte-identical, never running Lua |
| `tests/test_asset.c` (ctest `asset`, X5) | a library's assets decoded and linked; prop -> model -> material -> texture resolved once, pixels shared not copied; props solid; a mover's sound cued once per start, silent on a join's snap and a round reset, heard through the mixer; the sound bank reusing a clip; two consumers, one library; a library importing another's texture; every refusal (graph, imports, missing, wrong type in each field, malformed, reserved, provides both ways, missing/unsafe/unused/shared members, sizes, formats, duplicate descriptors, corrupt meshes, duplicate provider, a liar); the world key over texel, vertex, sample and draw changes, not provenance; older-engine pins; 200 load/free cycles and failures halfway through a set (ASan/LSan); 4000 mutated libraries, 3000 mutated worlds, 20000 member paths (same answer twice, printable messages); 7 libraries / 391 textures / 60 props resolved in milliseconds |
| `scripts/test_x5.sh` | "The X5 proof" |
| `scripts/test_cross_version.sh` | "Older engines and newer packages" (builds the X3 and X4 engines from history; not in verify.sh) |
| OAL `tests/test_assets.py` | member paths against the engine's corpus, limits from its contract, libraries (round trip, determinism, digest over payload and not provenance, X4 library bytes pinned, refusals in the engine's words, validation before writing, a library importing another's texture), worlds (props, sounds, refusals, inline movers stay schema 1), pinned X5 keys, two consumers, X4 fixture bytes pinned, a Source model imported as resources |

## Asset resources (X5)

> How does an actual reusable asset live behind a resource identity?

X4 answered what a resource is, who provides it and who may reference it;
only scripts crossed packages. X5 makes four kinds of ordinary content
real resources a LIBRARY provides and any package imports:

| Type | ID | Descriptor fields | Payload (a package member) | Runtime | Owner |
|---|---|---|---|---|---|
| texture | `x5shared:texture/test_crate` | format `rgba8`, width, height, member | width x height x 4 bytes (an OALMAP texture record's pixels) | `hta_asset_texture`: pixels | the world's asset table |
| material | `x5shared:material/test_crate` | draw (`opaque`, `alpha`), texture (a typed reference) | none | `hta_asset_material`: texture index, draw mode | the world's asset table |
| model | `x5shared:model/test_crate` | format `mesh1`, materials (typed references, one per slot), member | `MSH1`, u32 vertex/index/group counts, 40-byte vertices (the OALMAP's), u32 indices, groups of (first, count, slot) | `hta_asset_model`: an `hta_bsp_mesh` in model space whose submeshes draw with their slots' textures (borrowed) | the world's asset table; the GPU mesh: the renderer's `hta_went_gpu` |
| sound | `x5shared:sound/test_impact` | format `pcm_s16le`, rate, channels, frames, member | frames x channels x 2 bytes (an OALASSET sound record's samples) | `hta_asset_sound`: samples | the world's asset table; the mixer's copy: the session's `hta_world_sounds` bank |

Formats reuse what the engine already read; nothing is transcoded at run
time. `megamod-resources --json` ("assets") is the exact schema and limits.

### A library that provides them

```json
"assets": {"schema": 1,
  "members":   [{"path": "models/test_crate.mesh", "size": 1132}, {"path": "sounds/test_impact.pcm", "size": 11024},
                {"path": "textures/test_crate.rgba", "size": 1024}],
  "textures":  [{"format": "rgba8", "height": 16, "id": "x5shared:texture/test_crate", "member": "textures/test_crate.rgba", "width": 16}],
  "materials": [{"draw": "opaque", "id": "x5shared:material/test_crate", "texture": "x5shared:texture/test_crate"}],
  "models":    [{"format": "mesh1", "id": "x5shared:model/test_crate", "materials": ["x5shared:material/test_crate"], "member": "models/test_crate.mesh"}],
  "sounds":    [{"channels": 1, "format": "pcm_s16le", "frames": 5512, "id": "x5shared:sound/test_impact", "member": "sounds/test_impact.pcm", "rate": 22050}]},
"package": {"id": "x5.shared_art", "provides": ["x5shared:material/test_crate", "x5shared:model/test_crate",
            "x5shared:sound/test_impact", "x5shared:texture/test_crate"], "requires": [], "schema": 1},
"provenance": {"x5shared:model/test_crate": {"provider": "original", "license": "GPL-3.0-or-later", ...}}
```

then the members' bytes, in `members` order, right after the manifest.

- **A resource ID is identity; a member path is storage.** `models/
  test_crate.mesh` is where the bytes sit inside this package -- never
  looked up by any other package, never joined to a host path (the payload
  is found by offset in the package's bytes, desktop and APK alike), never
  a resource ID. A path is lowercase `[a-z0-9_]` segments joined by `/`,
  one extension on the last, at most 96 bytes and 6 segments: no `..`, no
  leading `/`, no `\`, no NUL, no capitals -- refused, never repaired.
- **Strict and canonical.** Every descriptor field is required and nothing
  else is allowed; lists are in canonical ID (members: path) order, each
  once; every member backs exactly one resource and every resource's
  member exists; sizes must equal what the descriptor says; a mesh's
  counts, offsets, indices, groups and slots are all checked; nothing is
  allocated before its size is known to fit.
- **provides == content**, scripts and assets alike, both directions.
- **Internal references are typed references.** A material's `texture`
  and a model's `materials` go through `hta_res_resolve` from the
  library's own point of view: its own resources, or ones it imports from
  a library it requires. A slot never takes a texture; a library can build
  materials on another library's textures (TESTED).
- **Only libraries provide assets** (for now): a world imports them. A world
  that lists a model in its provides is refused ("the world defines no
  such model").

### A world that uses them (world_entities schema 4)

```json
{"id": "x5:entity/crate_a", "kind": "prop", "links": [], "model": "x5shared:model/test_crate", "position": [-2.0, 1.2, 0.25]}
{"id": "x5:mover/basic_slide_door", "move": [0, 1.25, 0], "size": [0.1, 1.2, 1.1], "sound": "x5shared:sound/test_impact", "speed": 1}
```

- A **prop** (a new placement kind: emits and accepts nothing) draws its
  model where it stands and is solid as the model's bounds there (one box
  collision grid, built once; it never moves). Axis-aligned: no rotation
  yet.
- A mover definition's optional **sound** plays when a mover starts to open
  or close -- on the host from its own events and on every joiner from the
  host's replicated mover state (no protocol change; a joiner that finds a
  door already moving, or a new round, is silent).
- Schema 4 is required for either, and a schema 4 world gives every mover a
  definition (inline movers stay X1's schema 1).

### Reference flow, once, at load

```
package set loads (each package once, by ID)
  -> each library: assets parsed, members bound, payloads decoded and checked
  -> provides == content; digest over played members + payload
  -> set sorted by package ID; each library's material/slot references
     resolved typed (its own or its imports) to combined-table indices
  -> the world: prop.model / definition.sound resolved typed
     (world_entities.entities[].model, ...mover_definitions[].sound) against
     the world's own resources + its declared imports only
  -> the combined table MOVED to the world (hta_external_map.assets):
     textures by package ID then resource ID, and so on per type -- the
     same indices on every peer; each model bound to its slots' textures
  -> gameplay and rendering hold indices: prop -> model index,
     definition -> sound index; no name is looked up during play
```

`megamod-resources --bundle` prints every one of those references with its
provider package and index.

### Lifetime and ownership

- The **world** (`hta_external_map`) owns the asset table: texture pixels,
  model meshes, sound samples. Freed with the world
  (`hta_external_map_free`), which happens when the match's world is
  replaced or the app exits; never during a round.
- **Materials own no pixels** and **models borrow** their textures'
  pixels: one copy of every texture however many models, props or
  consumers use it.
- A **package set** is scratch while loading: libraries decode into it,
  then everything moves to the world (`hta_pkg_set_take_assets`) and the
  set is freed. A failure anywhere -- a library halfway through a set, a
  bad reference after the set loaded -- frees everything decoded so far
  (TESTED under ASan/LSan: 200 load/free cycles, failures at each stage).
- The **renderer** uploads each model a prop places once
  (`hta_went_gpu.model[]`, shared by every prop naming it) and frees those
  GPU meshes with the rest of the world's entity meshes.
- The **mixer's** clips are append-only and read on the audio thread, so it
  never borrows a world's samples: `hta_world_sounds` copies each DISTINCT
  sound once (matched by content) into a bank that lives as long as the
  session and is freed after the audio device stops. A later world with the
  same sound reuses the clip.
- **Round reset** changes nothing here (props are static, assets
  immutable; movers close silently). **Lua** never sees or owns any of it.

### Compatibility identity

A library's digest covers its `assets` member and every payload byte
("Compatibility identity" above); a world's key covers its required
libraries' digests in package-ID order. So one texel, vertex, sample or a
material's draw mode in a required library is a different world, and a
joiner holding it is refused before spawn, while a provenance-only change
is admitted (TESTED, desktop and Android). The world's own bytes carry the
references (the `world_entities` and `package` members); the library
carries the content. The canonical bytes decide, never a runtime struct:
desktop x86-64 and the Android emulator computed the same keys (below).

### Legacy content keeps working

Imported maps keep their baked props and textures; characters, weapons and
the UI sound pack still load by file name and are still matched by the
content fingerprint. No X1-X4 package changed a byte and no key moved:
x1 `552c1757`, x2 `73bd2d8b`, x3 `512a1fc3`, x4 `46bee75f`, de_dust2
`52fb3b08` (TESTED, pinned in OAL).

### Refusals (the engine's words, and Open Asset Lab's)

| Case | Message |
|---|---|
| dependency omitted | `x5:mover/basic_slide_door references missing sound x5shared:sound/test_impact (no package in this set provides namespace 'x5shared': is a requirement missing?)` |
| provider package missing | `package x5.resource_world requires package x5.shared_art, but it is not present (looked for packages/x5.shared_art.oalasset)` |
| missing resource | `x5:entity/crate_a references missing model x5shared:model/test_crates` |
| wrong type | `x5:entity/crate_a: model x5shared:material/test_crate is a material, expected a model` |
| malformed ID | `x5:entity/crate_a: model 'X5shared:model/test_crate' is not a resource ID: namespace has capital 'X' ...` |
| undeclared import | `x5:entity/crate_a: model x5shared:model/test_crate is provided by package x5.shared_art, which package x5.resource_world requires but does not import it from ...` |
| duplicate provider | `x5shared:texture/test_crate: provided by both package x5.dup_art and package x5.shared_art (duplicate providers are refused, never picked)` |
| file declares another package | `package x5.resource_world requires package x5.shared_art, but packages/x5.shared_art.oalasset declares package x5.other_art` |
| missing payload | `x5shared:model/test_crate declares package member models/test_crate2.mesh, but that member is missing` |
| invalid payload path | `x5shared:model/test_crate: member path 'models/Test_crate.mesh': has capital 'T' (paths are lowercase; nothing is folded)` |
| path traversal | `member path '../models/test_crate.mesh': has '..' (no parent references)` |
| duplicate descriptor | `package x5.shared_art: x5shared:texture/test_crate is declared twice` |
| provides disagrees | `package x5.shared_art has material x5shared:material/test_crate but does not list it in provides` |
| payload size | `x5shared:texture/test_crate: 5x4 rgba8 is 80 bytes, but member textures/crate.rgba holds 64` |
| corrupt mesh | `...: member models/crate.mesh: index 2 names vertex 24 of 24` (and counts, groups, slots, non-finite) |
| slot of the wrong type | `x5shared:model/test_crate: material slot x5shared:texture/test_crate is a texture, expected a material` |
| a library's import not declared | `sk:material/glass: texture xs:texture/crate is provided by package t.art, which package t.skin requires but does not import it from ...` |

### Older engines and newer packages

Measured with real binaries built from this repository's history
(`scripts/test_cross_version.sh`, OBSERVED 2026-09-27):

| Engine | Package | Result |
|---|---|---|
| X3 (a4e0318) | X4 world importing a library script | refused at load: `ability_script references missing script x4shared:script/pulse_ability` |
| X3 | declared X4 world needing no library | loads (it ignores the `package` member; the world plays the same), key `a902e83c` vs X4+ `ab402328`: X3 and X4+ peers refuse each other before spawn, **both directions** (`REFUSED (not the host's map)`) |
| X3 | X5 world | refused at load: `unknown kind 'prop'` |
| X4 (e0ae793) | X5 world | refused at load: `resource type 'model' is reserved, not loadable by this engine` |
| X4 | X5 library | refused (`a library carries a manifest only`; its provides hold reserved types) |
| X4 and X5 | X4 worlds | the same keys (`46bee75f`); an X4 joiner is admitted by an X5 host |

So no older engine reaches multiplayer with newer semantics: it either
refuses the package, or plays a world whose semantics it fully has and
computes a key no newer peer shares. `test_asset` pins the mechanism
without old binaries: a declared world's key always differs from the key of
its bytes read the pre-X4 way (this build reproduces the X3 binary's
`a902e83c` exactly that way), and an X5 world always carries
world_entities schema 4 and imports of types X4 reserved. No key schema
bump was needed.

### Open Asset Lab

- `assetlab/assets.py`: `Texture`, `Material`, `Model`, `Sound`; the
  descriptors, mesh1, member paths (checked against the engine's
  conformance verdicts), and a reader with the engine's checks and words.
- `assetlab/dependencies.py`: a `Library` carries assets; its manifest gets
  `assets` and a per-resource `provenance` member (never played), its bytes
  the payload; `read_library`, `link_assets` and the digest mirror the
  engine. A library without assets is written byte-for-byte as X4 wrote it.
- `assetlab/world.py`: `Entity(kind='prop', model=...)`,
  `MoverDefinition(sound=...)`, schema 4, every reference typed.
- **Source/GMod**: `assets.from_source_model` (CLI `assetlab asset-library
  MDL... --package ID --namespace NS`) turns a Source static model into
  `ns:model/<name>`, one material and texture per Source material, in
  runtime units -- the MDL, VMT and VTF paths, provider, Workshop item and
  licence kept as provenance only. OBSERVED: two CS:S props (de_dust crate,
  de_nuke crate) converted into scratch, placed by a test world and drawn
  by the engine through their resource IDs.

## The X5 proof

`scripts/test_x5.sh` (in `verify.sh`), with Open Asset Lab's library
`x5.shared_art` and its two consumers `x5_resource_world` and
`x5_second_world`:

1. OAL's copies of the contract and conformance corpus (member paths
   included) are this build's.
2. Keys: engine == OAL (`1b067045`, second world `6748f47e`); the
   resolved references (prop -> model -> material -> texture, definition ->
   sound) each from `x5.shared_art`; both consumers hold one library (the
   same digest); neither world carries the library's bytes.
3. Every refusal in the table above but the last four, by OAL's checker
   AND the engine, the same words.
4. Host + joiner A: A is blocked by crate A; presses button A; door A opens
   and the library's sound starts on the host and on A. Late joiner B finds
   door A open (and hears nothing); B's picture has both crates in the
   library's texture. The second world hosts and admits its joiner, crate
   drawn. No joiner runs Lua.
5. One texel of the library (world bytes identical) is refused before
   spawn; a provenance-only library change is admitted; a joiner without
   the library says what it lacks.

### Emulator evidence (Android 14 x86_64)

**OBSERVED**, 2026-09-27: the emulator APK carried `assets/maps/
x5_resource_world.oalmap`, `x5_second_world.oalmap` and `assets/packages/
x5.shared_art.oalasset`. Hosting X5 it logged `[world] assets: 1 textures,
1 materials, 1 models, 1 sounds (12 KB), 2 props, 1 sounds bound` and
`[world] 3 movers drawn, 1 prop models`, its map check `fce6fc67` equal
to the desktop's for the same world, and drew both crates in the library's
texture. Desktop joiners through `scripts/emu/udprelay.py`: A was blocked
at (-2.45, 1.20) by crate A, pressed button A -- the phone logged `[world]
sound x5shared:sound/test_impact: x5:entity/door_a started opening` and A
`join: sound ... started moving`; late joiner B found door A open; the
one-texel library was refused ("not the host's map"), the provenance-only
one admitted, and a joiner without the library refused to load the world.

## Prefabs (X6)

A library may also provide PREFABS (`namespace:prefab/name`, its
`prefabs` member): compositions of the world's entity kinds whose model,
sound and script references resolve here, typed, **from the providing
library's point of view**, once, when the set loads
(`prefabs.prefabs[].children[].model|sound|script`). A world imports only
the prefab (`world_entities.prefab_instances[].prefab`) and places
instances, which expand at load into ordinary placed entities. The
library's digest covers its `prefabs` member, so a changed child is a new
world key; a library without one digests as before. Older engines refuse
prefab packages (`resource type 'prefab' is reserved`). Everything:
[PREFABS.md](PREFABS.md).

## Event bindings (X7)

Worlds (`world_entities` schema 6) and prefabs (prefab schema 2) may carry
declarative event bindings. Their entity references are typed references
of their own (`world_entities.bindings[].source`, `.conditions[].entity`,
`.actions[].target`, `.actions[].at`: placed entities in the same world, a
prefab child by its placed ID included) and their sounds are `sound`
resources (`world_entities.bindings[].actions[].sound`, imported;
`prefabs.prefabs[].bindings[].actions[].sound`, from the prefab's own
package). The Events, Conditions and Actions tables above come from the
same tables the loader uses; `--json` adds limits, each kind's affordances
and the tick phase. No key schema change: bindings are bytes of members the
key already covers. Everything: [EVENT_BINDINGS.md](EVENT_BINDINGS.md).

## Not in X5

- **Prefabs** -- done in X6 ([PREFABS.md](PREFABS.md)).
- Worlds providing their own asset resources (only libraries do); props
  with rotation or scale; props that move, break or carry links; a model's
  own collision mesh (a prop collides as its bounds).
- Animation, skeletal models, characters and weapons as resources (still
  loaded by file name); the UI sound pack as resources.
- Sounds anywhere but a mover definition's start; materials beyond one
  texture and a draw mode (no shaders, no lightmaps, no surface kinds).
- GPU texture sharing between models (each model's GPU mesh uploads its
  slots' textures; the CPU copy is single).
- Streaming, downloading or transferring packages; a peer must already
  hold every package (a missing one is refused, named).

## Not in X4

- Versions and version constraints, signatures, integrity digests,
  installation, updates, caching, remote acquisition, a repository, load
  order: a package set is exactly what the world requires, found by ID.
- Importable types other than `script`; libraries of models, materials,
  sounds, prefabs; characters and weapons as declared packages.
- Script fault isolation per package: a failing imported script disables
  the world's scripts for the match, like any script (SCRIPTING.md).
- A name-to-index table on the wire: protocol v10 is unchanged; peers
  still prove identical content with the world key.
