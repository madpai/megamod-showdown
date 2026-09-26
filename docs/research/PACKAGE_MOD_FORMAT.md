# Future MegaMod package/mod format study

This is a **conceptual contract**, not a finalized extension or binary layout. Current [OALMAP v1/v2 and OALASSET v1](https://github.com/madpai/open-asset-lab/blob/main/docs/RUNTIME_PACKAGE.md) work and should remain readable while a real feature justifies a successor. The [MegaMod vision](../MEGAMOD_VISION.md) already considers an extensible chunked container. `.oalmod` and `.megapack` remain unchosen names.

## Ecosystem comparison

| Problem | Strong reference | Implication |
| --- | --- | --- |
| Identity/version | [Factorio `info.json`](FACTORIO.md), [tModLoader owner/name](TMODLOADER.md) | Canonical package ID + semantic package version + runtime API range |
| Dependencies | [Arma `CfgPatches`](ARMA3.md), [Factorio lifecycle](FACTORIO.md), [Bethesda masters](BETHESDA_XEDIT.md) | Explicit dependency DAG and locked exact resolved versions |
| Assets/scripts/config | [GMod layout](SOURCE_GMOD.md), [PZ layout](PROJECT_ZOMBOID.md) | Distinct typed sections and declared script realms |
| Overrides | [Bethesda/xEdit](BETHESDA_XEDIT.md) | Show conflict/provenance; no implicit last-wins |
| Client compatibility | [NS2 consistency](NATURAL_SELECTION.md), [GNS concepts](GAME_NETWORKING_SOCKETS.md) | Match exact content and replication schema hashes before join |
| Tooling | [Nanoforge](RED_FACTION_GUERRILLA.md), [Factorio docs](FACTORIO.md) | Preflight validation and creator-readable diagnostics |

## Proposed manifest fields (inference)

| Group | Fields | Why |
| --- | --- | --- |
| Identity | `package_id`, `version`, `display_name`, `authors`, `description`, `license`, `source_provenance` | Namespacing, review, rights traceability |
| Contract | `package_schema_version`, `required_runtime_api`, `required_features`, `content_schema_versions` | Safe compatibility checks |
| Dependencies | `requires[{id,version_range}]`, `optional_requires`, `conflicts`, `resolved_lockfile` | Reproducible DAG |
| Content | Typed definition inventory and canonical IDs; section/chunk indexes | Cheap lookup without scanning foreign formats |
| Script | Entry points by realm (`host`, later `client_cosmetic`), API version, capabilities, state schema version | Authority and persistence boundaries |
| Network | Replication schema digest, maximum declared event/state sizes | Join matching and bounded codec |
| Integrity | Per-section size/hash, whole-package hash, compiler version, input source hashes | Deterministic builds and corruption detection |
| Rights | Asset-level provenance/license/redistribution status, generated/inferred field tags | Public publishing decisions and audit |

**Package contents:** canonical manifest; typed definition table; normalized meshes/materials/textures/skeletons/animation/audio; collision/nav/world entity/event data; optional host scripts; generated validation report. Unknown *optional* chunks can be skipped; unknown required features, bad bounds, missing dependencies, hash mismatch, or unsupported schemas reject the package. A zipped directory or one chunked file is a future engineering choice after measuring Android mmap, compression, and patchability. Runtime paths must be relative, normalized, and bounded. Avoid storing credentials or absolute creator paths.

**Dependency algorithm:** OAL resolves a DAG, rejects cycles and unsatisfied version ranges, flattens inherited definitions and explicit patches, assigns deterministic canonical IDs, writes a lockfile, then signs or hashes the compiled closure. MegaMod validates closure/hash and loads it in topological order. Save games pin a compatible lockfile; join handshake compares gameplay-relevant package hashes and schema digest. Cosmetic-only differences can be classified later after a provable rule exists.

**Import feasibility:** OAL can someday read legally obtained Source/GMod, Arma, Bethesda, PZ, SE, or RFG assets only through per-format importer research, rights checks, and normalization. The target format must represent units/axes, mesh/material semantics, skeletons/animation roles, attachments, collision, navigation, world entity IDs, dependency references, and provenance. A source mod package is never automatically a safe MegaMod mod: foreign executable scripts should not be run or translated blindly.

**First proof:** package two original assets plus one scripted rule, lock dependencies, validate one missing asset and one conflicting ID, load on Linux and Android, reject a mismatched peer, and compare deterministic output hashes from two OAL builds. Retain OALMAP/OALASSET compatibility throughout.
