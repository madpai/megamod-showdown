# Comparative engine and mod architecture research

Research date: **2026-09-26**. This corpus informs [MegaMod's vision](../MEGAMOD_VISION.md) and [Open Asset Lab's vision](https://github.com/madpai/open-asset-lab/blob/main/docs/ASSET_LAB_VISION.md). It is architectural research, not an implementation plan or permission to reuse another project's code or game assets. The local MegaMod checkout is `halo-sandbox` in `halo-trial-android`, tracking `madpai/megamod-showdown`.

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
| [MegaMod recommendations](MEGAMOD_RECOMMENDATIONS.md) | COMPLETE as research; architecture decisions remain proposed | NOW / NEXT / LATER / EXPERIMENTAL / AVOID |

`COMPLETE` means the documented comparison and recommendation are present, **not** that every upstream implementation is exhaustively audited. Follow-ups mark evidence that cannot yet support a stronger claim. No importer, Lua runtime, registry, network transport, or package format was implemented as part of this research.

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

## Local baseline used for recommendations

The [MegaMod vision](../MEGAMOD_VISION.md) describes **protocol v9**, host-authoritative LAN UDP, per-source rate limiting, interpolation, and feature-specific packets; the older [network progress](../NETWORK_PROGRESS.md) has archived v7/v2 snapshots and must not be treated as current. `hta_unit`, `hta_prop`, and `hta_vehicle` work; the match loop is being moved out of Android incrementally. Open Asset Lab currently emits bounded [OALMAP v1/v2 and OALASSET v1](https://github.com/madpai/open-asset-lab/blob/main/docs/RUNTIME_PACKAGE.md); its source-family importers already preserve provenance and report unsupported features. This corpus recommends experiments that fit that baseline and the existing [minimal-refactor rule](../MEGAMOD_VISION.md#21-do-not-over-refactor).
