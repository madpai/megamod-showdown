# Comparative scripting boundary

Lua remains a **candidate**, not a committed implementation. Each observation below links to its [source report](README.md).

| System | What scripts/configs own | Native/engine boundary | Event/state model | Authority lesson |
| --- | --- | --- | --- | --- |
| [Factorio](FACTORIO.md) | Startup prototypes and runtime control Lua | Engine world exposed through API objects | Registered events, persistent `storage`, migrations | Lifecycle separation is precise; deterministic multiplayer model is not MegaMod's default |
| [GMod](SOURCE_GMOD.md) | Entities, weapons, gamemodes, hooks | Source engine and game DLL primitives | Server/client/shared realms, net library, network vars | Clear realms; prediction increases complexity |
| [NS2](NATURAL_SELECTION.md) | Substantial gameplay, entity classes and rules | Spark native engine services | Client/server entry points, declared network vars | Scripted gameplay can be broad; schema needed before spawn |
| [Project Zomboid](PROJECT_ZOMBOID.md) | Events, world/item behavior, UI | Java runtime and Lua bridge | `Events`, persistent world state | Small documented bridge is easier to version than broad Java exposure |
| [Arma 3](ARMA3.md) | SQF functions, mission logic; config declares types | Engine simulation and commands | Event handlers, mission state | Machine locality is powerful but hard to reason about |
| [Bethesda](BETHESDA_XEDIT.md) | Papyrus quest/object behavior | Forms and engine native services | Event-driven scripts and save state | Persistent references and migration matter |
| [Space Engineers](SPACE_ENGINEERS.md) | Full mods and narrower programmable-block code | Public interfaces over engine internals | Update/session/entity APIs | Different trust levels need different API surfaces |
| [tModLoader](TMODLOADER.md) | C# subclasses and hooks | Game engine/native data | Category hooks, save/network methods | A loader/registry can scale, but unrestricted hooks proliferate |

## Proposed MegaMod scripting contract (inference)

**Native C owns** entity pools/lifetime, simulation tick, transforms, collision, physics, movement, raycasts, animation playback, renderer/audio, networking codec/transport, package loading, and save serialization. **Host Lua decides** abilities, weapon fire policy, mutators, triggers, objectives, scoring, round flow, and small AI decision hooks by calling exposed verbs. **Client presentation** can initially stay in C; optional cosmetic Lua later gets a separate, read-only state and no gameplay verbs.

The host API should return generation-checked handles and immutable snapshots for event payloads. Core verbs might be `find_definition`, `spawn`, `damage`, `raycast`, `apply_impulse`, `set_team`, `play_animation`, `emit_effect`, and `schedule_timer`. Each verb checks the current phase, authority, type, bounds, and package capabilities. Scripts should never receive `hta_session *`, C pointers, raw files, sockets, packet writers, or arbitrary callback registration during a match.

**Event delivery:** compile a named subscription table at package load. Dispatch in deterministic order (phase, package dependency order, registration ordinal); cap recursive event depth and per-tick calls. Pass `(event_id, actor_handle, target_handle, immutable payload)` conceptually. A queued event records stable handles, so a deleted entity becomes an invalid handle rather than a dangling pointer. A handler error should log package/script/line/event, disable or abort that package's current action according to a documented policy, and never unwind across C frames unsafely.

**State:** package-global persistent state and per-instance state should be small typed tables of scalars, strings, IDs, and bounded arrays. C owns encoding, limits, version tags, and save/load; scripts provide migration functions when an explicit schema version changes. Transient Lua closures and native handles do not survive a save. A package's runtime API version and network schema digest are part of match compatibility.

**Limits:** one host VM or isolated per-package environments is an experiment to decide via measurements. Either way, remove file/network/process access, cap memory and instruction/time budgets, and validate script bytecode/source version. Do not promise a perfect hostile-code sandbox merely from Lua globals; consider scripts untrusted until C API and loader fuzzing are complete. For public packages, package provenance and user consent for scripts will be a separate product decision.

**First experiment:** take one current hardcoded ability and one synthetic trigger. Use a minimal Lua binding with host-only execution, fixed-tick dispatch, typed handles, deterministic seed, and a declared replicated cooldown/effect. Measure phone frame time and bytecode memory, script error behavior, and two-client authority. No whole-game scripting rewrite.
