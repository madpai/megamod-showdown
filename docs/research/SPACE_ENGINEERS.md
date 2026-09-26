# Space Engineers / VRage

**Evidence/version:** [archived source snapshot](https://github.com/KeenSoftwareHouse/SpaceEngineers) and current published [ModAPI reference](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/) read 2026-09-26. Snapshot behavior may differ from live game. **License:** the [EULA](https://github.com/KeenSoftwareHouse/SpaceEngineers/blob/master/EULA.txt) limits source use to Space Engineers-related, generally noncommercial work. No implementation code is reusable here.

## Architecture and lifetime

**Fact.** The source's [`MyEntity`](https://github.com/KeenSoftwareHouse/SpaceEngineers/blob/master/Sources/VRage.Game/Entity/MyEntity.cs) has an entity ID and a component container; position, render, physics, game logic, synchronization, hierarchy, and mod storage are accessible through components. It also has subclasses such as grids and blocks, so the design mixes inheritance with composition rather than pure ECS. [`MyEntities`](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/Sandbox.Game.Entities.MyEntities.html) creates entities from object builders and registers them in the scene; `MyEntity` exposes close/marked-for-close and tiered update needs. A grid is an entity that groups cube blocks; blocks are not automatically interchangeable with a free physics entity. [MyAPIGateway](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/Sandbox.ModAPI.MyAPIGateway.html) exposes session, entities, utilities, and other global services to mods, instead of handing them every internal singleton.

**Inference.** MegaMod's existing `hta_unit`/`hta_prop`/`hta_vehicle` can gain selected capabilities without becoming a universal object hierarchy. Distinguish a stable *definition ID*, a per-world *instance ID*, and an optional *network ID*; Space Engineers' builder/runtime/replication split makes those different roles visible.

## Content, serialization, and world

**Fact.** [`MyObjectBuilder_EntityBase`](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/VRage.ObjectBuilders.MyObjectBuilder_EntityBase.html) is a serialized entity representation, while runtime `MyEntity` is instantiated from builders. The [definition manager](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/Sandbox.Definitions.MyDefinitionManager.html) indexes game definitions using typed IDs. Runtime entities can be saved; entity components can carry mod storage. Grids/blocks and voxel terrain imply different world representations, with physics attached to runtime entities. The snapshot also exposes session components and entity update schedules.

**Unknown.** The inspected public API and snapshot do not prove current mod conflict resolution, current dependency ordering, complete unload rules, or modern multiplayer replication policy. Do not assume live Space Engineers is identical to the archived source. No universal grid-to-MegaMod asset mapping follows from the API.

**MegaMod/OAL implication.** OAL should compile a declarative definition into validated runtime sections; a saved world should store IDs and mutable instance state, not raw authoring objects. OAL can precompute render meshes, collision shapes, material surfaces, attachment points, and optional structural graphs. MegaMod only needs bounded construction, lookup, update, save, and replication mechanisms. World triggers, doors, and lifts should be generic components connected by stable event references, not VRage block classes.

## Scripting and API boundary

**Fact.** Keen's [ModAPI index](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/) explicitly distinguishes the full mod API (`Sandbox.ModAPI`, `VRage.ModAPI`, `VRage.Game.ModAPI`) from restricted programmable-block interfaces (`Sandbox.ModAPI.Ingame`, `VRage.Game.ModAPI.Ingame`). The [programmable-block interface](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/Sandbox.ModAPI.Ingame.IMyProgrammableBlock.html) is a narrower surface than the full [gateway](https://keensoftwarehouse.github.io/SpaceEngineersModAPI/api/Sandbox.ModAPI.MyAPIGateway.html). This is C# scripting/modding, with native/engine-side services behind interfaces, rather than a Lua architecture.

**Inference.** MegaMod should publish a small, versioned host Lua API of verbs and handles; if user-made in-world automation is added, it deserves a still smaller capability set. Do not expose the C session struct, raw pointers, filesystem, UDP socket, or mutable physics internals.

## Networking, physics, and tooling

**Fact.** `MyEntity` has a synchronization component and server position/velocity fields; it marks when an entity is ready for replication. Physics is a component separate from render and position. These are evidence of replication and physics boundaries, **not** evidence of a particular current authority or prediction algorithm. Grids and their blocks have much greater simulation and fracture costs than ordinary props. Published API docs and source files are the authoring/reference tools reviewed; current workshop packaging, navigation, and detailed validation were not established by the inspected sources.

**Inference.** For MegaMod, the host should own physics and send bounded state/events for only relevant dynamic bodies. OAL should validate collider complexity and generate static/dynamic variants. A grid-style editable construction system is far outside the immediate runtime budget.

## MegaMod conclusions

- **Adopt:** separate immutable definitions, runtime instances, saved state, and public API; add capabilities only where a feature needs them; use generation-checked entity handles and explicit close semantics.
- **Consider later:** tiered updates, per-entity mod storage, hierarchical attachments, typed physics constraints.
- **Avoid:** copying source/EULA code, exposing internals as the mod API, universal grids/blocks, and adopting an inheritance-heavy C# architecture in C.
- **For Open Asset Lab:** compile builders into bounded generic sections; validate attachment/skeleton/material/collision metadata and provenance before packaging.
- **Prototype:** a generic breakable door with definition ID, instance ID, transform/collider/health/event capability, save/load state, and host replication. Measure Android memory and update cost.
