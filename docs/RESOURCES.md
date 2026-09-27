# Resource identity and package dependencies (X4)

**Status:** implemented 2026-09-27 (X4). This is **infrastructure, not a
gameplay feature**: one grammar for content IDs, one registry of resource
types, typed references resolved once at load, explicit package
declarations with provides and requires, a checked dependency graph, and a
world key that covers what a world depends on. It describes the code as it
is. The code: `src/asset/resource.{h,c}` (grammar, registry, typed
resolution), `src/asset/package.{h,c}` (declarations, libraries, the graph),
`src/asset/resource_contract.c` (the contract, printed), `src/asset/
world_def.c` (the first consumer), `src/asset/external_map.c` (loading, the
world key), `src/app/content.c` (where required packages come from),
`src/tools/resources.c` (`megamod-resources`).

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
- The type must be registered. A **reserved** type (`model`, `prefab`...)
  parses but nothing may provide or reference one yet ("resource type
  'model' is reserved, not loadable by this engine"), distinct from an
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
| `sounds` | sound pack | supported | package | - | - | N2 | the UI sound pack (.oalasset); loaded by file name, its ID is audit-only |
| `model` | model | reserved | definition | - | - | X4 | reserved: a mesh a package provides for others to place |
| `material` | material | reserved | definition | - | - | X4 | reserved: a surface (textures, surface kind) |
| `texture` | texture | reserved | definition | - | - | X4 | reserved: an image |
| `sound` | sound | reserved | definition | - | - | X4 | reserved: one sound (not the UI 'sounds' pack) |
| `animation` | animation | reserved | definition | - | - | X4 | reserved: a clip for a skeleton |
| `prefab` | prefab | reserved | definition | - | - | X4 | reserved: a composed, reusable entity (a likely X5) |
| `ruleset` | ruleset | reserved | definition | - | - | X4 | reserved: game rules (team deathmatch...) |

| Reference field | Expects | Resolves to | Since |
|---|---|---|---|
| `world_entities.entities[].links[].target` | placed entity | the same package | X1 |
| `world_entities.entities[].definition` | mover definition | the same package | X2 |
| `world_entities.entities[].script` | script | the same package or a declared import | X3 |
| `world_entities.ability_script` | script | the same package or a declared import | X3 |
| `world.entity(id)` | placed entity | the same package | X3 |
| `package.requires[].resources[]` | any importable type | the required package it is listed under | X4 |
<!-- megamod-resources --markdown: end -->

- **Scope.** *package*: identifies what a whole package is. *placement*: a
  thing placed in one world, never listed in provides, never taken from
  another package. *definition*: reusable content a package defines and
  lists.
- **Importable** (another package may require it): `script` only, in X4 --
  the one type the runtime resolves across packages and the proof needed.
  Mover definitions stay inside their world (a library has no geometry
  to draw them with). A future type becomes importable by flipping the
  registry row and adding its runtime binding; the declaration, graph and
  key already handle any importable type.
- `character`, `weapon`, `sounds` IDs are recognised (Open Asset Lab's `ids`
  audit proposes them) but those packages are still loaded by file name
  and matched by the content fingerprint (CONTENT_COMPATIBILITY.md); they do
  not declare packages yet.
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
| library | OALASSET v1 of kind `library`, manifest only (no models, no sounds, nothing after it) | its scripts | `packages/<package id>.oalasset`, by the ID a requirement names |

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
`link cycle through ...` is refused. A future relationship (prefabs naming
each other, say) decides its own rule.

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
  `package` and `scripts` members exactly as stored. A one-character change
  to a library's Lua changes the key of every world requiring it, though
  the world's own bytes did not change. **TESTED** (unit, OAL, and
  `test_x4.sh` through the real join: refused before spawn).
- **Not hashed:** provenance, display names, importer versions -- the
  world's and the library's (`source_provenance`, `importer_version`,
  `display_name`, `asset_version`). A provenance-only change is admitted.
  **TESTED**.
- **Ordering.** Lists in the declaration must be canonical, so there is one
  byte form per declaration and nothing to normalise; the closure is hashed
  in package-ID order, so the order requirements were walked in or packages
  found in cannot change the key. Two declarations that differ at all
  (another requirement, another import) are different keys, deliberately.
- **Pre-X4 packages: keys unchanged.** None carries a `package` member, and
  a world without one has no dependencies to append, so the stream is
  byte-identical to X2/X3's and so is every key (x1 `552c1757`, x2
  `73bd2d8b`, x3 `512a1fc3`, the imported maps). No key schema bump:
  `HTA_WORLD_KEY_SCHEMA` stays 1. An X3 build and an X4 build agree on every
  pre-X4 world; on a declared world the X3 build ignores the member,
  computes another key, and they refuse each other -- the safe direction
  (an X3 build cannot load a world that imports scripts at all).
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

All of it is printed from the tables and code the loader uses. Drift is
caught three ways: `test_resource` compares the tables in this file with
`--markdown`; Open Asset Lab keeps `--json` and `--conformance` as
`assetlab/data/megamod_resources.json` and `megamod_id_conformance.json`,
reads its registry and limits from the first and checks its own grammar
against every verdict in the second; `scripts/test_x4.sh` checks both copies
equal this build's output. (Like `megamod-script-api` for scripting.)

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
