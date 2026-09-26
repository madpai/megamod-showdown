# Red Faction: Guerrilla

**Evidence/version:** community [RF:G editing index](https://www.redfactionwiki.com/wiki/RF:G_Editing_Main_Page), [map organization](https://www.redfactionwiki.com/wiki/RF:G_Map_organization), [Nanoforge](https://github.com/rfg-modding/Nanoforge), [SyncFaction](https://github.com/rfg-modding/SyncFaction), and [Reconstructor](https://github.com/rfg-modding/Reconstructor), read 2026-09-26. This is tool/file-semantic and black-box architecture research; **the Geo-Mod implementation was not accessed**. Licenses vary among community tools (Reconstructor is MPL-2.0); the game and data are proprietary. No code or assets copied.

## Architecture and content

**Fact.** Community documentation identifies package archives (`.vpp_pc`, `.str2_pc`), XML-like gameplay tables (`.xtbl`), map zone data, and toolchains for browsing/editing them. [Nanoforge](https://github.com/rfg-modding/Nanoforge) reports partial map viewing/editing, mesh/texture tools, table validation, and mod-manager package generation; its published [zone model](https://github.com/rfg-modding/Nanoforge/blob/master/Nanoforge/Rfg/Zone.cs) separates a list of zone objects from terrain. [Map organization](https://www.redfactionwiki.com/wiki/RF:G_Map_organization) itself flags some loading details as unknown. These resources do not expose the proprietary engine/runtime split, entity lifetime rules, networking protocol, or destruction algorithm.

**Inference.** The relevant architectural pattern is a data-rich authoring pipeline plus runtime zone streaming. OAL could someday parse lawfully obtained assets into generic world/mesh/material/structural representations, but each source format needs a separate rights and feasibility assessment. MegaMod should never need to understand `.xtbl` or `.vpp_pc` at runtime.

## World, physics, and destruction

**Fact.** RF:G's observed signature is breakable structures and physics debris in authored worlds; community file documentation covers zones and object/mesh assets. It does **not** prove that the engine stores `StructuralNodes + Connections + SupportRules` or any particular constraint graph. Terrain destruction and building destruction must not be conflated.

**Inference/hypothesis to test.** A MegaMod-native destructible could be an authored render mesh plus simplified fracture pieces, a support/connectivity graph, per-piece collision bodies, and a bounded debris policy. OAL could preprocess mesh segmentation and support hints; the host would own break decisions and replicate compact break events/state, while clients create cosmetic debris. This is exploratory, not an immediate architecture commitment. A mobile budget requires hard caps on active bodies, pieces, collision complexity, and network events.

## Scripting, networking, packages, tooling

**Fact.** [Reconstructor](https://github.com/rfg-modding/Reconstructor) explicitly reports that an earlier broad Lua binding exposed many fields with little proven use and increased maintenance cost; its rewrite intends a smaller selective scripting API. Its current README says scripting is not yet implemented in that rewrite. [SyncFaction](https://github.com/rfg-modding/SyncFaction) focuses on updates, mod discovery, file replacement/resource editing, and multiplayer usability. The reviewed sources do not specify authoritative physics replication, prediction, interpolation, or mod mismatch semantics. Community archive/tool documentation is not an official SDK contract.

**Inference.** MegaMod should expose only useful validated Lua verbs and leave destruction physics in C. Package manifests should make content hashes and dependency closure inspectable before joining. OAL's preview/validation reports can give creators better feedback than runtime crash diagnosis.

## MegaMod conclusions

- **Adopt:** validation and preview as first-class authoring steps; a deliberately small script API; separate gameplay break state from cosmetic debris.
- **Consider later:** OAL-generated structural graph/fracture candidates and streamed world zones.
- **Avoid:** claiming knowledge of proprietary Geo-Mod internals, copying game file semantics into runtime, dynamic arbitrary mesh fracture on Android, unbounded networked debris, broad auto-bound Lua APIs.
- **For Open Asset Lab:** exploratory legal/format feasibility for archives and zones; normalized structural metadata only after an original-content prototype proves the representation.
- **Prototype:** one original small bridge/wall mesh with hand-authored supports. OAL validates the graph; host detaches a bounded piece after damage and replicates one break event. Measure frame time and packet size on Android.
