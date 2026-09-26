# Project Zomboid

**Evidence/version:** official [Build 42.20 feature overview](https://projectzomboid.com/blog/features-overview-build-42-20/); community [Coco Labs modding guide](https://github.com/cocolabs/pz-modding-guide), [ZomboidDoc](https://github.com/cocolabs/pz-zdoc), [ZomboidMod](https://github.com/cocolabs/pz-zmod), and [PZ Wiki Modding API docs](https://github.com/PZ-Wiki-Modding/PZ-API-Docs), read 2026-09-26. Some tooling predates Build 42. The PZ wiki was blocked by robots for this research; Build 42 API details need a version-pinned follow-up. **License:** the game/Java implementation is proprietary. Community tooling licenses do not grant reuse rights over it; no game code was decompiled or copied.

## Architecture and entities

**Fact.** The game exposes a Lua modding layer over a Java runtime. Its community tools generate Lua-facing API information from installed Java classes, reflecting a large native/managed boundary. Build 42's [official overview](https://projectzomboid.com/blog/features-overview-build-42-20/) states that client visuals run locally while actions execute on the server. Community mod templates separate `media/lua/client`, `server`, and `shared` code. The world is tile/chunk based with persistent simulation state, not a general 3D ECS template.

**Inference.** MegaMod's native C can similarly own world simulation and expose a narrow event API; the lesson is the need for a *deliberate* bridge contract. Java method exposure in PZ is broad and version-sensitive, which MegaMod should avoid by versioning a smaller Lua API. A 3D transform/collider entity model should not inherit PZ's tile assumptions.

## Content and packages

**Fact.** PZ mods use metadata (`mod.info`), media folders, Lua scripts, and separate declarative item/recipe data. [ZomboidMod](https://github.com/cocolabs/pz-zmod) includes tasks to create mod structure, edit metadata, assemble distributions, and inspect API surface. Build 42 has introduced versioned mod packaging changes; exact current `mod.info` dependency and compatibility semantics are not firmly established by the inspected primary sources. The reviewed material does not establish a universal field-level override resolver or hot-unload contract.

**Inference.** OAL should validate package identity, build/runtime compatibility, assets, recipes/definitions, and script realms before publishing. Semantic authoring data should compile into stable MegaMod definitions. Package version mismatch should be checked before a client enters a match, rather than relying only on a Workshop item ID.

## Events, scripting, and persistence

**Fact.** PZ's Lua `Events` callback pattern is documented by its community API; modders use it to react to simulation stages instead of editing every Java behavior. Community tooling provides code navigation and generated API stubs because bridge discoverability is otherwise hard. Persistent world/mod state is part of the modding ecosystem, but exact Build 42 serialization guarantees vary by API and were not established here.

**Inference.** MegaMod should publish machine-readable API signatures, context/authority annotations, and examples with every Lua runtime version. Scripts should receive immutable event snapshots and safe entity handles, then call bounded engine verbs. Persistent script values should have schema/version limits; scripts cannot retain native pointers or depend on callback closure identity after load.

## Networking, world, physics, tooling

**Fact.** Build 42's server-action/client-visual split is an official authority statement. PZ has multiplayer commands and synchronization, but the reviewed public resources do not specify transport reliability, prediction details, physics constraints, or mod mismatch handshake sufficiently to transplant them. Its world persistence and recipes are useful content-model examples, while its isometric tile world is unlike MegaMod's mesh maps.

**Inference.** MegaMod should let client Lua request bounded gameplay intents, have host Lua/C validate them, and replicate resulting state/events. OAL can generate world IDs, item/recipe references, and validation reports. Error reports should include package, file, line, callback, and runtime API version.

## MegaMod conclusions

- **Adopt:** server-authoritative scripted events; separate host/client/shared script roles; generated API docs/stubs; versioned world state.
- **Consider later:** creator IDE integration and structured migration tooling.
- **Avoid:** broad automatic exposure of C internals to Lua, silently mixing client and server authority, assuming Build 41 docs apply to Build 42.
- **For Open Asset Lab:** compile declarative content and check script/API compatibility; preserve provenance and version markers in packages.
- **Prototype:** one server-authoritative pickup/recipe rule with a cosmetic client event, save/load round trip, and a mismatched-package join rejection.
