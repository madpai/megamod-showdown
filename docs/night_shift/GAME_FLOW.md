# MegaMod: Night Shift -- game flow

**Status:** first playable vertical slice, 2026-09-27. This is the
authoritative design summary for the original slice. X8, X9 and the later
[scenario rules](SCENARIO_RULES.md) build on it without changing this route.
Source: Open Asset Lab
`projects/night_shift/` (world `world01.py`, prefabs `facility.py`, art and
sound `art.py`, the one script `scripts/anomaly.lua`). Test:
`scripts/test_night_shift.sh`. Related: [STATE_GRAPH](STATE_GRAPH.md),
[PACKAGE_GRAPH](PACKAGE_GRAPH.md), [ENTITY_BUDGET](ENTITY_BUDGET.md),
[PLAYTEST_NOTES](PLAYTEST_NOTES.md), [FINDINGS](FINDINGS.md).

## Premise

HARROW ANNEX, a research annex, stopped answering at 02:10 during the night
shift. A recovery crew (2-4 players) comes in through the loading dock with
one job, printed on the work order facing them at the spawn: **recover the
data core from the research wing**. Nothing else is said. What happened is
left to the dark, the barred specimen cells, the broken tank, the stains,
the knocking, and the thing in the research wing's cold spot.

In the original slice there were no NPCs, inventory or combat goals;
players still carried Showdown's rifles (see FINDINGS). The facility is
the threat. The later Scenario rules give the crew one sidearm.

## Areas

```
    y
   24 |                    +-----------+
      |                    |   CORE    |==service tunnel==+
      |                    |  CHAMBER  |  (sealed until   |
   16 |                    +---| |-----+   lockdown)      |
      |                  +-----------+                    |
      |  cell --passage->| SPECIMEN  |                    |  steam vent (north)
    9 |      corridor -D3-> wing --->|   HALL    |         |
    6 +--+--------+ |    +-----------+                    |
      |  PUMP  |CONTROL| |                                 |  lift breaker
    0 +--------+-------+ |                                 |
      |  AUX   |       | |                                 |
   -4 +--------+       +D1+---------+----steam vent (south)+
      |   ^ passage    |  DOCK  [shutter]
  -12 |   +------------|  spawn [LIFT] -> surface (x 30..42)
      +-----------------------------------------------------> x
      -16      -9     -0.8  0.8   4  7          17.4 18.6
```

| Area | Role | What is there |
|---|---|---|
| Loading dock | spawn, first lesson, final exit | work order sign; D1 (dead security door); the freight lift (dead); crates, a truck; amber fixtures; a ceiling light that is off |
| Maintenance passage | the only open way at the start | low, dark; a distant crash when you walk it in the dark |
| Auxiliary power room | objective 1 | the generator; the breaker behind it |
| Main corridor | the spine | long, low, a few red fixtures; D3 and its status board; a collapse at the north end; knocking behind D3 |
| Control room | objective 2a | dead monitor wall, the security console |
| Pump room | objective 2b, first hazard | flooded floor with a live cable; the coolant valve across the water; the pump |
| Research wing | the anomaly | barred specimen cells; the cold spot |
| Holding cell | where the anomaly puts you | pitch dark, one way out |
| Specimen hall | dread | four tanks, one broken, glass and a stain on the floor |
| Core chamber | objective 3, escalation | the data core on its pedestal; cyan conduits; the sealed tunnel shutter |
| Service tunnel | escape, second hazard | long, low, red; two steam vents on the plant's cycle; the lift breaker |
| Freight lift / surface | exit, completion | the lift car; the surface yard under the sky |

## Objective sequence

1. **Restore auxiliary power.** Both doors in the dock buzz (`locked`) and
   stay shut; the work order and the `AUX POWER / MAINTENANCE` sign point
   at the only open way. The passage (a crash far away, `distant_bang`,
   while the annex is dark) leads to the aux room; the breaker is behind the
   generator. Throwing it: the generator spins up, the plant's heartbeat
   starts (a distant thud every 8 s), the dock light drops on, D1's lamp
   lights green with a power-up tone. D1's button now opens it.
   *This is the tutorial: dead thing buzzes; power makes the lamp green;
   green means the button works.*
