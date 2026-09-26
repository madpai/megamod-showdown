# Terraria / tModLoader

**Evidence/version:** [tModLoader repository](https://github.com/tModLoader/tModLoader) (MIT licensed), [1.4.5 `ModType` source](https://github.com/tModLoader/tModLoader/blob/1.4.5/patches/tModLoader/Terraria/ModLoader/ModType.cs), [1.4.5 `ModSystem` source](https://github.com/tModLoader/tModLoader/blob/1.4.5/patches/tModLoader/Terraria/ModLoader/ModSystem.cs), [API documentation showing v2026.07](https://docs.tmodloader.net/docs/stable/), read 2026-09-26. Terraria game content has separate rights; this report reuses no source.

## Architecture, entities, lifetime

**Fact.** `ModType` records its owner mod and name, exposes a `FullName` incorporating both, and has distinct `Load`, `Register`, `SetupContent`, and `Unload` stages. Its templated forms construct an underlying game entity and optionally create a new mod-type instance per runtime entity. `ModItem`, `ModNPC`, `ModProjectile`, and `ModPlayer` attach hooks/behavior to established Terraria categories; `ModSystem` hosts global/world lifecycle hooks. This is an inheritance-and-hooks design layered on a fixed engine data model, not textbook ECS.

**Inference.** Hundreds of independent mods can participate because the loader discovers registered types and dispatches hooks for each category; the engine need not know each mod's concrete subclass. MegaMod should take the *registry and lifecycle* idea, not the C# subclass architecture. Compact C capability masks and Lua callbacks can cover the initial categories.

## Content, packages, and versioning

**Fact.** [ModItem API](https://docs.tmodloader.net/docs/stable/class_mod_item.html) documents automatic registration of derived classes. [`ModType`](https://docs.tmodloader.net/docs/stable/class_mod_type.html) and source distinguish template/instance behavior and static setup. [ModSystem API](https://docs.tmodloader.net/docs/stable/class_mod_system.html) provides content setup, world load/unload, and serialization hooks. tModLoader packages mod code and assets as a mod and uses a mod loader with dependency/version metadata; exact resolution semantics need a dedicated loader audit. Hook dispatch order and conflict behavior should not be inferred from class names alone.

**Inference.** MegaMod should define `CharacterDefinition`, `WeaponDefinition`, `ProjectileDefinition`, `AbilityDefinition`, `VehicleDefinition`, `GameRule`, and `WorldSystem` as **data schemas**, with optional script behavior IDs. Registry entries should include the owning package and validate referenced IDs. Systems should be allowed to subscribe only to named events, not to monkey-patch every engine function.

## Scripting, networking, persistence

**Fact.** tModLoader uses C# code for mod behavior, and game engine/native code remains below it. `ModSystem` source includes world lifecycle and save/load hooks; it validates that save/load and net-send/net-receive methods are overridden in pairs. [ModItem](https://docs.tmodloader.net/docs/stable/class_mod_item.html) exposes custom `NetSend`/`NetReceive`, and [ModPacket](https://docs.tmodloader.net/docs/stable/class_mod_packet.html) permits mod messages. The API documents which hooks run on clients or servers, but tModLoader does not imply a single universal authority policy for every hook.

**Inference.** Pair validation is useful: if MegaMod defines persistent or replicated custom state, require both encode and decode schema validation at package build. User-authored wire bytes should not be sent directly from Lua; declare state/event fields and let C serialize bounded values.

## World, physics, tooling

**Fact.** Terraria's world is tile-centric, with NPCs/items/projectiles in fixed categories and global hooks; it is not a general 3D rigid-body reference. `ModSystem` has world load and update points and can store world data. ExampleMod and API docs act as tooling/templates. This has limited bearing on MegaMod collision, navigation, constraints, or destruction.

**Inference.** OAL can offer category-specific authoring forms and validation, but generic 3D content must stay independent of Terraria's categories. Provide error messages that identify package, definition, field, and conflicting mod.

## MegaMod conclusions

- **Adopt:** owned, named registration; clear load/setup/unload phases; category-specific definitions; explicit world/system hooks; paired save/network schema checks.
- **Consider later:** mod-added global systems and conditional content loading.
- **Avoid:** inheritance-heavy engine objects, arbitrary C# runtime dependency, unbounded hook counts, raw mod-authored packet serialization.
- **For Open Asset Lab:** build registries and field-level validation from schemas, then generate creator-facing docs/examples and a content inventory.
- **Prototype:** register two independent abilities and a world rule from separate packages, unload both between matches, and verify no stale callbacks or IDs remain.
