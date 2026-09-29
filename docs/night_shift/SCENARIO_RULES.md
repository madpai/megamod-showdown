# Night Shift scenario rules and flashlight

**Status:** first dedicated rules slice after the physical X9 review. The
`night_shift_x9` package, its key, authored lights and route are unchanged.
This is a Showdown runtime adapter for that world; it is not a general
scenario declaration format or a completed PvE mode.

## What the slice does

- Selecting `night_shift_x9` starts the new `Scenario` game mode instead of
  Slayer, including on the desktop host. It disables the frag and time limits,
  Slayer start announcement, kill score, postgame scoreboard and automatic
  round reset. The roster remains available for character choice.
- Human crew members cannot damage each other, and native roster abilities
  are inactive while this mode owns the ability button for the flashlight.
- The recovery crew starts with one Trial pistol, no frag grenades and no
  Blood Gulch pickups. There are no bots or creatures in this slice. The
  weapon's native magazine and reserve still set ammunition; this is not a
  new inventory or survival-resource system.
- The HUD shows the next broad objective from the host's replicated power,
  access, lockdown and lift relays. The touch SWAP button reads **USE** when
  a world control is in reach. Completion is host authoritative: the
  existing `shift_complete` relay becomes active when the first player
  reaches the lift surface, then the mode shows **Shift complete** and stays
  complete. A late joiner receives the replicated `over` state and relay
  state; no event replay is needed for the banner.
- The ability touch button toggles a camera-mounted spot light labelled
  **LIGHT ON/OFF**. It uses one of the renderer's existing eight local-light
  slots, replacing the farthest selected authored light when the budget is
  full. It has no battery or AI visibility rule. Its toggle is currently
  local visual state, so a peer does not see the beam and late join does not
  reconstruct it. World fixtures and X9 authored lighting still replicate
  through their original relays.

The light has 12 wu range, intensity 2.5, near-white RGB
`(0.92, 0.95, 1.0)`, inner cone cosine 0.94 and outer cosine 0.78. These
are first-pass presentation values the owner found useful on an S24+, not an
X9 lighting retune. The world package and its source assets were not edited.

## Boundaries and next evidence

`Scenario` is a small rules boundary motivated by Harrow Annex. The world
still owns doors, power, hazards, route logic and the completion relay through
X7 bindings and host Lua. The mode currently recognizes the X9 world by ID;
moving this to validated generic mode requirements would need a second real
consumer. Protocol v12 admits the new mode value and intentionally refuses
older peers; WORLD_STATE's compact representation is unchanged.

The 2026-09-29 S24+ solo run on build `56b7f0e` completed the lift route.
The flashlight was on when the report was sent. The owner says it worked
great and everything worked, but the UI felt cluttered. Two SEND REPORTs
confirm Scenario mode,
`match.flashlight_on` and `match.scenario_complete`; the app log records the
lift completion. On High at render scale 1 and 2x MSAA, the last minute
measured 120.0 mean FPS, 11 ms p99, no hitches and no dropped audio voices.
The only >50 ms session hitch was the first frame (206 ms). The report does
not establish how long the light was on or its isolated frame cost. The
reports are solo, so host/late-join agreement is still unmeasured on a
physical phone.

This presentation pass makes secondary Scenario buttons icon-only and
hides SWAP when no USE target is available. FIRE, LIGHT and USE retain their
labels. Phone review should check that the quieter controls remain easy to
find and use, and a matching-build peer should late join during lockdown and
after completion.