2. **Research access: coolant + security.** D3 buzzes; the board beside it
   (`RESEARCH WING ACCESS / NEEDS COOLANT + SECURITY`) has two empty lamp
   sockets. Walking up the corridor while D3 is dead, something knocks on
   it from the inside. The security console (control room) and the coolant
   valve (pump room, across the flooded floor: the live water hurts, walk
   round it) each light one lamp; the valve also starts the pump. The
   second one done powers D3 (its lamp, a power-up tone where you stand and
   at D3). Either order; either player.
3. **The data core.** Through D3 and the research wing. A player who walks
   into the wing's **cold spot alone** is taken -- moved to a pitch-dark
   holding cell, a chord of breath and beating tones -- and must find the
   passage out into the specimen hall; a group walking in together only
   hears it stir, once. Past the tanks, the core chamber. Using the core:
   it unlatches and rises out of its pedestal (1.7 s), and when it has
   risen clear --
4. **Lockdown (escalation).** The alarm sounds at the core and then keeps
   sounding on the plant's heartbeat (every 4 s, at three red beacons and
   at D1); red beacons drop from the ceilings; the dock light dies; **D1
   loses power and slams shut** (the way in is gone); **D3 slams** behind
   the crew; the **service tunnel's shutters grind open** at both ends; the
   tunnel's steam vents start firing on the heartbeat.
5. **Escape.** The tunnel runs from the core chamber round the east side
   and back to the dock. Something bangs and knocks behind you as you go
   (post-lockdown stinger). Steam vents scald anyone who walks under a
   plume (30 each); wait for it to lift. Halfway, the **lift breaker**:
   throwing it powers the freight lift (its lamp, its tone, back in the
   dock). The tunnel comes out in the dock beside the lift. The lift's
   button opens it; stepping into the car takes that player to the surface
   (a bell and a motor), sets `shift_complete` and plays the closing chord.
6. **Win state.** Each player who reaches the surface is out. There is no
   scoreboard or match end (see FINDINGS: rules). `shift_complete` is the
   world's record that the shift ended.

Designed length: 5-10 minutes for a crew that knows nothing (the
automated crew takes 161 s wall-clock for the whole chain, walking in
straight lines with no hesitation). The prompt's 15-30 minutes needs more
content than 64 entities allow (ENTITY_BUDGET).

## Relay / state model

World relays (hand-placed): `aux_power`, `security_link`, `coolant_flow`,
`lockdown`, `shift_complete`, `anomaly`. Prefab relays: each security
door's own `power` (`d1__power`, `d3__power`, `lift__power`). Movers that
carry visible state (because relays are host-only and not replicated):
door lamps, status lamps, the dock light, red beacons, shutters, the core.
Timers: the hidden `facility_cycle` mover (its 4 s travel is the only clock)
and the core's slow rise. Details: [STATE_GRAPH](STATE_GRAPH.md).

## Key prefabs

`security_door` (3 placed: D1, D3, the lift), `breaker_panel` (2), `console`,
`valve`, `status_lamp` (2), `ceiling_light`, `alarm_light` (3), `shutter`
(2), `data_core`, `steam_vent` (2), `pump`. See
[PACKAGE_GRAPH](PACKAGE_GRAPH.md).

## Custom Lua points

One script, `nightshift:script/anomaly` (32 lines), on one hidden
interactable (`cold_spot_hook`, reach 0.05, buried in a wall) that the
binding `cold_spot` (`cold_spot entered -> use cold_spot_hook`) calls. It
exists because X7 cannot read **how many other players stand near the
actor** (`game.near`) and has no counter (`at most twice a round`). Every
other behaviour in the world is bindings. See FINDINGS "Lua usage".

## Late join and respawn

Late joiners and the respawned spawn in the dock. Before lockdown D1 leads
on (once powered); after it, the dock's tunnel shutter is open, so a late
or respawned player reaches the lift breaker and the lift from the dock
side. Nobody can be sealed in: D3's buttons work from both faces while it
has power, and the holding cell has an exit.
