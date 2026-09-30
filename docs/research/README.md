# Comparative engine and mod architecture research

Research date: **2026-09-26**. This corpus informs the [MegaMod Engine and Showdown vision](../MEGAMOD_VISION.md) and [Open Asset Lab's vision](https://github.com/madpai/open-asset-lab/blob/main/docs/ASSET_LAB_VISION.md). **MegaMod Engine** is the reusable runtime; **MegaMod Showdown** is its official reference game and stress test; **Open Asset Lab** compiles foreign and original content. Future original games consume the Engine independently. The corpus is architectural research, not an implementation plan or permission to reuse another project's code or game assets. The local checkout is `halo-sandbox` in `halo-trial-android`, tracking `madpai/megamod-showdown`.

## Reading order and status

| Report | Status | Main question |
| --- | --- | --- |
| [Space Engineers / VRage](SPACE_ENGINEERS.md) | COMPLETE; version drift NEEDS FOLLOW-UP | Runtime entity, component, builder, public API boundary |
| [Factorio](FACTORIO.md) | COMPLETE | Definition and runtime stages, migrations |
| [Source / Garry's Mod](SOURCE_GMOD.md) | COMPLETE | Game module, entity I/O, scripted addons |
| [Natural Selection](NATURAL_SELECTION.md) | COMPLETE; NS2 installed scripts NEEDS FOLLOW-UP | Native and Lua boundary |
| [tModLoader](TMODLOADER.md) | COMPLETE | Extensible registration and hooks |
| [Arma 3](ARMA3.md) | COMPLETE | Addon dependencies and config inheritance |
| [Project Zomboid](PROJECT_ZOMBOID.md) | COMPLETE; Build 42 details NEED FOLLOW-UP | Java/Lua events and authority |
| [Bethesda / xEdit](BETHESDA_XEDIT.md) | COMPLETE; per-title details NEED FOLLOW-UP | Stable records, masters, overrides |
| [Red Faction: Guerrilla](RED_FACTION_GUERRILLA.md) | COMPLETE for published file/tool semantics; destruction internals NEED FOLLOW-UP | Authoring and structural destruction hypothesis |
| [GameNetworkingSockets](GAME_NETWORKING_SOCKETS.md) | COMPLETE | Transport capabilities versus current LAN protocol |
| [Entity comparison](ENTITY_COMPARISON.md) | COMPLETE | Small C composition model |
| [Scripting comparison](SCRIPTING_COMPARISON.md) | COMPLETE | Host Lua boundary to prototype |
| [Content registration](CONTENT_REGISTRATION.md) | COMPLETE | Definition registry and conflicts |
| [Package and mod format](PACKAGE_MOD_FORMAT.md) | COMPLETE | Manifest and dependency design |
| [Cross-engine synthesis](CROSS_ENGINE_SYNTHESIS.md) | COMPLETE | Decisions by problem |
| [Universal Modder workflows](UNIVERSAL_MODDER.md) | STUDIED; adapted skills and native project verification implemented | Source-of-truth research, reproducible asset preparation and runtime evidence |
| [Local library mashups / Oblivion](LOCAL_LIBRARY_MASHUPS.md) | LOCAL SCAN; three-model private static proof passed, supported importer still future work | Installed donors, concrete crossover ideas, measured BSA/NIF evidence and next content slice |
| [MegaMod recommendations](MEGAMOD_RECOMMENDATIONS.md) | COMPLETE as research; architecture decisions remain proposed | NOW / NEXT / LATER / EXPERIMENTAL / AVOID |
| [Current boundary review](CURRENT_RUNTIME_CONTENT_BOUNDARY_REVIEW.md) | N1 SOURCE OBSERVATION; current code, not a replacement for Claude's inventory | Which fields and names currently cross OAL/MegaMod and LAN boundaries? |
| [Content ID grammar](CONTENT_ID_GRAMMAR_RECOMMENDATION.md) | N2 READ-ONLY AUDIT LANDED; no format migration | OAL-audited package-owned syntax and migration rules |
| [World-event slice](WORLD_EVENT_SLICE_RECOMMENDATION.md) | X1 IMPLEMENTED ([WORLD_ENTITIES](../WORLD_ENTITIES.md)); note carries the evidence | Minimum host event, handle, collision and snapshot contract |
| [Dense-index hazards](DENSE_INDEX_HAZARDS.md) | N1 FOLLOW-UP; reconciled with v10 | Which other local positions can cross peers? |
| [V10 fingerprint review](V10_FINGERPRINT_ARCHITECTURE_REVIEW.md) | LANDED IMPLEMENTATION REVIEW | Actual compatibility scope, float and world-key limits |
| [X1 original authoring path](X1_ORIGINAL_AUTHORING_PATH.md) | X1 IMPLEMENTED as recommended (B: programmatic OAL world) | Small original fixture through OAL validation/compiler |
| [Post-N3 order](POST_N3_ORDER_DECISION.md) | DECISION MEMO | X1 using current tools, then full Step 5 |
| [Halo reconstruction comparison](halo-reconstruction/README.md) | RESEARCH ONLY; Xbox 2342 versus Trial needs validation | Halo behavior evidence, ancestry audit, universal-port lessons and independent tests |

`COMPLETE` means the documented comparison and recommendation are present, **not** that every upstream implementation is exhaustively audited. Follow-ups mark evidence that cannot yet support a stronger claim. This research pass itself implemented no importer, Lua runtime, registry, network transport, or package format; later X1–X9 work landed separately (see [HANDOFF](../HANDOFF.md#current-testing-objective)).

## Mechanic and capability research [Someday direction]

Comparative research may study a narrow mechanic as well as a complete
engine. Skate locomotion and trick recognition, arcade or high-speed hover
handling, parkour, boost/drift systems, rapid party-round reset, physics
hazards and survival directors are **possible future subjects**, not
completed studies. Before proposing a major [Gameplay Capability Module](../GAMEPLAY_CAPABILITY_MODULES.md),
search and read the relevant reports above and current production evidence
such as [Night Shift](../night_shift/README.md). Write a focused new note
only for the unresolved question, retaining **Fact / Inference / Unknown**.
A donor project is research evidence, not a runtime dependency; observing
its architecture gives no permission to copy its code or assets. Check the
specific license and GPLv3 compatibility before reuse. Generic MegaMod
Engine names and contracts should lose donor-game terminology, and the
result should support original content too.

## Method and evidence key

- **Fact** means the cited official API, repository source, creator documentation, or named community tool directly supports the statement. **Inference** means our architectural interpretation, design proposal, or a behavior deduced from those facts. **Unknown** marks a question the reviewed sources do not settle.
- Published source was read only where legitimately available: selected Space Engineers `MyEntity`/component files, Source SDK entity files, tModLoader `ModType`/`ModSystem`, original Natural Selection game-rule source and release terms, xEdit `wbImplementation.pas`, and Nanoforge's zone model. API documentation was preferred when it expresses the contract more clearly.
- Primary sources precede community documentation. Project Zomboid and Red Faction need community references because their target game internals are not an open engine specification. No game binary was decompiled for this corpus; NS2 installed Lua scripts were not inspected.
- Upstream web pages can change. The versions below describe what the pages exposed on the research date, not a claim that every engine release matches a source snapshot. Do not copy restricted source code into MegaMod or Asset Lab.

## Resource index and license boundary

| System | Reviewed resources and version/snapshot | Source-use boundary |
| --- | --- | --- |
| Space Engineers | [published source snapshot](https://github.com/KeenSoftwareHouse/SpaceEngineers), [EULA](https://github.com/KeenSoftwareHouse/SpaceEngineers/blob/master/EULA.txt), [ModAPI](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/), including [gateway](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/Sandbox.ModAPI.MyAPIGateway.html) and [entity builder](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/VRage.ObjectBuilders.MyObjectBuilder_EntityBase.html); archived snapshot, ModAPI site accessed 2026-09-26 | EULA restricts reuse to game-related, generally noncommercial development; **architecture only** |
| Factorio | [official Lua API](https://lua-api.factorio.com/latest/), [data lifecycle](https://lua-api.factorio.com/latest/auxiliary/data-lifecycle.html), [mod structure](https://lua-api.factorio.com/latest/auxiliary/mod-structure.html), [migrations](https://lua-api.factorio.com/latest/auxiliary/migrations.html); docs displayed **2.1.20** | API ideas only; game/assets remain proprietary |
| Source and GMod | [Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013) and [license](https://github.com/ValveSoftware/source-sdk-2013/blob/master/LICENSE), [Facepunch wiki](https://wiki.facepunch.com/gmod/), [entity I/O example](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/doors.cpp); repository and wiki accessed 2026-09-26 | Source SDK 1 license is noncommercial/game-specific; **no code reuse** |
| Natural Selection | [original NS source and exclusions](https://github.com/unknownworlds/NS), [original game-rule source](https://github.com/unknownworlds/NS/blob/master/main/source/mod/AvHGamerules.cpp), [NS2 community modding reference](https://naturalselection.fandom.com/wiki/Modding), [Unknown Worlds forum entity discussion](https://forums.unknownworlds.com/discussion/133818/how-to-properly-register-instance-a-new-custom-entity-class-server-createentity-returning-nil); accessed 2026-09-26 | NS GPL portion excludes Valve SDK and named libraries; NS2 game scripts are not generally open licensed |
| tModLoader | [source and MIT license](https://github.com/tModLoader/tModLoader), [ModType source, 1.4.5 branch](https://github.com/tModLoader/tModLoader/blob/1.4.5/patches/tModLoader/Terraria/ModLoader/ModType.cs), [ModSystem source](https://github.com/tModLoader/tModLoader/blob/1.4.5/patches/tModLoader/Terraria/ModLoader/ModSystem.cs), [API docs, v2026.07 displayed](https://docs.tmodloader.net/docs/stable/); accessed 2026-09-26 | MIT applies to tModLoader repository, not Terraria game content; architecture only here |
| Arma 3 | [Bohemia addon guide](https://community.bistudio.com/wiki/Arma_3:_Creating_an_Addon), [CfgPatches](https://community.bistudio.com/wiki/CfgPatches), [config inheritance](https://community.bistudio.com/wiki/Class_Inheritance), [multiplayer locality](https://community.bistudio.com/wiki/Multiplayer_Scripting); accessed 2026-09-26 | Documentation is reference; game assets/tools carry their own terms |
| Project Zomboid | [official Build 42.20 overview](https://projectzomboid.com/blog/features-overview-build-42-20/), [community guide](https://github.com/cocolabs/pz-modding-guide), [ZomboidDoc](https://github.com/cocolabs/pz-zdoc), [ZomboidMod](https://github.com/cocolabs/pz-zmod), [PZ API docs](https://github.com/PZ-Wiki-Modding/PZ-API-Docs); Build 42 stable reported by official 2026 posts, older tooling may target earlier builds | Java game implementation is proprietary; tooling licenses do not license game code |
| Bethesda / xEdit | [xEdit source, MPL-2.0](https://github.com/TES5Edit/TES5Edit), [record/master implementation](https://github.com/TES5Edit/TES5Edit/blob/dev/wbImplementation.pas), [xEdit docs](https://github.com/TES5Edit/docs/blob/master/_pagebuilder/8-managing-mod-files.txt), [Creation Kit file documentation](https://ck.uesp.net/wiki/File_menu); accessed 2026-09-26 | xEdit license applies to xEdit, not Bethesda formats, assets, or game code |
| Red Faction: Guerrilla | [Nanoforge](https://github.com/rfg-modding/Nanoforge) and [zone model](https://github.com/rfg-modding/Nanoforge/blob/master/Nanoforge/Rfg/Zone.cs), [Reconstructor, MPL-2.0](https://github.com/rfg-modding/Reconstructor), [community file index](https://www.redfactionwiki.com/wiki/RF:G_Editing_Main_Page), [map organization](https://www.redfactionwiki.com/wiki/RF:G_Map_organization); accessed 2026-09-26 | Tool licenses vary; proprietary game data and Geo-Mod internals are excluded |
| Networking | [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets), [public message types](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/steamnetworkingtypes.h); accessed 2026-09-26 | Check repository and third-party licenses before any adoption; this report proposes none |

## 2026-09-26 local baseline used for recommendations

At the time of this research pass, the [MegaMod vision](../MEGAMOD_VISION.md) and [content contract](../CONTENT_COMPATIBILITY.md) described **protocol v10**, host-authoritative LAN UDP, map and imported-roster compatibility checks, rate limiting and feature-specific packets; the older [network progress](../NETWORK_PROGRESS.md) has archived v7/v2 snapshots. `hta_unit`, `hta_prop`, and `hta_vehicle` worked; the match loop ran through shared `hta_session_tick`. Open Asset Lab emitted bounded [OALMAP v1/v2 and OALASSET v1](https://github.com/madpai/open-asset-lab/blob/main/docs/RUNTIME_PACKAGE.md) and had a read-only [ID audit](https://github.com/madpai/open-asset-lab/blob/main/docs/CONTENT_IDS.md). The [N1 source review](CURRENT_RUNTIME_CONTENT_BOUNDARY_REVIEW.md) remains historical evidence for why the v10 boundary matters. X1–X9 and protocol v11 landed afterward; consult the [current handoff](../HANDOFF.md#current-testing-objective) and milestone documents before applying these recommendations under the existing [minimal-refactor rule](../MEGAMOD_VISION.md#21-do-not-over-refactor).
