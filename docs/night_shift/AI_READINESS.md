# Night Shift -- AI readiness (no AI was built)

An assessment of HARROW ANNEX as a future NPC test world, against Open
Asset Lab's research corpus (`docs/research/ai/`: MEGAMOD_AI_ARCHITECTURE,
AI_ROADMAP, NAVIGATION_RECAST_DETOUR, ENTITY_IO_AND_AFFORDANCES, PERCEPTION,
BLACKBOARDS_AND_MEMORY). Nothing here was implemented; no navmesh, agent,
perception or blackboard code exists. Observations only.

## Is the layout navmesh-friendly?

Yes, and simply so.

- **One floor level** (every walkable floor at z = 0), axis-aligned boxes,
  no slopes or stairs. A single Recast bake per agent profile would be
  trivial; the engine's existing bot grid already covers it (`[world] 10651
  of 33458 nav nodes reachable from a start and back`, built at every load).
- **Widths:** corridors 1.2-1.6 wu, doorways 1.2 wu. A player-sized agent
  (radius 0.18) passes everywhere; a large creature (radius ~0.5) would be
  stopped by the 1.2 wu passage, the vestibule and the tunnel -- a
  two-radius test (AI_ROADMAP's acceptance list) is built into the map.
- **Off-mesh transitions:** two teleports (the lift to the surface; Lua's
  holding cell) -- exactly the "lift/teleport link needs eligibility,
  ownership and completion" case the navigation note describes.
- Non-solid decals (water, lane paint, grates) and 0.01-thick floor decals
  must not become obstacles in a bake (they are non-solid in the package).

## Doors that change routes: 5

| Mover | Gates | Opens when |
|---|---|---|
| `d1__door` | dock <-> corridor (the whole facility) | `d1__power` active and a button is used; closes and loses power at lockdown |
| `d3__door` | corridor <-> research wing | `d3__power` (the AND gate) and a button; slams at lockdown, still powered |
| `lift__door` | dock <-> lift car | `lift__power` (the tunnel breaker) and a button |
| `shutter_core__shutter`, `shutter_dock__shutter` | the service tunnel | lockdown only (no button) |

So the route graph changes **three times** in a play-through (aux,
research, lockdown), and one link (D1) is removed, not added -- a good
test of path invalidation.

## Would an NPC need doors and buttons? Yes

To follow players past D1 or D3 an agent must `use` a door's button (either
face) -- the same action a player's press runs (`hta_went_use`), with the
same power condition and the same `locked` buzz when unpowered. Nothing in
Night Shift is player-specific: every door, breaker, console and valve is
an interactable with a `used` event; hazards `damage` whoever enters; the
lift `teleport`s whoever enters. X7's actions are the affordances the
research asks for (`use`, `open`, `activate`, `damage`, `teleport`).
A design choice the map allows: a creature that **cannot** use buttons
(doors hold it; the players' power choices become its routes).

## Perception stimuli already present

- **Sound events with a position:** 52 of the world's 108 binding actions
  are `play_sound` at an entity (doors, breakers, alarms, steam, shocks,
  the core). They already exist host-side as world cues before they reach
  joiners -- the "bounded gameplay sound events" PERCEPTION.md wants. Door
  servo sounds come from mover starts. The heartbeat makes periodic
  alarms: a hearing model must tell "ambient alarm" from "player noise".
- **Damage stimuli:** hazards damage through `hta_game_hurt` with a known
  source entity.
- **Sight:** dark rooms and occluders (tanks, the generator, the pedestal)
  -- but lighting is not simulated (E3), so "hidden in the dark" is not
  something a perception system could read.
- **State as knowledge:** relays (`aux_power`, `lockdown`...) are host
  state an agent's blackboard could read directly.

## Patrol / search / chase spaces

- **Patrol:** the main corridor (long sightline), the specimen hall (four
  tanks as cover), the tunnel (after lockdown).
- **Search:** control + pump rooms (a dead-end pair), the aux room
  (the generator blocks sight), the holding cell and its passage.
- **Chase:** the post-lockdown loop -- core -> tunnel -> dock -> (sealed D1)
  -- is one long route with steam on a timer: a chase with a hazard to time.
- **Choke points:** D1, D3, the maintenance passage (1.2 wu), the
  vestibule, the tunnel (1.2 wu wide, ~45 wu long).

## Where AI would collide with the engine as it is

1. **The entity budget, if agents were world entities** (62/64 used). An
   agent as a game *unit* (like today's bots) would not use world entity
   slots; any spawn point, patrol node or relay for it would.
2. **Action completion:** a door's completion is its `opened` event (it
   exists); a button press has none beyond its effects. Enough for doors.
3. **The anomaly shows the scripted-custom path works:** Lua read
   `game.near`, chose, and requested a teleport through the action seam --
   the shape the research proposes for "scripts compose existing actions".

## Does the proposed first AI milestone make sense for this map?

Partly. The map is a good *test world* for it (doors, a removed link, two
radii, teleport links, sound stimuli). But:

- a Recast bake buys little here: the map is flat and the existing grid
  already reaches it; the roadmap's option (B) -- a basic agent on the
  existing grid behind a navigation interface -- fits Night Shift better
  than (A) first;
- Night Shift's observed pain is not "we needed a creature". It is the
  entity cap, scenario rules, lighting and authoring (FINDINGS). A monster
  in today's build would carry an assault-rifle HUD, a frag counter and no
  darkness to hide in.

So: AI is a sensible **later** milestone on this map, not the next one.
