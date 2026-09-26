# Natural Selection / Natural Selection 2

**Evidence/version:** [original NS source release and README](https://github.com/unknownworlds/NS), [NS2 modding guide](https://naturalselection.fandom.com/wiki/Modding), [Unknown Worlds forum entity registration discussion](https://forums.unknownworlds.com/discussion/133818/how-to-properly-register-instance-a-new-custom-entity-class-server-createentity-returning-nil), and [NS2 server guidance](https://naturalselection.fandom.com/wiki/Server_Maintenance_Manual), read 2026-09-26. NS2's installed Lua scripts were **not** available locally; exact behavior of modern NS2 builds needs direct licensed-install study. **License:** original NS has a GPL portion, but its README explicitly excludes Valve's Half-Life SDK, FMOD, Lua 5.0, and a particle library from that grant. NS2 game scripts/assets are not assumed reusable. No code copied.

## Architecture and entity model

**Fact.** Original Natural Selection is a Half-Life game modification with published [game-rule source](https://github.com/unknownworlds/NS/blob/master/main/source/mod/AvHGamerules.cpp) containing server-side spawn and update responsibilities. It is historical evidence of a game layer built on an engine; it is not Spark/NS2 source. NS2 mod documentation describes separate client and server Lua entry points and a game setup file. Community [entity registration guidance](https://forums.unknownworlds.com/discussion/133818/how-to-properly-register-instance-a-new-custom-entity-class-server-createentity-returning-nil) uses Lua classes, a unique map name, and declared network variables before the server creates a scripted entity. That supports a substantial scripted game layer over native services. It does **not** prove that every entity or physics operation runs in Lua.

**Inference.** MegaMod should keep C ownership of entity storage/lifetime, transforms, collision, animation, rendering, audio, and wire encoding. Lua may define abilities, weapon behavior, team rules, objectives, trigger responses, scoring, and round flow through handles and verbs. The benchmark is whether one new gameplay rule can ship without recompiling C while a bad script cannot corrupt engine storage.

## Content, packages, and world

**Fact.** The NS2 modding references describe mod directories, `game_setup.xml`, Lua entry files, the game's shipped `ns2/lua` scripts, editor tooling, and Workshop packaging. Maps are authored in Spark Editor. The reviewed materials do not establish a general cross-mod semantic-version resolver or a consistent conflict policy. Map entity names and scripted class registration are coupled at load.

**Inference.** Asset Lab can convert original or imported maps to world/entity definitions while preserving provenance. A MegaMod package should declare content IDs, required packages, script entry points, and explicit allowed runtime capabilities. A map entity's generic definition should be resolved before Lua hooks begin. Navigation, collision, and spawn validity belong in compile-time validation.

## Scripting, networking, persistence

**Fact.** NS2's modding descriptions distinguish client and server Lua and show network-variable declaration for custom Lua classes. [Server guidance](https://naturalselection.fandom.com/wiki/Server_Maintenance_Manual) mentions file consistency hashes for client/server mod files. Exact prediction, RPC guarantees, ownership transfer, script sandbox, and persistence semantics cannot be confirmed from the reviewed public pages alone. Original NS source cannot answer those NS2 questions.

**Inference.** The value for MegaMod is **declared network shape before instances exist**. Let the host execute gameplay Lua and validate client intents; replicate a compact declared set of state and events. Cosmetic client Lua should have no authority and should be optional. Keep script state in a versioned, bounded serialization table rather than arbitrary Lua closures.

## Physics and tooling

**Fact.** NS2 ships an editor and offers logs for Lua errors according to the modding guide. The reviewed documentation does not specify native physics representation, destruction graph, or full navmesh API. A broad Lua gameplay layer increases creator speed but also makes load order, errors, and network declarations part of the content contract.

**Inference.** Asset Lab can static-check declared network fields, event hooks, referenced assets, and client/server entry points; MegaMod still needs runtime limits and clear stack traces naming package and script. An NS2-style broad Lua surface is too expensive and risky as MegaMod's first scripting milestone.

## MegaMod conclusions

- **Adopt:** narrow native primitives with scripted gameplay, separate server/client entry points, declared network state, creator-visible script diagnostics.
- **Consider later:** client cosmetic scripts and map hot reload for local authoring.
- **Avoid:** treating original NS as NS2 implementation evidence, copying excluded/GPL-plus-SDK code, allowing arbitrary script access to engine internals, or assuming every gameplay loop should run in Lua.
- **For Open Asset Lab:** validate script declarations and references; normalize map entities and attachments; record which behavior remains unsupported.
- **Prototype:** move one current special-cased hero ability to host Lua, with C raycast/damage/audio verbs, a declared cooldown field, and replicated effect event. Measure Android frame cost and error reporting.
