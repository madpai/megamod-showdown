# Source SDK 2013 / Garry's Mod

**Evidence/version:** Valve's [Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013), especially [base entity](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/baseentity.h) and [door implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/doors.cpp), plus official [Facepunch wiki](https://wiki.facepunch.com/gmod/) read 2026-09-26. Source SDK contains substantial HL2, HL2DM, and TF2 *game-side* code; it is not a complete unrestricted engine release. **License:** [Source 1 SDK license](https://github.com/ValveSoftware/source-sdk-2013/blob/master/LICENSE) restricts use to noncommercial Source-related development. Do not copy its code, names, assets, or scripts into MegaMod.

## Architecture and entities

**Fact.** The SDK separates engine interfaces from server and client game modules. Server `CBaseEntity` implements class-linked map entity creation, save data descriptions, inputs/outputs, physics hooks, and network properties; client classes receive corresponding replicated fields. Subclassing is central. A map's static BSP geometry is distinct from dynamic entities. [Door source](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/doors.cpp) demonstrates class registration, authored key fields, explicit inputs/outputs, and network send data in one game-side class. Entity handles manage references across object lifetime.

**Inference.** MegaMod needs the *contract* behind this: a world file containing generic entity definitions and event links, with runtime instances and safe handles. It does not need Source's C++ class tree or map class names. Translate a button to `Interactable + EventEmitter`, a teleport volume to `Trigger + Teleporter`, a relay to an event router, a breakable wall to `Collider + Health + Breakable`, and a moving platform to a transform driver with a collider.

## Content and world pipeline

**Fact.** Source maps carry entity keyvalues and compiled world data; game DLLs link class names to behavior. Source's asset pipeline is split across authoring/compilation and runtime formats. GMod [Lua folder structure](https://wiki.facepunch.com/gmod/Lua_Folder_Structure) describes addon/gamemode layout and scripted entity/weapon locations. [Scripted entities](https://wiki.facepunch.com/gmod/scripted_ents) and [scripted weapons](https://wiki.facepunch.com/gmod/weapons) register Lua tables by names. Addons can derive from bases and use hooks, but that does not supply a universal safe override policy. Source SDK/Facepunch docs reviewed do not establish a general semantic-version dependency solver.

**Inference.** Asset Lab should parse Source keyvalues into source-specific records, then normalize recognized classes into generic world entities. Unsupported logic should appear in a compatibility report with source location and target link, as the current pipeline already does. Compile event target IDs and check cycles/missing targets offline. Keep runtime world loading source-agnostic.

## Scripting, networking, and authority

**Fact.** GMod's [realms](https://wiki.facepunch.com/gmod/States) isolate server and client Lua; [prediction](https://wiki.facepunch.com/gmod/Prediction) is a separate concern. [Network variables](https://wiki.facepunch.com/gmod/Networking_Usage) and the [net library](https://wiki.facepunch.com/gmod/net) are available for scripted content. Lua hooks and gamemodes let addons change behavior; server/client/shared files declare execution contexts. SDK send tables describe native replicated properties; [entity I/O](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/baseentity.h) connects authored world objects through named inputs and outputs.

**Inference.** MegaMod's host-only gameplay Lua plus client-side presentation is a smaller authority model. A custom entity type should declare a fixed, validated replication schema at startup. Client messages are intentions with explicit permissions, never arbitrary invocations of entity input methods. Predicted movement can follow later; broad predicted Lua behavior would be hard to reconcile.

## Physics and tooling

**Fact.** SDK entities distinguish static brushes, triggers, and movable physics objects; doors and breakables are game entities layered onto engine collision/physics. Constraints and complex physics are engine services, not inferable from a simple map class. Source/GMod creators use map tooling and addon layout; errors appear at map compile, script load, and runtime. This report does not establish complete navigation or Workshop dependency semantics.

**Inference.** OAL should compile static collision, trigger volumes, moving collision anchors, and navigation hints. Runtime physics should keep static world mesh separate from a bounded dynamic-body pool. Authoring previews should show event links and unsupported logic, not merely geometry.

## MegaMod conclusions

- **Adopt:** generic map event graph, safe entity handles, explicit replication fields, separate server/client script roles, compile-time diagnostics.
- **Consider later:** entity I/O delays, reusable prefab logic, owner prediction for a small set of actions.
- **Avoid:** Source class names in MegaMod packages, unrestricted entity method RPC, broad subclass hierarchies, rebuilding the Source toolchain, copying SDK code.
- **For Open Asset Lab:** build class-to-generic translation with provenance and unresolved-feature reports; emit stable target references and validate them.
- **Prototype:** convert one original/synthetic button → relay → door map plus one teleport trigger; verify save/load and two-client event replication without Source content.
