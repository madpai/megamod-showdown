# Night Shift -- engine friction log

Living record of every place where building Night Shift met a limit of
MegaMod Engine (runtime, networking, rendering, audio, collision, rules,
tooling). Creator-tool friction is in [OAL_FRICTION](OAL_FRICTION.md).
Every entry was **hit while building or testing this slice**; nothing here
is speculative. Severity is about Night Shift as built: BLOCKING (could not
ship the slice), HIGH (changed the design or the experience materially),
MEDIUM (worked around, with a real cost), LOW (annoyance).

Nothing was BLOCKING. **No engine code was changed** in this milestone.

## Summary

| # | Friction | Area | Severity | Blocked content? | Workaround |
|---|---|---|---|---|---|
| E1 | 64-entity world cap (WORLD_STATE index) | networking | **HIGH** | two doors, signs, lamps cut | design to 62; see ENTITY_BUDGET |
| E2 | relays are host-only: visible state costs a mover | networking / engine | **HIGH** | - | a lamp mover per door, status lamps |
| E3 | lighting: models lit by one global scene light; no per-world ambient, fog or lights | rendering | **HIGH** (for horror) | real darkness, light changes | dark albedo; coloured boxes read as lamps; props that drop into view as "lights" |
| E4 | no scenario rules: Slayer HUD, rifles, no win state | rules / objectives | **HIGH** (for "feels like a game") | a proper end | `shift_complete` relay + teleport per player |
| E5 | no timer / delay | engine (X7) | MEDIUM | - | movers as clocks (`facility_cycle`, the core's rise, the pump) |
| E6 | Lua is reachable only through `on_used` | scripting | MEDIUM | - | hidden interactable + a binding's `use` |
| E7 | the Lua bridge inherits a button's 0.5 s cooldown | scripting / engine | MEDIUM | - | none; a second entry within 0.5 s is dropped |
| E8 | no `left` / "while inside": hazards only on entry | engine (X7) | MEDIUM | pressure plates, continuous damage | two puddle triggers side by side |
| E9 | world surfaces are flat colour only (no textures, no text) | rendering | MEDIUM | signage, decals | props for every sign (entities) |
| E10 | audio: no loops, no stop, no occlusion | audio | MEDIUM | ambience bed, a proper alarm loop | replays on the heartbeat |
| E11 | prefabs have no per-instance parameters | content model | MEDIUM | labelled lamps, per-door speeds | one generic lamp + one sign |
| E12 | movers never push or stop players | collision | MEDIUM | a plume that fills the tunnel | plume stops above head height |
| E13 | conditions have no OR / NOT; no AND node | engine (X7) | LOW | - | 3 scald bindings; mirrored AND pair |
| E14 | actions act on the event's actor only | engine (X7) | LOW | "evacuate everyone" | each player rides the lift |
| E15 | no `round_started` event | engine (X7) | LOW | ambience from the first second | heartbeat starts with aux power |
| E16 | world movers: definition-only in schema >= 4, never model-drawn | content model | LOW | - | prefabs for anything textured that moves |
| E17 | bot nav grid built with 0 bots | AI / performance | LOW | - | none; ~0.5-0.6 s of every load |
| E18 | long refusal messages truncated at 192 bytes | debugging | LOW | - | read the OAL checker instead |
| E19 | menus list the world's file name, not its display name | UI | LOW | - | name the file well (`night_shift`) |
| E20 | spawns cannot change with world state | rules | LOW | checkpoints | the dock stays connected (tunnel shutter) |
| E21 | `megamod-match` exits without freeing its session | tooling | LOW | - | LSan exit reports on every world (pre-existing) |
| E22 | `megamod-join --route` is 512 bytes | test tooling | LOW | - | the crew split across joiners |

## Entries

### E1 -- 64-entity cap (HIGH)
- **Trying to build:** a facility with four powered doors, per-room signs, lamps on every panel.
- **System:** X6 prefabs expanded into world entities; WORLD_STATE's entity index (< 64, protocol v10).
- **Awkward:** every entity counts, including ones that never reach a client (relays, triggers, a Lua hook, static sign props). A security door is 6 entities; three of them are 29% of the world.
- **Workaround:** yes -- cut D2 and D5, frames as world boxes, 4 signs, one light. Final: 62/64.
- **Blocked content:** the slice fits; its natural growth (15-30 min target, more rooms) does not.
- **Possible solution:** index only wire-visible entities (29 of the 62 never reach the wire), or widen WORLD_STATE's index (protocol).
- **Belongs in:** networking + engine.

### E2 -- relay state is invisible to joiners (HIGH)
- **Trying to build:** "is this door powered?" readable by every player.
- **System:** X7 relays remember `active`/`inactive` host-side only (not replicated).
- **Awkward:** any state a player must see has to be a mover that moves: each door carries a lamp mover, the research gate two status lamps.
- **Workaround:** yes (the lamps), at 1 entity per indicator.
- **Possible solution:** replicate relay state, or a material/visibility swap driven by a condition.
- **Belongs in:** networking / engine.

### E3 -- lighting (HIGH for this genre)
- **Trying to build:** darkness, emergency lighting, lights that die at lockdown.
- **System:** original worlds are unlit flat colour; models (props, prefab movers) are lit by the scene light, which on the desktop joiner makes a (36,38,42) door read as light grey.
- **Awkward:** no per-world ambient, fog, light sources or emissive; "the lights go out" can only be a prop moving away. Prop brightness differs from the world's by ~3x, so textures are authored dark by eye.
- **Workaround:** dark albedo everywhere, saturated boxes as fixtures, the dock light and red beacons as movers.
- **Blocked content:** real darkness / flashlight play; light changes that affect the room.
- **Belongs in:** rendering.

### E4 -- rules and objectives (HIGH for "feels like a game")
- **Trying to build:** a co-op scenario with a start and an end.
- **System:** the host's game modes (Slayer, Team Slayer, CTF).
- **Awkward:** players spawn with an assault rifle and pistol (weapon viewmodel on screen), friendly fire is on (FFA), the HUD says "In 1st place with 0 Frags", "kills to win 25" could end the round and reset the world; nothing marks the shift complete for anyone.
- **Workaround:** `shift_complete` relay + a teleport per player; host with 0 bots, no time limit.
- **Possible solution:** a minimal scenario/objective ruleset (no weapons, no score, a completion state) -- a rules milestone, not a framework.
- **Belongs in:** rules / objectives.

### E5 -- no timers (MEDIUM)
- **Trying to build:** a repeating alarm, periodic plant noise, steam on a cycle, a delay before lockdown.
- **System:** X7 (no timers by design).
- **Workaround:** a hidden `facility_cycle` mover (4 s travel) that re-opens itself on `closed` while `aux_power` is active; the core's 1.7 s rise; the pump's stroke. Works, is bounded (`opened`/`closed` are new roots), but the hidden cycle costs an entity that draws nothing, and intent hides in speeds and distances (4 s = 0.5 wu at 0.125 wu/s).
- **Possible solution:** a `timer` entity or a `delay` action argument.
- **Belongs in:** engine (X7).

### E6 -- Lua entry points (MEDIUM)
- **Trying to build:** a trigger (walking into the cold spot) that runs custom logic.
- **System:** X3 callbacks: only `on_used` (and `on_ability`).
- **Workaround:** a hidden interactable (`cold_spot_hook`, reach 0.05, inside a wall) with the script, called by a binding's `use`. Costs an entity; reads as a hack in the data.
- **Possible solution:** a `call`/`script` binding action, or `on_entered`/`on_activated` callbacks.
- **Belongs in:** scripting.

### E7 -- the bridge's cooldown (MEDIUM)
- **Observed:** D and E entered the cold spot 0.27 s apart; the second `use` was dropped: `[bind] use: cooling down, nothing happens` (`HTA_WENT_COOLDOWN` 0.5 s). Harmless here (they were together); a lone player entering just after someone else would not be judged at all.
- **Possible solution:** as E6; or no cooldown for binding-issued uses.
- **Belongs in:** scripting / engine.

### E8 -- no `left`, no "while inside" (MEDIUM)
- **Trying to build:** a live floor that hurts while you stand in it; a pressure plate.
- **System:** triggers emit `entered` once per outside -> inside transition.
- **Workaround:** two adjacent puddle triggers (20 each); standing still is safe. Steam: damage only when entering under a plume.
- **Belongs in:** engine (X7): `left`, or a repeating `inside` with a period.

### E9 -- world surfaces are flat colour (MEDIUM)
- **Trying to build:** signage, room names, warning stencils.
- **System:** OAL compiles boxes to solid-colour textures (with a fixed 0.9 checker); textured surfaces exist only on models.
- **Workaround:** every sign is a prop (an entity, E1).
- **Belongs in:** OAL + rendering (textured world boxes / decals).

### E10 -- audio (MEDIUM)
- **Trying to build:** an ambience bed, an alarm that loops until stopped, sound that is muffled by walls.
- **System:** X5 sounds, played once at an entity, 2..40 wu attenuation, no occlusion.
- **Workaround:** the heartbeat replays `machinery` / `alarm`; 52 of 108 actions are `play_sound`.
- **Belongs in:** audio.

### E11 -- no per-instance prefab parameters (MEDIUM)
- **Trying to build:** status lamps labelled COOLANT and SECURITY; a lift gate slower than a door.
- **Workaround:** one generic lamp + one sign board; the lift is an ordinary door.
- **Belongs in:** content model (X6).

### E12 -- movers pass through players (MEDIUM)
- **Observed risk:** a steam plume dropping to the floor would enclose a body; a closing door can shut on a player (docs/WORLD_ENTITIES: "A mover does not push or stop on players").
- **Workaround:** plumes stop at 0.85 wu (a body is 0.7); doors close only on lockdown, away from the crew.
- **Belongs in:** collision.

### E13 -- condition logic (LOW)
- `scald_opening` / `scald_open` / `scald_closing` for "plume not closed"; the research AND is two mirrored bindings. Readable enough; grows quadratically for larger ANDs.

### E14 -- actions on the actor only (LOW)
- Fine for co-op (each player rides the lift); "seal everyone in" or "shock whoever is in the room" is not expressible.

### E15 -- no `round_started` (LOW)
- The plant's heartbeat starts at aux power; the annex is silent before (which suits it).

### E16 -- world movers (LOW)
- `nightshift:entity/facility_cycle: a world with props or mover sounds (world_entities schema 4) gives every mover a definition; inline movers are schema 1 only` -- the first error of the project. World-level movers can only be drawn as a coloured box; anything textured that moves had to be a prefab.

### E17 -- nav grid (LOW)
- `[world] nav: 33458 nodes in 518 ms` (desktop), `608 ms` (emulator): the bot grid is built for a world hosted with 0 bots and no bot content -- the largest part of load.

### E18 -- truncated errors (LOW)
- The X6 engine's refusal of Night Shift prints `... prefab nightshift:prefab/breaker_panel: unknown f` (`HTA_ERRLEN` 192). Long package chains will hit this in the current build too.

### E19 -- menu names (LOW)
- Create Game lists `NIGHT_SHIFT` (file name), not `Night Shift: Harrow Annex`.

### E20 -- spawns are static (LOW)
- Respawns and late joiners always start in the dock; the design keeps the dock connected in every state instead.

### E21 -- `megamod-match` teardown (LOW, pre-existing)
- LSan at exit: Blood Gulch 54 MB, the X7 world 20 MB, Night Shift 24 MB (124 allocations, all held by the session) -- the session is never freed. Not Night Shift's; the X7 runtime and the package loader leak nothing (the bench frees 50 loads cleanly under LSan).

### E22 -- route length (LOW, test tooling)
- `megamod-join --route` holds 512 bytes; a whole play-through does not fit one joiner, so the test uses a crew (which is the point anyway).
