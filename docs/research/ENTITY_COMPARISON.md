# Comparative entity architecture

Read with the [individual reports](README.md). `F` = directly documented in cited sources; `I` = MegaMod interpretation. A row compares the *public model*, not hidden internals.

| System | Entity representation and lifetime | Composition / inheritance / hooks | Identity and serialization | Network relevance |
| --- | --- | --- | --- | --- |
| [Space Engineers](SPACE_ENGINEERS.md) | `MyEntity`, subclasses, close flags and scene registry (F) | Entity component container plus subclasses (F) | Definition ID, entity ID, object builder (F) | Sync component/readiness (F); current authority details unknown |
| [Source/GMod](SOURCE_GMOD.md) | Registered map/runtime class, safe handles (F) | Deep native subclasses; Lua ENT/SWEP tables and hooks (F) | Map keyvalues and save descriptors (F) | Send tables, net messages and prediction realms (F) |
| [Factorio](FACTORIO.md) | Named prototype instantiated as runtime API object (F) | Prototype type inheritance and event callbacks (F) | Stable prototype names, `storage`, migrations (F) | Mod state must align on multiplayer load (F); transport hidden |
| [Natural Selection 2](NATURAL_SELECTION.md) | Lua class with unique map name and server creation (community F) | Lua class inheritance and game scripts (community F) | Declared network fields (community F); save semantics unknown | Client/server scripts and custom network vars (community F) |
| [tModLoader](TMODLOADER.md) | Fixed Terraria entity categories plus mod types (F) | C# inheritance and category/global hooks (F) | Mod-owned `FullName`, save/load hooks (F) | Per-type net hooks and mod packets (F) |
| [Arma 3](ARMA3.md) | Config type instantiated in mission/world (F) | Config inheritance and SQF event handlers (F) | Config class names, addon identity and required addons (F) | Locality can change among machines (F) |
| [Project Zomboid](PROJECT_ZOMBOID.md) | Java world/tile objects exposed to Lua (community F) | Java hierarchy plus Lua events (community F) | Script item definitions and persistent world data (community F) | Build 42 server executes actions (official F) |
| [Bethesda/xEdit](BETHESDA_XEDIT.md) | Base form and placed reference (F) | Typed records, references, Papyrus events (F); no ECS evidence | FormID, masters, overrides, save changed state (F) | Multiplayer not relevant to studied plugin model |

## What the comparison says

**Inheritance is useful for authoring taxonomies but expensive as the engine's only extension mechanism.** Source and tModLoader succeed with class hierarchies because their native/managed environments support extensive virtual hooks. MegaMod is C with existing efficient, specialized arrays. A universal `Entity` base struct with dozens of optional function pointers would bring class-system costs without the tooling. Factorio and Arma show that many gameplay variants can be *definitions*, then share a small set of runtime behaviors. Space Engineers demonstrates components alongside specialized entity kinds, rather than replacing every class with a textbook ECS.

**Stable content identity is different from object identity.** Bethesda's FormID/master problems and Factorio's prototype names argue for authoring IDs independent of load order. Space Engineers' runtime entity IDs and object builders argue for separate instance and serialized identities. Network IDs are session-local and can be smaller. A saved world should never depend on an allocator slot or network sequence number.

**Lifecycle needs an explicit owner.** A package owns definitions and callbacks; a world owns placed and spawned instances; the host owns authoritative instance mutation; the renderer/audio systems borrow resources. Destruction must invalidate handles before callbacks can reuse them. Package unload should occur at a match boundary until resource/callback ownership is proven.

## Proposed MegaMod-native model (inference)

1. **Definition registry at startup:** OAL-compiled, immutable `kind:namespace.name` IDs. Resolve references and dependency closure once; assign dense type-local numeric indices for C tables.
2. **Runtime instance registry:** keep `hta_unit`, `hta_prop`, `hta_vehicle`, projectiles, and world objects in their specialized pools. Add a tiny common handle (`kind`, `slot`, `generation`) and a lookup/validation function. Do not move hot movement arrays into a generic allocator.
3. **Capabilities for cross-cutting behavior:** start with `Transform`, `Collider`, `Health/Breakable`, `Interactable/Trigger`, `EventEmitter/Receiver`, and `NetworkReplicated`, only when a real world-entity slice needs them. A capability can be a typed index into its own compact pool, not heap-allocated component objects.
4. **Behavior:** fixed C systems advance physics/collision/animation/net; optional host Lua handlers run at declared events and call bounded verbs. No per-entity `Update` callback unless profiling proves it affordable.
5. **State boundaries:** `definition` is immutable; `instance state` contains health/open/position/etc.; `save record` is versioned; `replication schema` is declared. Never serialize C memory layouts or Lua closures.

### First proof

Implement a synthetic original-content map containing one door, button, trigger, and breakable. Use existing collision/prop pathways where possible. The door's event links should work without a new C subclass; stale handles must be rejected; save/load and host replication must preserve behavior. Measure entity lookup, active update count, memory, and Android frame time against current code. See [recommendations](MEGAMOD_RECOMMENDATIONS.md).
