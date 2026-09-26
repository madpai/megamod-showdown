# Factorio

**Evidence/version:** official [Lua API 2.1.20](https://lua-api.factorio.com/latest/), [data lifecycle](https://lua-api.factorio.com/latest/auxiliary/data-lifecycle.html), [mod structure](https://lua-api.factorio.com/latest/auxiliary/mod-structure.html), [prototype API](https://lua-api.factorio.com/latest/index-prototype.html), [runtime API](https://lua-api.factorio.com/latest/index-runtime.html), and [migrations](https://lua-api.factorio.com/latest/auxiliary/migrations.html), read 2026-09-26. **License:** documentation describes an API; Factorio game implementation/assets are proprietary. Architectural concepts only.

## Architecture: three explicit stages

**Fact.** Settings prototypes are built at startup; the prototype/data stage then builds definitions before a map exists; runtime/control Lua receives events and manipulates instantiated game objects. In each startup stage, mods run ordered `stage.lua`, `stage-updates.lua`, and `stage-final-fixes.lua` rounds in a shared Lua state. The stage state is discarded. A loaded save runs control code in per-mod state, with `on_init`, migrations, `on_load`, and configuration-change hooks under documented conditions. Prototypes are no longer mutable in control. This is the strongest direct precedent for MegaMod's proposed **content build → immutable definitions → host gameplay** boundary.

**Inference.** OAL can do far more of the data stage offline than Factorio can: parse foreign assets, normalize materials/skeletons, generate collision/nav hints, resolve dependencies, and validate declarations. MegaMod should consume compiled definitions and execute host Lua only for runtime decisions. A limited creator-facing build step may eventually compose definitions, but running arbitrary imported Lua in Asset Lab is unnecessary.

## Entities and content registration

**Fact.** Prototype types use documented fields and inheritance in the [prototype API](https://lua-api.factorio.com/latest/index-prototype.html); mods register or amend named prototypes through the data table. The [mod structure](https://lua-api.factorio.com/latest/auxiliary/mod-structure.html) specifies `info.json`, dependencies, assets, startup files, control, and migrations. The [lifecycle](https://lua-api.factorio.com/latest/auxiliary/data-lifecycle.html) defines ordering from dependencies plus internal names and tracks which mod changed a prototype. Runtime [API](https://lua-api.factorio.com/latest/index-runtime.html) exposes read-only prototypes, object handles, `script` events, `remote` cross-mod calls, and persistent `storage`.

**Inference.** MegaMod IDs should be namespaced strings at authoring time, resolved to compact numeric runtime IDs at load. Definitions should be immutable once a match begins. A package may *propose* a typed patch to a dependency's definition at build time, with explicit target/version and provenance; OAL should surface a conflict if multiple patches touch the same field. A universal silent final-fixes phase would make reproducibility harder and is unnecessary at first.

## Scripting, persistence, networking

**Fact.** Mods use modified Lua 5.2. Runtime receives registered events; persistent state is held in `storage`, while code and ephemeral local variables are recreated on load. The [migration documentation](https://lua-api.factorio.com/latest/auxiliary/migrations.html) covers renaming prototypes and updating saved data. Factorio's save startup rules account for joining multiplayer clients and synchronizing mod state. The official API does not expose Factorio's internal transport, collision, prediction, or physics implementation as a mod contract.

**Inference.** MegaMod should provide typed event payloads, bounded per-package persistent state, definition lookups, and an explicit schema migration path before durable scripted worlds are supported. Host authority fits MegaMod better than attempting Factorio-style deterministic execution on every client. The host sends validated effects and state; clients may run only cosmetic scripts with no gameplay authority.

## World, physics, and tools

**Fact.** Factorio separates tile/world state and runtime entities from their prototypes. Collision boxes, selection boxes, masks, and graphical metadata are declarative prototype properties. The API has machine-readable documentation for tooling, and startup validation errors for missing required properties. The reviewed docs do not settle a general door/lift/teleport abstraction or expose a general rigid-body/constraint system.

**Inference.** OAL should use schema-generated authoring forms and validators: required fields, ranges, referential integrity, dependency closure, and helpful source locations. Compile geometry and collision ahead of time. For worlds, OAL should link trigger/event target references before runtime.

## MegaMod conclusions

- **Adopt:** a strict content-definition phase and runtime phase; named IDs; dependency-aware ordering; immutable definitions during a match; event-driven host scripts; migration/version hooks when saves exist.
- **Consider later:** safe build-time definition patching and machine-readable public script API documentation.
- **Avoid:** a shared mutable data table at match load, arbitrary cross-mod mutation, lockstep determinism as a requirement, and complex final-fixes ordering without a concrete use case.
- **For Open Asset Lab:** treat OAL as the compiler and validator of definitions, dependencies, assets, and provenance; emit a resolved registry plus diagnostics.
- **Prototype:** two original packages, one defining a weapon and one explicitly patching one field; compile twice to verify deterministic output and actionable conflict errors, then spawn the weapon via a read-only definition lookup in a host-only Lua sandbox.
