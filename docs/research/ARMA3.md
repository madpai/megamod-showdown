# Arma 3

**Evidence/version:** Bohemia's [Creating an Addon](https://community.bistudio.com/wiki/Arma_3:_Creating_an_Addon), [CfgPatches](https://community.bistudio.com/wiki/CfgPatches), [config inheritance](https://community.bistudio.com/wiki/Class_Inheritance), [CfgFunctions](https://community.bistudio.com/wiki/CfgFunctions), [event handlers](https://community.bistudio.com/wiki/Arma_3:_Event_Handlers), and [multiplayer scripting](https://community.bistudio.com/wiki/Multiplayer_Scripting), read 2026-09-26. These are living wiki pages, not a pinned game build. **License:** Bohemia's documentation and tools are reference material; no game implementation or assets are copied.

## Architecture and content

**Fact.** Arma separates engine functionality, declarative `config.cpp` class trees, and SQF functions/scripts. The addon guide says an addon without `config.cpp` is ignored; `CfgPatches` identifies the addon, `requiredAddons[]`, and supplied unit/weapon content. `CfgVehicles`, `CfgWeapons`, `CfgAmmo`, and related classes describe many game objects. A [master config](https://community.bistudio.com/wiki/Class_Inheritance) is assembled from base and addon configs, with class inheritance and patching. This makes a huge catalog configurable without a native class for every weapon or vehicle, but also creates order and inheritance hazards.

**Inference.** MegaMod should use typed declarative definitions for characters, weapons, projectiles, vehicles, factions, materials, abilities, and world entities. OAL should compile inheritance *away* into a resolved definition per ID, preserving provenance and an explanation of inherited fields. At runtime, C sees flattened data plus a behavior ID, not an Arma-style master config.

## Entities, world, and lifetime

**Fact.** Config classes define entity kinds; runtime objects are created in missions/editor and manipulated by script commands. Eden Editor, mission files, event handlers, triggers, and waypoints provide world authoring. Runtime object locality matters for script execution and replication. The reviewed docs do not define a general ECS model, a universal object teardown contract, or one collision/constraint representation.

**Inference.** MegaMod should separate authored map placement from runtime instance state. OAL can compile world placements, trigger volumes, event links, nav hints, and prevalidated collider references. A host-created runtime ID survives until explicit destroy; static map IDs can remain stable across saves.

## Scripting, networking, persistence

**Fact.** [CfgFunctions](https://community.bistudio.com/wiki/CfgFunctions) registers tagged SQF functions from addon or mission paths. [Event handlers](https://community.bistudio.com/wiki/Arma_3:_Event_Handlers) respond to creation, damage, death, and other events. [Multiplayer scripting](https://community.bistudio.com/wiki/Multiplayer_Scripting) documents *locality*, including objects whose owning machine changes. This is more flexible and complex than MegaMod's current host authority. Script persistence is mission/save dependent; the reviewed pages do not establish a universal mod storage schema or package mismatch handshake.

**Inference.** MegaMod should deliberately reject shifting simulation ownership between clients for now. Host Lua gets authoritative events; client presentation scripts get visual events. Packages should declare script entry points and capability sets. The compiler should prohibit circular inheritance and unresolved required addons.

## Physics and tooling

**Fact.** Arma tools pack addons into PBOs; config, model, and script authoring are separate steps. Config files select model and gameplay properties, while the engine handles movement, collision, vehicles, and world simulation. The addon guide mentions tool-time checks and `CfgPatches` load relationships. The inspected sources do not expose enough to recommend Arma's physics or navigation implementation.

**Inference.** OAL should act like a strict config compiler with previews: parse, flatten, resolve dependencies, validate model/skeleton/attachment/material references, and report exact missing assets before packaging. A native Android runtime should not merge and interpret large config class trees at startup.

## MegaMod conclusions

- **Adopt:** manifest-level supplied content and required packages; typed declarative definitions; namespaced functions; authoring/runtime split.
- **Consider later:** limited inheritance in authoring data and mission-style scenario packages, flattened by OAL.
- **Avoid:** global master-config patching, implicit last-wins conflicts, client locality transfer, PBO/SQF compatibility, and huge config trees parsed on phones.
- **For Open Asset Lab:** dependency DAG diagnostics, config flattening with field provenance, preview and validation for world placements and attachment points.
- **Prototype:** two packages where one extends an original weapon definition; OAL emits a flattened result and a field provenance report, then rejects an accidental conflict.
