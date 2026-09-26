# Comparative content registration

| Reference | Registration mechanism | Useful principle | Cost to avoid |
| --- | --- | --- | --- |
| [Factorio](FACTORIO.md) | Named prototypes in startup stages | Content exists before world; runtime read-only | Shared mutable final-fixes ordering |
| [Arma 3](ARMA3.md) | Config class tree and `CfgPatches` | Explicit supplied content/dependencies | Huge merged global inheritance graph |
| [Bethesda/xEdit](BETHESDA_XEDIT.md) | Plugin records, masters, overrides | Stable references and conflict inspection | Load-order-coupled winning override |
| [tModLoader](TMODLOADER.md) | Mod-owned `ModType` subclasses | Loader discovers new types and hooks | One native subclass/hook path per game variant |
| [Space Engineers](SPACE_ENGINEERS.md) | Typed definitions and object builders | Definition versus serialized/runtime entity | Complex class/object-builder machinery |
| [Source/GMod](SOURCE_GMOD.md) | Class names, Lua ENT/SWEP registration | Map entity lookup and script extension | Foreign class names and unbounded net tables |
| [Project Zomboid](PROJECT_ZOMBOID.md) | Item/recipe script definitions plus Lua events | Data definitions beside scripted behavior | Version-sensitive broad Java bridge |

## Recommended registry (inference)

**Categories:** `character`, `weapon`, `projectile`, `ability`, `vehicle`, `material`, `sound`, `game_mode`, `world_entity`, `mutator`, plus world/map and animation/skeleton assets. Every definition has a category, lowercase namespaced canonical ID (`weapon:mega.ion_rifle`), owner package ID/version, schema version, source provenance, and optional behavior script ID. The colon/dot notation is illustrative; the parser grammar remains to be chosen. An ID is stable across load-order changes and never derived from a file position.

**Compile flow:** OAL validates manifest/dependency DAG → imports and normalizes → resolves authoring inheritance and explicit typed patches → validates cross-references and material/skeleton/attachment compatibility → computes canonical package and schema hashes → emits compact read-only tables. Runtime checks bounds, required feature/API versions, dependency lockfile, IDs, and hashes, then assigns dense numeric indices. Gameplay scripts may instantiate registered definitions through validated host APIs; they cannot register or mutate types after a match starts.

**Overrides:** initial policy should be *addition under own namespace only*. A package may reference another package's public definitions if it declares a compatible dependency. When real use demands customization, support an explicit patch with target ID, expected owner/version/hash, changed paths, and provenance. If two patches touch the same field, OAL requires an explicit merge or a creator-selected winner shown in a report; runtime should never silently choose by filename/load order. A patch that renames an ID needs a migration alias for saves.

**Conflicts:** duplicate canonical IDs, missing dependencies, cycles, incompatible versions, unresolved references, invalid component combinations, unsupported script API versions, and network schema collisions are build errors. Same-ID definitions from unrelated packages should never be merged automatically. Validation reports should include source file/line, normalized ID, package, dependency path, and suggested correction.

**Unload:** no hot unload during an active match initially. At the match boundary, destroy instances, cancel timers/events, release scripts, release package-owned assets, clear registry indices, then load the next closure. This matches the current native runtime's need for explicit memory ownership. Hot reload can be an authoring-only later experiment.

**Open Asset Lab role:** OAL owns normalization and registry compilation; MegaMod owns runtime safety and last-resort validation. OAL can generate creator-facing ID search, dependency graphs, material maps, skeleton/animation compatibility, and references from world entities to assets. See [package format](PACKAGE_MOD_FORMAT.md).
