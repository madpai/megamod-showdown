# MegaMod vision — the north star

**Read this before any major architectural decision** in this repository or
in [Open Asset Lab](https://github.com/madpai/open-asset-lab) (its companion
is `docs/ASSET_LAB_VISION.md` there). This is the canonical statement of
what both projects are becoming. It was set by the owner on 2026-09-26.

The [comparative research corpus](research/README.md) studies the external
architectures behind possible next steps. Its recommendations are proposals;
the current implementation and staged priorities remain defined here.

Every section separates three things, and so must every contributor:

| Tag | Means |
|---|---|
| **[Now]** | Implemented and tested in the repositories today |
| **[Next]** | The planned direction; build toward it when concrete work calls for it |
| **[Someday]** | Aspirational; documented so decisions don't foreclose it, not in scope |

Nothing here is permission to rewrite working systems. See
[§21 Do not over-refactor](#21-do-not-over-refactor).

---

## 0. In one paragraph

MegaMod is not "Halo with mods". It is becoming a **native, lightweight,
content-driven game runtime** where worlds, characters, weapons, vehicles,
abilities, scripts and rules are composed from normalized building blocks
and played natively and in multiplayer on modest hardware. **Open Asset Lab**
is becoming the system that **understands, prepares, validates and packages
those building blocks** from any origin. **Halo is one compatibility layer.
Source is one importer family. Steam Workshop is one provider.** The
long-term platform belongs to MegaMod itself.

```
 EXTERNAL OR ORIGINAL CONTENT
            │
      OPEN ASSET LAB ── acquire (providers) · identify · parse · normalize
            │           analyze · validate · compile · provenance
      MEGAMOD PACKAGES
            │
      MEGAMOD ENGINE ── core runtime · game framework · content runtime
            │           compatibility layers (Halo Trial, …)
  GAME / SANDBOX / EXPERIENCE
            │
   NATIVE MULTIPLAYER
```

**The rule that decides most arguments:**
*MegaMod should understand MegaMod content. Open Asset Lab should understand
the messiness of external content.*

### Names
- **MegaMod**: the engine and platform.
- **MEGAMOD SHOWDOWN**: this repository and its first experience, the
  crossover arena game.
- The C prefix `hta_` ("Halo Trial Android") is historical. Keep it. Renaming
  thousands of symbols is churn with no capability. New *concepts* get
  generic names (below); the prefix stays.

---

## 1. What MegaMod is becoming

A lightweight, native, modular, content-driven game runtime and creation
platform. It loads normalized worlds, characters, weapons, vehicles,
abilities, scripts, rules and other content from many origins, and it stops
caring where they came from. Once content reaches the runtime, its origin
matters only as provenance or compatibility metadata.

The defining experience **[Someday]**: *pick a world, characters, weapons,
vehicles, abilities, rules, mutators — play.* For example gm_construct;
Master Chief, Goku, Chell, an original robot; a gravity gun, a sword, a
custom energy weapon; King of the Hill; low gravity, explosive deaths,
destructible props. All of it launched without modifying or rebuilding
the C engine.

Crossover content is the demonstration, not the identity. The deeper goal:
take worlds, characters, assets, behaviours and gameplay ideas from wildly
different sources, normalize them into a common interactive language,
recombine them into new experiences, and run them natively and multiplayer
on lightweight hardware.

### Conceptual engine structure [Next, as a direction]

```
MegaMod Engine
├── Core Runtime       rendering · audio · input · physics · networking · timing
│                      memory · entity lifetime · resource management
├── Game Framework     characters · weapons · abilities · inventory · health/damage
│                      teams/factions · vehicles · objectives · scoring · game rules
├── Content Runtime    worlds · meshes · materials · animations · audio assets
│                      scripts · gameplay metadata
└── Compatibility      Halo Trial · OAL runtime packages · future adapters
```

This is a map to refactor *toward* when concrete work justifies it, not a
directory layout to impose. How the code is laid out today is in
[ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md).

### Where it stands [Now] (2026-09-26)

| Area | Today |
|---|---|
| Platforms | Native ARM64 Android (NativeActivity, Vulkan, AAudio, Java menus/HUD). Desktop Linux: SDL2 window, Vulkan, SDL audio; the sandbox, a LAN joiner, a dedicated-server skeleton and offscreen tools. The **full game runs only on Android**; desktop parity is [DESKTOP_AGENT.md](DESKTOP_AGENT.md). |
| Rendering | One Vulkan forward renderer (swapchain, desktop, offscreen): lightmaps, skinned meshes, instancing, sky, HUD; MSAA, render scale, bloom, tone mapping, FXAA, fog; quality presets Potato–Ultra. |
| Halo compatibility | Reads the owner's Trial cache (Blood Gulch, bitmaps, sounds, ui) and takes gameplay values from the tags. **Every match, including on imported maps, still loads Blood Gulch's tags** for bipeds, weapons, sounds, sky, rules and equipment (`hta_game_load` on the Halo cache). Only `megamod-sandbox` runs with no game data. |
| Imported content | OALMAP v1/v2 maps (v2 adds static Source lightmaps) and OALASSET v1 characters, weapons and sound banks, made by Open Asset Lab from Source/GMod. The owner's private bundle: six maps, 14 characters, 11 weapons. Hero stats and abilities come from `hero_roster.json` data plus special-cased C in `game.c`. |
| Gameplay | Slayer, Team Slayer, CTF (C in `game.c`); bots on a nav grid; Halo vehicles on Blood Gulch only; heroes with flight and abilities; destructible props, gibs, weather, procedural sound. |
| Multiplayer | Host-authoritative UDP, protocol v9, LAN discovery and direct IP, fuzzed decoders, per-source rate limiting. Feature-specific packets (WORLD, GAME, KILL, FX, VEHICLES, DROPS, …). No authentication yet (v10 plan in [DEDICATED_SERVER.md](DEDICATED_SERVER.md)). |
| Physics | Custom: triangle-grid collision with placed instances, biped movement, box/sphere rigid bodies for debris and props, Halo-derived vehicles, a single-body corpse tumble. **No articulated ragdolls, no joints or constraints.** |
| Navigation | A 2D-ish grid built from collision, with directed links and prop blocking. **No ladders, lifts, doors, teleports or flight paths.** |
| Materials | Halo shader interpretation for Halo content; imported maps carry baked RGBA albedo (+ optional lightmap) per group with alpha and breakable flags. **No shared material model.** |
| Entities | Fixed kinds: units (local/remote/bot), vehicles, projectiles, props, pickups, drops. **No component model, no world logic** (doors, buttons, triggers): imported maps are static geometry plus breakables. |
| Scripting | None. |
| Tooling for agents | Host tests (45), sanitizer CI, NDK check, `verify.sh` with Trial data, SEND REPORT from the phone, and an agent harness (control channel, event log, playtests) in `megamod-sandbox`. |

---

## 2. What Open Asset Lab is becoming

A general content **ingestion, analysis, normalization, conversion,
validation, packaging and creation** environment that turns heterogeneous
game assets into clean runtime content MegaMod understands: a universal
content compiler, a content-intelligence platform and, eventually, creator
tooling. It is *not* "a script that converts GMod content for MegaMod".

**[Now]:** a Python package with Source BSP v19/v20, MDL v44–v49,
VMT/VTF/VPK and GMA readers; a Steam Workshop provider (search, fetch,
analyze, import, collections); OALMAP/OALASSET compilers with provenance
manifests and compatibility reports; and a private local web service with a
job queue and a staged library.

The detail — providers, importers, intermediate representations, humanoid
and weapon normalization, AI-assisted analysis — is in Open Asset Lab's
`docs/ASSET_LAB_VISION.md`. The contract between the two is below.

---

## 3. Separation: engine vs content tooling

| MegaMod (runtime) owns | Open Asset Lab (tooling) owns |
|---|---|
| Loading **MegaMod packages** and validating them defensively | Acquiring content (providers) with provenance |
| Rendering, audio, physics, networking, input, timing | Identifying, parsing and understanding foreign formats |
| Generic gameplay concepts: characters, weapons, doors, triggers, rules | Translating foreign concepts into those generic ones |
| Running scripts and rules | Normalizing skeletons, materials, units, coordinates |
| Replication and authority | Generating what content lacks (collision, nav hints, LODs, previews) |
| Compatibility layers it already has (Halo Trial) | Validating before runtime; compiling packages; staging |

**Rules:**
- **Foreign terminology belongs in importers and compatibility layers.**
  The runtime never learns `func_door`, `SWEP`, `$surfaceprop` or a Halo tag
  class name for *new* systems. (The Halo layer that exists keeps its tag
  knowledge in `asset/` and the parts of `game/` that name tags.)
- The runtime must **not** trust packages: bounds-check everything, as the
  OALMAP loader already does.
- **Anything expensive, heuristic or format-specific happens offline** in
  Asset Lab, not on a phone at load time.
- Contract changes (package formats, runtime markers) are coordinated and
  tested in both repositories.

---

## 4. Why Halo is now a compatibility layer

Halo was the bootstrap. The Trial's real data forced this engine to solve
native Android execution, Vulkan, audio, input, collision, navigation, bots,
vehicles, weapons, characters, HUD, effects, multiplayer, modes and map
loading against a real game, not a toy. That history stays documented
([HANDOFF.md](HANDOFF.md), [JOURNAL.md](JOURNAL.md)) and the tag discipline
stays: take values from the data, and ledger every invented constant.

But Halo is now **one compatibility layer**:
- **Keep Halo working where practical.** Blood Gulch remains a regression
  test and a playable map. Don't break it without the owner's say-so.
- **Stop growing Halo assumptions into general systems.** New systems
  consume generic definitions; the Halo layer *produces* those definitions
  from tags, the way Asset Lab produces them from Source.
- **The coupling to remove, in order of leverage** [Next]:
  1. `hta_game_load` needs a Halo cache even for imported maps: bipeds,
     weapons, sounds, sky, netgame equipment. Generic definitions
     (`WeaponDefinition`, `CharacterDefinition`, …) filled either from tags
     or from packages would lift that.
  2. The sky on imported maps is Halo's (kept on purpose for now).
  3. Vehicles exist only as Halo vehicles, and only on Blood Gulch.
  4. Menus and the main-menu presentation are the Trial's `ui.map`.
- The milestone that proves the change: **a complete original MegaMod
  experience that runs with no Halo and no Source assets** (§16).

## 5. Why Source is an importer, not a runtime dependency

Source/GMod is the first major content family, and an excellent torture
test for generality: see Asset Lab's `docs/GENERALIZATION_AUDIT.md`. But its
formats, entity classes, units and material system are Asset Lab's problem.
**[Now]** the runtime already knows no Source format: it reads OALMAP and
OALASSET only. The one Source rule that had leaked into C (team from
`info_player_terrorist` class names) was moved out in 2026-09-23's
generalization audit: Asset Lab writes a generic `"team"` per start and the
loader reads only that. Weapon numbers drafted from SWEP Lua are labelled
estimates for review. Keep it that way. If Source support disappeared tomorrow,
MegaMod should not notice beyond the content it can no longer import.

---

## 6. Entity / component direction [Next]

Move gradually toward **composition**: entities described by capabilities,
not an ever-growing list of hardcoded kinds (player, bot, crate, barrel,
hero, corpse, door, turret, teleporter, explosive barrel…).

Candidate capabilities: `Transform`, `Renderable`, `Collider`,
`PhysicsBody`, `Health`, `Damageable`, `Inventory`, `Faction`, `Character`,
`Weapon`, `AbilitySet`, `Vehicle`, `Interactable`, `Breakable`, `Trigger`,
`AudioEmitter`, `NetworkReplicated`, `Scripted`.

```
breakable crate = Transform + Renderable + Collider + PhysicsBody + Health + Breakable + NetworkReplicated
door            = Transform + Renderable + Collider + Hinge + Interactable + EventReceiver + NetworkReplicated
character       = Transform + Renderable + Collider + Health + Inventory + Faction + AbilitySet
                  + CharacterController + AudioEmitter + NetworkReplicated
```

The exact list doesn't matter. Avoiding endless hardcoded combinations
does. **This is not a textbook ECS rewrite.** `hta_unit`, `hta_prop`,
`hta_vehicle` work. The first components should appear where a real feature
needs them (world entities in §8 are the natural first user), and existing
kinds migrate only when touching them anyway pays for itself.

## 7. Scripting direction [Next → major milestone]

An embedded scripting runtime, so a new ability, weapon behaviour, trigger,
mutator or game mode does not require recompiling the engine. **Lua is the
leading candidate** (small, embeddable, proven on mobile). Evaluate
alternatives only if one is clearly better for size, safety or determinism.

**Principle: the engine exposes verbs; scripts compose them.** C provides
efficient, safe primitives (spawn, damage, beam, ray, play animation, apply
impulse, set team, start timer, emit event). Scripts decide what an
"EnergyBeam" is:

```lua
-- illustrative only, not an API commitment
ability "EnergyBeam" {
  cooldown = 8,
  activate = function(player)
    player:play_animation("beam_charge")
    wait(0.8)
    world:beam{ origin = player:hand_position(), direction = player:aim(), damage = 500 }
  end
}
```

Constraints that shape the design: scripts run **on the host** (authority)
unless explicitly cosmetic; they need budgets (instruction/time limits) so
content can't stall a phone; they must never touch files or the network
directly; and every verb must be replicable (§11). Candidate uses:
abilities, weapons, triggers, doors, world events, modes, scoring, win
conditions, mutators, sequences, AI hooks, procedural events.

Today's `hero_roster.json` stats plus special-cased C abilities are the
thing this replaces. Heroes are the obvious first scripted content.

## 8. Generic world events and interactive entities [Next]

Imported maps must become more than static geometry, **without
reproducing Source's entity system**. MegaMod gets a small generic
event/signal model:

- **Events:** `OnSpawn`, `OnUse`, `OnEnter`, `OnExit`, `OnDamage`,
  `OnDeath`, `OnBreak`, `OnTimer`, `OnRoundStart`, `OnRoundEnd`
- **World objects:** `Door`, `Button`, `Trigger`, `Teleporter`, `Lift`,
  `Spawner`, `CaptureZone`, `Breakable`, `DamageVolume`, `MovingPlatform`
- **Wiring:** an entity's event targets another entity's input (open,
  toggle, enable, teleport-to), as data, with scripts for anything richer.

Asset Lab translates foreign entities into these:

| Source | MegaMod |
|---|---|
| `func_button` | Button + EventEmitter |
| `trigger_teleport` | Trigger + Teleporter |
| `func_breakable` | Renderable + Collider + Health + Breakable **[Now]**: breakable groups already flow through OALMAP flags |
| `func_door` | Door (+ Hinge or MovingPlatform) + Interactable |

## 9. Game rules as data and script [Next]

Modes stop being C per variation. The engine emits general facts: player
joined or died, item picked up, entity destroyed, zone entered, flag
captured, timer expired, score changed, round started or ended. Rules
react. Slayer, Team Slayer, CTF, King of the Hill, Gun Game, Infection,
Prop Hunt, hero modes ("whoever kills the hero becomes the hero"), vehicle
modes and community modes become rule definitions. **Mutators** (low
gravity, double speed, explosive deaths, random weapons) are small rule
modifiers layered on any mode.

**[Now]** Slayer, Team Slayer and CTF are C in `game.c`, with the mode
enum replicated in GAME. Port them to the rule model only once it exists
and is tested. They are its regression suite.

## 10. Packages [Now → Next]

**[Now]** OALMAP v1/v2 (a fixed-section binary + canonical JSON manifest;
[Asset Lab's RUNTIME_PACKAGE.md](https://github.com/madpai/open-asset-lab/blob/main/docs/RUNTIME_PACKAGE.md))
and OALASSET v1 (characters, weapons, sound banks). Both are valuable
working prototypes. **Don't discard them.**

**[Next, when requirements force it]** an extensible **chunk-based**
container so new data doesn't mean another monolithic version branch.
Conceptually:

```
OALP  META MESH MATL TEXR COLL NAVM ENTS ANIM AUDO PHYS SCRP …
```

Each chunk is tagged, sized and versioned. **Unknown optional chunks are
skippable** where safe, and required chunks are declared in META. Migrate
when a concrete need (world entities, scripts, materials) would otherwise
fork OALMAP again, not for elegance. The loader keeps the existing
bounds-checking discipline.

### Experience packages [Someday → Next]
A distributable **experience** (working names `.oalmod`, `.megapack`,
`.megamod`; don't settle the name early):

```
my_experience/
  manifest   name, version, runtime requirements, entry world, mode,
             player limits, dependencies, configuration, provenance
  maps/ characters/ weapons/ vehicles/ props/ materials/ audio/
  scripts/ rules/ ui/
```

A completely original game should eventually ship this way.

## 11. Networking direction [Next]

**Keep the host-authoritative model.** It is the right foundation. But
as entities become composable, replication should become **explicit per
component** rather than a growing list of feature packets:

| Component | Policy |
|---|---|
| Transform | replicated (interpolated) |
| Health | server authoritative + replicated |
| PlayerMovement | predicted locally, validated by host |
| Projectile | server authoritative |
| WeatherParticle | local only |

Policies: `server-authoritative`, `replicated`, `predicted`, `interpolated`,
`local-only`. Keep strict protocol versioning, fuzzing and validation, and
the per-source rate limit. Before any internet play: authentication (v10)
and host validation of client-reported state (today the host trusts a
joiner's position).

## 12. Physics direction

**[Now]** custom systems that work: collision grid, impulses, vehicles,
debris, breakables, a corpse tumble, environment interaction. **Don't
replace them prematurely.** **[Next]** when the engine needs articulated
ragdolls, constraints, hinges, stacking, jointed objects, dynamic vehicles,
ropes or physics puzzles, evaluate **Jolt Physics** (preferred first look:
modern, C++ with a C-friendly surface, used on mobile) or **Bullet**.
Don't write a modern rigid-body solver from scratch without a compelling
reason. Mobile performance and authoritative multiplayer (determinism,
host-side simulation, bounded cost) stay the deciding criteria.

## 13. Navigation direction

**[Now]** a grid from collision with directed links and prop blocking. It
has been very useful; keep it while it works. **[Next]** worlds with
ladders, jumps, drops, lifts, doors, teleports and flying actors need
**navmesh + off-mesh links** (walk region → ladder → upper region; → jump →
platform; → elevator → floor 2; → teleport → remote region). Asset Lab can
prepare nav offline (§3). Replace the grid only when the new system shows
a tested advantage on real maps.

## 14. Material abstraction [Next]

One MegaMod material model instead of rendering bound to Halo or Source
assumptions. Shared fields: base colour, normal, roughness, metallic,
emissive, opacity, alpha test, detail texture, lightmap, environment
reflection, **surface type**. Halo shaders, Source VMT/VTF and glTF PBR all
translate into it (Halo at load; foreign formats in Asset Lab). Renderer
improvements then benefit all content. **Interoperability and clear
semantics, not photorealism.**

**Surface semantics** drive generic behaviour: wood → wood impact sound,
splinters, wood debris; glass → cracks, shards; metal → sparks, ricochet;
concrete → dust, chunks, heavy thud. **[Now]** props already have a material
(`HTA_RMAT_*`: wood, metal, concrete, glass, flesh, dirt) driving debris,
effects and procedural sound. That is the seed; extend it to world surfaces
from package data. Imported worlds can become *more* interactive in
MegaMod than in their source game.

## 15. Creator tooling: MegaMod Studio [Someday]

Don't rush into a giant editor. But Asset Lab's local web interface may
grow into **MegaMod Studio**: Dashboard, Library, Workshop, Imports,
Characters, Weapons, Maps, Materials, Audio, Projects, Packages,
Validation, Preview. Later: World Editor, Entity Inspector, Rule Editor,
Script Editor, Package Builder, Multiplayer Test Launcher.

**Preserve the local-first model.** Heavy work runs on the desktop; a
phone or tablet uses the interface over the owner's private Tailscale
network. No public listener. The desktop agent harness
([DESKTOP_AGENT.md](DESKTOP_AGENT.md)) is how a Studio "Play" button will
launch and inspect a test session.

## 16. Original content is essential

Imported content is a stress test for generality, **not a prerequisite**.
MegaMod must support original, open-licensed, user-created and procedurally
generated content, and assets made for MegaMod.

**The milestone that proves MegaMod is its own engine:** a complete
original experience (world, characters, weapons, rules) that runs with **no
Halo and no Source assets**. `megamod-sandbox` is the seed: it already runs
the engine's physics, props, gibs, weather and presets with no game data.

The first-class original workflow [Next]:
```
Blender → glTF → Open Asset Lab → character / weapon / map metadata
        → validate → preview → compile → MegaMod
```
No Halo, no Source, no Garry's Mod. Building a **non-Source importer
(glTF first)** is how Asset Lab proves it has generalized.

**Procedural worlds [Someday]:** once Asset Lab understands content
semantically, it could assemble worlds from modular content ("abandoned
mall, medium, 12 players, infection, high verticality, medium destruction")
or remix existing ones. This is a possibility to keep open, not scope.

## 17. Future user experience [Someday]

A possible top level: **PLAY** (installed experiences) · **DISCOVER**
(browse content and experiences) · **LIBRARY** (maps, characters, weapons,
vehicles, props, packages) · **CREATE** (combine world, roster, weapons,
abilities, mutators, rules) · **SERVERS** (host or join). This is a north
star, not a UI to build now.

---

## 18. Development priorities (directional, not a sprint list)

Imported content is now primarily a **stress test**. Don't spend the next
phase importing hundreds more characters. Choose milestones by dependency
order and current stability:

1. Keep extracting Halo assumptions from general runtime systems (§4).
2. Define a clearer generic entity/component model (§6).
3. Formalize runtime asset/content definitions (weapon, character, world).
4. Establish a generic material abstraction (§14).
5. Establish a generic event/entity interaction model (§8).
6. Interactive world entities: doors, buttons, triggers, breakables, lifts,
   ladders, teleports.
7. Prepare engine APIs for scripting (§7).
8. Embed the scripting runtime.
9. Move game rules to data/script (§9).
10. Formalize component/entity replication (§11).
11. Navigation with special traversal / off-mesh links (§13).
12. Evolve package formats when requirements justify it (§10).
13. Evaluate richer rigid-body physics (§12).
14. Humanoid normalization and animation retargeting (Asset Lab).
15. Weapon normalization (Asset Lab).
16. A non-Source importer (glTF).
17. Original MegaMod-native content (§16).
18. Complete experience packages.
19. Asset Lab library / project model.
20. Eventually, MegaMod Studio.

**Already in flight and consistent with this:** desktop parity plus the
agent harness ([DESKTOP_AGENT.md](DESKTOP_AGENT.md)). Pulling the match
loop out of `platform_android.c` is a precondition for most of the list,
and it lets agents test on the desktop.

## 19. The design question for every feature

**Is this a general MegaMod capability, or a special case for one source
game?** Prefer the general capability.

| Don't add | Add |
|---|---|
| `SourceDoor` | `Door` |
| `HaloWeapon2` | `WeaponDefinition` |
| `GModTeleport` | `TeleportTrigger` |

## 20. Agent checklist: before implementing a major system

- Does this work for **original** MegaMod content?
- Could **another importer** use it?
- Is the runtime learning a **generic capability** or one game's
  terminology?
- Could it eventually be configured through **data or scripting**?
- Does it **compose** with unrelated systems?
- Can **multiplayer** replicate it cleanly?
- Can **Open Asset Lab validate** it before runtime?
- Does it preserve **provenance**?
- Does it keep **acquisition separate from conversion**?
- Does it move MegaMod toward a **platform** rather than one hardcoded game?

If a proposal fails several of these, reconsider its architecture.

## 21. Do not over-refactor

The codebase works. This vision must not trigger reckless rewrites. For
every architectural improvement:

1. Identify a **real limitation** a concrete feature hits.
2. Define the **general concept** that solves it.
3. Refactor the **minimum** area.
4. **Preserve** working behaviour.
5. Add or update **tests** (host tests; a playtest where it plays).
6. Run existing verification (`verify` skill, CI).
7. **Verify Android** (the owner's phone check).
8. Verify **Halo compatibility** where it applies (Blood Gulch).
9. Verify **imported content** (the bundle's maps and characters).
10. **Document the coupling that remains.**

No abstractions that solve no concrete problem. No replacing working
systems because another architecture looks cleaner. Large moves happen in
stages with a phone check each, as the loop extraction does.

## 22. Content, legal and provenance rules — unchanged, and stronger

The vision **strengthens** provenance; it relaxes nothing:
- No Halo assets, executables or DLLs, no Trial `.map` files, no Workshop
  downloads, no converted packages (`.oalmap`, `.oalasset`, future
  formats), no proprietary previews in either public repository.
- The personal APK with the owner's content never goes to anyone else. The
  shareable build carries no third-party content.
- Every package records where its content came from (source hash, origin,
  provider, conversion). Inferred or AI-suggested values are labelled as
  such.
- Workshop availability grants no redistribution rights; neither does a
  format being documented.
- Original and open-licensed content is the path to anything shareable.
  An experience package that ships publicly carries only content its author
  has the right to ship.
