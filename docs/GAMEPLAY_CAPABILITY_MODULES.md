# Gameplay Capability Modules — future MegaMod Engine direction

**Status: [Someday] architectural north star, not an API or an implementation
milestone.** Read with the [canonical vision](MEGAMOD_VISION.md), the
[current architecture](ENGINE_ARCHITECTURE.md), the
[research index](research/README.md), and the [Night Shift evidence](night_shift/README.md).
Current priorities follow measured project needs and the
[handoff](HANDOFF.md#current-testing-objective). X1–X9 are already landed;
physical S24+ validation of X9 visuals and frame pacing is still pending.

**Showdown can be ridiculous. The engine must remain clean.**
**MegaMod does not merge games. MegaMod learns reusable capabilities.**
**Native capabilities simulate. Lua composes.**
**One roster. One engine. Many games.**

## 1. Purpose and architectural model

MegaMod Engine should eventually run radically different experiences from
the same normalized roster and content ecosystem. Showdown tests strange
combinations; independent games can use the same Engine for focused designs.
The layers below describe responsibilities, not directories or a loader plan:

```text
Core Runtime                rendering · input · audio · physics · networking
                            timing · entity lifetime · resource management
        ↓
Gameplay Capability Modules FPS/skate/flight locomotion · vehicles · racing
                            ranged/melee combat · AI · tricks · hazards …
        ↓
Lua / data game modes       Slayer · dedicated Night Shift · Skate Showdown
                            racing · Party · Infection · community modes …
        ↓
Experience content          world · roster · equipment · rules · mutators
                            presentation · playlists
```

A **Gameplay Capability Module** is a reusable native Engine system for a
coherent mechanical domain. Depending on the real feature, it may own
simulation, lifecycle and state, generic verbs and events, definitions,
replication policy, animation and attachment requirements, physics
requirements, bounded state visible to Lua, and compatibility information.
Examples worth considering when demanded by real work include FPS, skate,
flight, parkour, movement-shooter, vehicle and hover movement; ranged and
melee combat; trick recognition; inventory; PvE AI and horror direction;
structural destruction;
physics manipulation; race checkpoints; and dynamic hazards. These are
possibilities, not a list of implemented modules.

“Module” names an architectural boundary. It does **not** prescribe a
dynamic library, plugin, ECS component, DLL, separate repository, or new C
ABI. Existing `player`, `vehicle`, `game`, weapon and world-event code is not
already organized this way. The [entity/component direction](MEGAMOD_VISION.md#6-entity--component-direction-now--next)
concerns object composition; a Gameplay Capability Module concerns a
reusable mechanical system. The two concepts may interact, but are not
synonyms.

## 2. Capability versus game mode

| Question | Answer | Examples |
| --- | --- | --- |
| What can the Engine simulate? | A capability | FPS movement, skating, flight, vehicle handling, ranged combat, PvE AI, physics interaction, trick recognition |
| Which capabilities are active, and what are this experience's rules? | A Lua/data game mode | Slayer, dedicated Night Shift, Skate Showdown, MegaMod Racing, MegaMod Party, Infection |

Native code handles frequent movement, collision, rigid bodies, vehicle
solvers, expensive AI, animation runtime, rendering and networking. Lua and
data choose rules, timers, scoring, elimination and win conditions,
objectives, spawns, hazards, mutators, playlists, configuration and event
reactions. The host Lua and declarative bindings already used by Night Shift
are a small proof of that division; complete Lua/data game modes and the
listed future capabilities are not landed. A game mode should combine
capabilities instead of adding each new genre as a special case in
`game.c` or Slayer.

Illustrative pseudocode only; none of these functions or IDs is a frozen API:

```lua
mode.require_capability("locomotion.skate")
mode.require_capability("scoring.tricks")

function on_player_spawn(player)
    player:set_locomotion("skate")
end

function on_trick_landed(player, trick)
    score:add(player, trick.points)
end
```

Lua should not be a high-frequency skateboard physics controller, vehicle
solver or rigid-body engine. **Native capabilities simulate. Lua composes.**

## 3. Donor games and source-independent design

Decompiled or recompiled games, source releases, SDKs, open recreations,
mod frameworks and technical writeups can be research donors. They are not
automatically runtime dependencies or reusable code. For a proposed
mechanic: identify its useful behavior; read existing [research](research/README.md)
first; distinguish **Fact / Inference / Unknown**; check source and asset
rights; separate reusable principles from donor-specific structure; map the
idea onto MegaMod concepts; and prove it with original content where
practical. Open Asset Lab normalizes lawful foreign content offline.

`Skate3Controller`, `MW2Movement`, `SourceParkour`, and
`HaloVehicleSystem2` would leak donor identity into the Engine. Names such
as `SkateLocomotion`, `ParkourCapability`, `ArcadeVehicleHandling`,
`HoverVehicleHandling`, `RaceParticipant`, or `HorrorDirector` illustrate
the generic direction without choosing final API names. **MegaMod does not
merge games. MegaMod learns reusable capabilities.**

## 4. Skate Showdown and the shared roster [Someday]

Skate Showdown is a sharp future test: the same MegaMod Character should
skate without a separate “skater version” of each roster member. A
character might expose a definition, normalized skeleton/retarget profile,
collision and hit profile, equipment and attachment points, while a mode
selects an active locomotion capability such as FPS, skate, flight or
vehicle. Master Chief, Dragonborn, John Marston, Ronald McDonald, an
original character and future compatible imports should all be ordinary
normalized characters at the Engine boundary; their identities are
Showdown content, not Engine types.

A future skate system might own board state, push, steer, carve, ollie,
grab, flip, manual, grind and bail behavior; trick recognition and scoring
events; skate physics, camera and animation state; and relevant host/network
state. None of that is claimed as current implementation.

Shared-character interoperability may eventually need canonical humanoid
skeletons, retarget profiles, named hand/foot/board/weapon/camera
attachments, stance offsets, scale compensation, animation sets, IK hints,
locomotion compatibility metadata and graceful fallback for non-humanoids.
These are design questions, not schemas. Open Asset Lab should do expensive
offline skeleton analysis, retargeting, stance/attachment generation,
validation and provenance work. The Engine consumes bounded, validated,
engine-ready results. **MegaMod understands engine-ready concepts. Open
Asset Lab understands foreign content.**

## 5. MegaMod Racing [Someday]

Racing can test vehicle-centric and high-speed gameplay, checkpoint/lap
rules and the separation of race participation from movement. Possible
experiences include grounded arcade kart-style racing, high-speed hover,
combat or demolition races, aerial and broomstick races, skate races and
improvised vehicle races. They are design categories, not plans to clone
commercial games.

**A race participant need not be a four-wheeled car.** In a Showdown
example, Master Chief could drive, Harry could ride a broom, Superman
could fly, and another character could use a hovercraft or skateboard.
Race rules care whether a participant can enter, cross checkpoints, obey
route/lap rules, hold race state, reset or respawn, and be ranked. Movement
can come from another capability. Possible generic domains include arcade
and hover handling, checkpoints, laps, position, boost, reset and scoring;
their identifiers and contracts remain undecided.

## 6. Night Shift: proven slice, first dedicated rules

[Night Shift: Harrow Annex](night_shift/GAME_FLOW.md) is a real production
vertical slice, not merely a scary map concept. Original content built in
Open Asset Lab already exercises package/resource identity and
compatibility, prefabs, world bindings, doors and hazards, power and
lockdown progression, authored route state, host authority, late-join
reconstruction and a small Lua escape hatch for nearby-player logic and a
counter. [The findings](night_shift/FINDINGS.md) show declarative bindings
carried 58 of 59 behaviors in the original slice. X8 added compact world
state and had a physical S24+ solo playtest near 120 FPS; X9 added authored
ambient/local lighting, fog, material response and presentation. X9's
[visual pass](night_shift/X9_VISUAL_PASS.md) has desktop and SwiftShader
evidence. A subsequent S24+ solo review recorded 120.2 mean FPS, positive
visual feedback and no perceived stutter. Desktop submit/fence timing is
not an isolated GPU lighting benchmark; SwiftShader FPS does not predict
phone FPS. X9 added no
shadows or new bloom implementation.

A later [scenario-rules slice](night_shift/SCENARIO_RULES.md) added a native
`Scenario` mode to `night_shift_x9`: one pistol, no grenades or pickups,
crew friendly-fire protection, objective text, a host-authoritative
completion state and a local toggleable flashlight. Its physical phone
performance and physical multiplayer behavior remain unmeasured. It is a
first rules slice, not a Lua/data game-mode framework.

The facility created horror **without a creature**. [AI readiness](night_shift/AI_READINESS.md)
assesses a possible later NPC test; it built no PvE creature AI. A future
dedicated Night Shift Lua/data mode could compose FPS locomotion, guard-style
spawn/loadout with deliberately limited ammunition and equipment, dedicated
failure state, richer objectives,
PvE AI, horror spawning/director behavior, scripted encounters and survival
rules. Zombies, aliens, creatures and other threats remain open design
space, not established canon or implemented features. The proven slice
supplies evidence for what a dedicated mode should build upon.

## 7. MegaMod Party [Someday]

MegaMod Party is a first-class **future architectural validation target**:
a session of short, readable rounds with changing movement, physics,
vehicle, combat, survival and team rules. It should have its own identity.
The same players and normalized characters persist while rules and active
capabilities change. A conceptual playlist could be Kart Soccer → Floor
Is Lava → Skate Trick-Off → Bomb Pass → Flashlight Hunt → Bumper Cars →
Giant Player. This is an illustration, not a backlog or promised order.

```text
Party Session
├── players / roster and persistent session score
├── round sequence and transition state
└── Microgame
    ├── required capabilities · world / arena · spawn rules
    ├── timer · scoring · elimination / win condition · mutators
    └── cleanup / reset behavior
```

This is not a schema or a request for one huge `PartyGameSystem`.
Illustrative combinations: Skate Trick-Off uses skate locomotion and trick
scoring; Kart Soccer uses arcade vehicle handling, a physics ball and team
goals; Flashlight Hunt uses FPS movement, light/darkness and PvE **or**
asymmetric-player rules; Floor Is Lava uses movement, a changing hazard
and elimination. Capability identifiers remain illustrative.

Representative **ideas, not roadmap tasks**, show the desired breadth:

| Family | Possible microgames |
| --- | --- |
| Movement / obstacle | Floor Is Lava, Falling Platforms, Obstacle Course, Red Light / Green Light, Musical Platforms, Low Gravity Knockout, Grapple Madness, Jetpack Panic, Cannonball, Don't Stop Moving, Don't Touch the Color, Conveyor-Floor Survival, Skate Trick-Off, Short Checkpoint Race |
| Vehicle | Arcade Race, High-Speed Hover Race, Bumper Cars, Kart Soccer, Tank Sumo, Demolition Race, Air Race, Broomstick Race, Improvised Vehicle Race |
| Combat chaos | Bomb Pass, Rocket Tag, One Bullet, Random Weapon Rotation, Hot Potato Weapon, Melee Knockback, Giant Player, Everyone vs One, Character Roulette, Weapon Roulette, Random Superpower, Physics Roulette, Gravity Roulette |
| Physics / environment | Giant Ball, Human Bowling, Skate Bowling, Crate Stack, Meteor Shower, Falling Ceiling, Rising Water, Door Roulette, Collapsing Arena, Moving Hazards, Delivery / Cargo Chaos |
| Hunt / survival / social | Flashlight Hunt, Hide-and-Seek, prop-style disguise, Invisible Players, Grab the Creature, Protect the Idiot, Loot Goblin, Boss Says, Simon Says, Limited Safe-Zone Survival |

Party play would pressure reusable round lifecycle, timers, scoring and
session scoring, elimination, spawn/reset, objectives, hazards, mutators,
selection, arenas, physics interaction, spectating, transitions, cleanup,
multiplayer sync and late-join policy. The scripting test is concrete: enter
a short round, initialize its capabilities, score it, clear transient state,
switch to a different ruleset **without restarting the Engine**, and keep
players and session score. Eventually a creator should usually add a
microgame with existing capabilities, small Lua/data rules and content,
not another C branch. These abstractions earn their place when real modes
need them.

## 8. Open Asset Lab, packages and compatibility

Open Asset Lab stays the offline intelligence and compilation layer. Future
needs may include character/skeleton and vehicle normalization,
retargeting, attachments, locomotion compatibility, track/checkpoint and
hazard metadata, physics metadata, package capability declarations,
provenance and diagnostics. MegaMod consumes bounded engine-ready results;
it does not parse each donor game's formats on the phone.

An experience might someday declare required generic capabilities, for
example `locomotion.fps`, `locomotion.skate`, `locomotion.flight`,
`combat.ranged`, `combat.melee`, `vehicle.drive`,
`vehicle.arcade_racer`, `vehicle.hover_racer`, `race.checkpoints`,
`race.laps`, `ai.pve`, `director.horror`, `scoring.tricks`,
`scoring.race`, `world.interaction`, `world.dynamic_hazard`. These are
**illustrative IDs only**. A future loader should reject a missing or
incompatible required capability before spawn. This should grow with the
existing typed resource/definition work, package graph, world key and
multiplayer compatibility, without replacing them or fixing a new format
here.

## 9. Host authority and cost

The host remains authoritative; gameplay Lua need not run
deterministically on every peer. Before implementing any capability, answer:

- What state and decisions belong to the host, and what bounded input does
  a client send?
- Which events and state replicate? How does a late joiner reconstruct
  current state without replaying gameplay effects?
- Which actions, if any, can be safely predicted and reconciled?
- How are capability and package versions checked before spawn?
- What Android CPU, memory and bandwidth costs have been measured?
- How are malformed packages, inputs and excessive activity bounded?

Night Shift's host world changes and late-join snapshots are evidence for
this discipline. Donor games with different ownership models are not a
reason to introduce peer-owned gameplay.

## 10. Research and legal boundary

Before major capability work, search and read the existing
[research corpus](research/README.md) and relevant Night Shift or other
production evidence. Write a focused note only for the unresolved question.
Potential future research includes skate movement/tricks, parkour, arcade
and hover handling, boost/drift/checkpoint rules, rapid round reset,
physics-party mechanics, dynamic hazards/arenas, asymmetric rules,
survival directors, specialized AI and destruction. No such study is
claimed complete here. Record **Fact / Inference / Unknown**, versions,
sources, and licensing limits.

Architectural observation, behavioral observation, independently developed
implementation and licensed code reuse are distinct. “Decompiled” does
not mean open source; “recompiled” does not mean reusable; “downloadable”
or Workshop-available does not mean redistributable. Before copying any
specific code, verify its license, coverage, GPLv3 compatibility and any
game-specific restriction. Proprietary source, assets and user-supplied
retail data have separate rights from an open mod framework or tool. When
reuse is unclear or forbidden, study the idea and implement it
independently. Do not commit unauthorized game assets or code to this public
repository. Preserve package and asset provenance.

## 11. Staging and validation targets

**Real product need → small reusable capability → second use →
generalization if justified.** Current X1–X9 foundations and the pending
physical X9 review stand on their own. Skate, Racing and Party are
architectural north stars, future consumers and research targets; they are
not automatically the next engineering milestone. No speculative movement,
vehicle, networking, package, plugin, ECS or source-tree rewrite follows
from this document.

Possible future proofs, when product needs justify them:

| Experience | What it could validate | Current evidence boundary |
| --- | --- | --- |
| Night Shift | Focused PvE/horror composition, Lua/data mode rules, objectives, director and enemy behavior | Original scenario, interactions, Lua, host/late join, X9 visuals and a first native Scenario rules slice exist; Lua/data mode, AI and director do not |
| Skate Showdown | Alternate locomotion, shared roster, retargeting, tricks and scoring | Aspirational; no skate capability claimed |
| MegaMod Racing | Generic race participants, checkpoints/laps and varied movement systems | Aspirational; current Halo vehicles are not this framework |
| MegaMod Party | Rapid recombination, round reset, Lua/data rules and persistent roster/session score | Aspirational; no party playlist claimed |

At maturity the *same* group of characters could fight in an FPS arena,
drive or fly through a race, skate, survive falling platforms, play a
physics ball game, hunt with flashlights, face a PvE horror round and
finish with a giant-player finale, all on one runtime, networking model,
roster, package ecosystem and scripting environment. **One roster. One
engine. Many games.**

## 12. Decision questions for future APIs

- Could scoring express Slayer kills, trick points, race placement, team
  goals, a party round and persistent party-session score?
- Could round lifecycle cover Slayer, a dedicated Night Shift, a
  45-second microgame, a race and a survival finale?
- Does a movement API assume FPS walking where skate, flight, vehicle,
  hover or parkour might become a real second consumer?
- Could an objective represent CTF, extraction, a race checkpoint,
  a party goal and a PvE task?
- Can original content use the mechanic? Can Open Asset Lab validate its
  inputs? Can the host replicate and a late joiner reconstruct it within
  Android budgets?

These questions should prevent needless shooter assumptions without
generalizing before evidence exists.

**Are we importing a game, or teaching MegaMod Engine a reusable capability
that can support original content too?** The architectural answer is the
latter.
