# Night Shift -- findings and the next milestone

> Historical 2026-09-27 findings. X8 has since implemented the recommended
> world-state milestone; see [WORLD_STATE.md](../WORLD_STATE.md) and the
> separate 74-object `project_x8.py` build. The original 62-object package
> and the measurements below remain fixed.

What building MegaMod's first production vertical slice showed. Evidence
is in the sibling docs; this page ranks it. Only what was hit while
building and testing Night Shift is ranked -- not speculative features.

## Content composition (final slice)

| | |
|---|---|
| packages | 3 (`nightshift.assets`, `nightshift.facility`, `nightshift.world01`) |
| asset resources | 17 textures, 17 materials, 17 models, 18 sounds (all original, made in code) |
| prefab types / instances | 11 / 19 |
| hand-placed entities | 21 |
| expanded entities (total) | 62 of 64 (41 from prefabs) |
| bindings | 59 (27 world, 32 prefab) |
| conditions / actions | 33 / 108 |
| events used | all 6 (`used` 21, `entered` 13, `activated` 12, `opened` 7, `closed` 3, `deactivated` 3) |
| Lua scripts | 1 (32 lines) |
| world key | `356266bd` (OAL == engine) |
| content source | 1,162 lines of Python + Lua in `projects/night_shift/` |

## Lua usage: one script

| Script | What it does | Why X7 could not |
|---|---|---|
| `nightshift:script/anomaly` | when someone walks into the research wing's cold spot: if any other player is within 5 wu, stir (a sound) on the first visit only; if alone, take them to the holding cell -- at most twice a round | X7 conditions read relays and movers, not **how many players stand near the actor**; X7 has **no counter**. Both are exactly Lua's `game.near` and a local variable. |

Everything else -- power, doors, the AND gate, hazards, alarms, timers,
lockdown, exit -- is bindings. The declarative system carried **58 of 59**
behaviours; the one escape hatch was used for what it is for. Its only
cost was the bridge (E6, E7): Lua cannot be reached from a trigger
except through a hidden button.

## Engine pain, ranked by what Night Shift actually hit

| Rank | Pain | Evidence |
|---|---|---|
| **HIGH** | **world entity capacity** (64, every kind counts) + **state visibility** (relays host-only, so visible state costs movers) | designed to 62/64; D2, D5, frames, signs, lamps cut; 29 of 62 entities never reach the wire; 11 of 19 movers only show state or keep time (ENTITY_BUDGET, E1, E2) |
| **HIGH** | **scenario rules** | Slayer HUD ("In 1st place with 0 Frags"), rifles and friendly fire in a co-op horror slice, no completion state, a score limit that would reset the world (E4) |
| **HIGH** (genre) | **lighting** | no darkness, fog or light sources; models ~3x brighter than world colour; "lights" are coloured boxes and moving props (E3) |
| MEDIUM | timers / delays | the plant's heartbeat is a hidden mover; the core's rise is the delay (E5) |
| MEDIUM | Lua entry points; the bridge's cooldown | a hidden interactable + `use`; a second entry within 0.5 s dropped (E6, E7) |
| MEDIUM | trigger semantics (`left`, "while inside") | hazards hurt once per entry (E8) |
| MEDIUM | audio (loops, stop, occlusion) | ambience by replay; alarms through walls (E10) |
| MEDIUM | world surfaces flat colour; per-instance prefab parameters | signs are props; one generic lamp (E9, E11) |
| MEDIUM | movers pass through players | plume height constrained (E12) |
| LOW | condition logic, actor-only actions, no `round_started`, world mover rules, nav grid cost, truncated errors, menu names, static spawns, tool teardown, route length | E13-E22 |

Not hit at all: **performance** (X7 steps < 2 us, 7 ms loads, 60 fps on the
emulator, 0 dropped sounds), **multiplayer correctness** (authority, late
join, compatibility, no duplicate triggers), **package identity**, **X7's
vocabulary** (every event, condition and action used; none missing for
simple behaviour), **collision of rotated prefabs** (doors turned 90/180
block and open correctly), **the absence of AI** (the facility carried the
horror without a creature), **inventory** (relays represented every
"have done X"; nothing needed carrying).

## OAL product findings

- **Did well:** validation in the engine's words (no engine refusal of an
  OAL-accepted package all session), identical world keys, deterministic
  builds, prefabs + bindings as data, `megamod-resources` introspection.
- **Tedious:** every coordinate by hand; every layout check a
  build -> host -> scripted walk -> screenshot loop.
- **Needs visual tooling:** a plan view (boxes, entities, prefab
  footprints, door slide paths) and a reachability overlay -- three
  blocked routes were found only by walking into them.
- **Needed direct editing:** everything (all content is code).
- **Repeated most:** `project build`, the host/joiner/shot loop, the
  emulator loop.
- **Validations that saved time:** typed references and requirements,
  binding affordances (none failed after the first build), the world key
  parity, the mover-definition rule.
- **Future editor features that would matter most (in order):** plan view
  with reachability; binding graph view; asset/lighting preview and sound
  audition; a text form for bindings; live reload.
- **Added this milestone:** `assetlab project build|budget` (a project
  folder instead of `fixtures.py`, with the entity/binding budget).

## Assumptions that proved wrong

- *"64 entities is plenty for a small slice."* Three doors are 18.
- *"Props look like the world."* They are lit by the scene; authored
  colours come out ~3x brighter.
- *"Texture row 0 is the top of a face."* It is the bottom.
- *"A world can place an inline mover."* Not once it has props.
- *"Lua can react to a trigger."* Only through a hidden button, which has a
  cooldown.
- *"Without timers there can be no ambience or alarm loop."* A mover's
  travel is a usable clock.
- *"A whole play-through fits one scripted joiner."* 512 bytes of route.

## Recommendation for the next milestone

**World State vNext: entity capacity and visible state** -- give original
worlds room to grow and let joiners see world state without spending
movers on it: index only wire-visible entities (or widen the index) so
host-only relays, triggers, hooks and static props stop consuming the
64, and replicate relay state compactly so a door's "powered" is state,
not a lamp mover. This is a protocol change (v11) and must keep
X1-X7 worlds and keys stable.

Why this and not the others:

- It is the only HIGH item that is a **hard ceiling on content**: Night
  Shift has 2 entities left and its 15-30 minute target needs ~20-30 more;
  every future original world hits the same wall first.
- **Scenario rules** is the close second (the tone damage is real, and a
  minimal "no weapons, no score, a completion state" mode is small); it
  should follow, or ship as a small companion slice.
- **OAL plan view + reachability** is third: it costs time, not content.
- **Lighting** is genre-specific; Night Shift is readable without it.
- **AI/navigation is not next** (AI_READINESS): this map is a good future
  test world, but no pain observed here asked for a creature, and one
  would inherit the rifle HUD and the entity cap.

Not started. STOP here.
