# Cross-engine synthesis: smallest useful architecture

This synthesis groups **observed patterns** and **MegaMod inferences** by problem. The individual reports hold citations, license notes, and unknowns. No other engine's source or data is a code donor.

## Entity model

**Observed:** [Space Engineers](SPACE_ENGINEERS.md) combines entities/components with specialized grid/block classes; [Source](SOURCE_GMOD.md) uses entity subclasses and map I/O; [Factorio](FACTORIO.md) separates prototypes from runtime objects; [tModLoader](TMODLOADER.md) registers mod-owned category hooks. **Inference:** retain MegaMod's specialized C pools, add generation-checked common handles and a few cross-cutting capability pools where needed. Keep authoring definitions separate from runtime instance state. A wholesale ECS rewrite has no supporting performance or product requirement.

## Scripting

**Observed:** Factorio has a strict startup/runtime boundary; [NS2](NATURAL_SELECTION.md), [GMod](SOURCE_GMOD.md), and [PZ](PROJECT_ZOMBOID.md) put substantial gameplay in Lua with client/server distinctions; [Space Engineers](SPACE_ENGINEERS.md) has two different public API trust levels; [Reconstructor](RED_FACTION_GUERRILLA.md) documents the cost of exposing too much. **Inference:** host Lua should compose C verbs for abilities, weapons, triggers, rules, and scoring. A declared event/state schema, safe handles, limited APIs, quotas, and exact error attribution matter more than maximizing Lua coverage.

## Content identity and registration

**Observed:** Factorio uses named prototypes, [Arma](ARMA3.md) exposes config classes plus addon dependencies, [Bethesda/xEdit](BETHESDA_XEDIT.md) depends on FormIDs/masters and pays heavily for implicit overrides, and tModLoader records the owning mod in a full name. **Inference:** stable namespaced authoring IDs plus immutable startup registries and dense runtime indices. Explicit patch packages only after a real need; no load-order-derived identity or silent last-wins.

## Networking and replication

**Observed:** Source/GMod expose replication tables, prediction, and separate realms; Space Engineers' snapshot shows sync components; NS2 community docs declare custom network vars; [GameNetworkingSockets](GAME_NETWORKING_SOCKETS.md) solves transport delivery/connection problems, not gameplay authority. **Inference:** preserve MegaMod's v9 host-authoritative UDP protocol; declare schemas for modded entity state, compare content hashes at join, send bounded intents from clients, and separate lossy snapshots from reliable state transitions. Profile Internet needs before a transport replacement.

## Mod dependencies and package conflicts

**Observed:** Factorio orders mods by dependency/name and has startup update/final-fix rounds; Arma `CfgPatches` states requirements; Bethesda masters and xEdit make overrides inspectable; tModLoader owns registration lifecycle. **Inference:** OAL resolves a dependency DAG and compiles a lockfile. New content under own namespace is straightforward; changes to another package require explicit typed patches and conflict reporting. Runtime only loads a resolved closure.

## Physics and destruction

**Observed:** Space Engineers attaches physics to runtime entities and handles grids; Source distinguishes world brushes/triggers/movable physics; RF:G community tools show destruction-oriented authored data but not Geo-Mod internals. **Inference:** keep static world collision separate from bounded dynamic bodies. Host decides structural damage; clients may create cosmetic debris. Asset Lab can preprocess collision, fracture candidates, and support graphs, but a structural graph is a hypothesis pending an original-content prototype.

## World architecture

**Observed:** Source entity I/O links map objects; Arma missions/editor place runtime objects; Bethesda records distinguish base forms and world references; Factorio runtime instances refer to prototypes. **Inference:** OAL compiles world placements with stable authored IDs, generic capabilities, target links, and validator diagnostics. MegaMod instantiates them, advances transforms/physics, dispatches events, and stores mutable state. Triggers, doors, lifts, and teleports become data-defined combinations, not imported engine class names.

## Content pipeline and authoring tools

**Observed:** Factorio validates prototypes; Arma compiles configs/addons; xEdit visualizes references/conflicts; Nanoforge validates tables and previews maps; Source separates map authoring from compiled runtime data. **Inference:** OAL is the key simplifier. It can normalize source formats, units, materials, skeletons/attachments, animations, collision/nav, and eventually structural data; resolve dependencies; validate schemas; and emit provenance. MegaMod can then remain a small loader, renderer, simulation, and networked runtime. Runtime should still bounds-check all packages because compiled content can be corrupted or malicious.

## Trade-off summary

| Choice | Why it fits MegaMod | Cost/constraint |
| --- | --- | --- |
| Specialized C pools + limited capabilities | Preserves hot paths and existing behavior | Needs a clear handle and ownership contract |
| Offline compiled definitions | Less phone startup/validation work; reproducible content | OAL/compiler/runtime schema coordination |
| Host Lua for gameplay | New behavior without C rebuild; authority stays simple | Sandboxing, quotas, debugging, state versioning |
| Explicit dependencies and patches | Predictable packages and saves | Creator tooling required for diagnostics |
| Declared network schema | Safe modded replication and mismatch detection | Schema evolution and bandwidth limits |
| Optional structural preprocessing | Mobile work shifted offline | Requires quality/performance evidence before adoption |

The [recommendations](MEGAMOD_RECOMMENDATIONS.md) prioritize experiments around current limitations. The [Asset Lab companion map](https://github.com/madpai/open-asset-lab/blob/main/docs/RESEARCH_CONNECTIONS.md) describes compiler consequences.
