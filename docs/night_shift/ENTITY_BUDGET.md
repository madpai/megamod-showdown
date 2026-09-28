# Night Shift -- entity budget

WORLD_STATE indexes world entities with a value below 64 (protocol v10), so
a world may expand to at most **64 entities** (`HTA_WDEF_MAX_ENTITIES`;
props count, relays count, a hidden hook counts). The limit was not raised.
`assetlab project budget projects/night_shift` prints this table from the
real expansion; the engine reports the same count at load (`[world] 62
world entities`).

## Final slice: 62 of 64 (headroom 2)

| | Count |
|---|---|
| hand-placed entities | 21 (relay 6, trigger 7, prop 4, teleport 2, interactable 1, mover 1) |
| prefab instances | 19 |
| entities expanded from prefabs | 41 |
| **total** | **62 / 64** |
| bindings | 59 / 128 (27 world, 32 from prefabs) |

By kind (after expansion): mover 19, interactable 12, prop 11, relay 9,
trigger 9, teleport 2.

| Prefab | Instances | Entities each | Total | Kinds |
|---|---|---|---|---|
| `security_door` | 3 | **6** | **18** | 2 interactables, 2 movers (door, lamp), relay, prop |
| `breaker_panel` | 2 | 2 | 4 | interactable, prop |
| `steam_vent` | 2 | 2 | 4 | mover, trigger |
| `alarm_light` | 3 | 1 | 3 | mover |
| `console`, `valve`, `data_core` | 1 each | 2 | 6 | interactable + prop/mover |
| `status_lamp`, `shutter` | 2 each | 1 | 4 | mover |
| `ceiling_light`, `pump` | 1 each | 1 | 2 | mover |

| Hand-placed | |
|---|---|
| relays | `aux_power`, `security_link`, `coolant_flow`, `lockdown`, `shift_complete`, `anomaly` |
| triggers | `passage_stinger`, `corridor_stinger`, `tunnel_stinger`, `puddle_a`, `puddle_b`, `cold_spot`, `lift_car` |
| teleports | `holding_cell`, `surface` |
| props | 4 signs (`sign_orders`, `sign_aux`, `sign_research`, `sign_lift`) |
| mover | `facility_cycle` (the hidden clock) |
| interactable | `cold_spot_hook` (Lua's entry point; nobody can press it) |

**Which types consume the most:** the security door (6 each, 29% of the
world for three doors). Per role: *state made visible* costs the most --
every piece of state a joiner must see is a mover (door lamps, status
lamps, beacons, the dock light) because relays are host-only; 11 of the 19
movers exist only to show state or to keep time.

## Did 64 become a real constraint? Yes -- it shaped the design

It never blocked a build, because the design was cut to fit before
authoring. Evidence of the cuts (first plan -> slice):

| Wanted | Cost | Decision |
|---|---|---|
| a door between the corridor and the control room (D2) | 6 | **cut**: an open doorway; the console needs no power gate because D1 already gates the wing |
| a door into the core chamber (D5) | 6 | **cut**: an open vestibule; D3 is the research gate |
| door frames as props (the X6/X7 prefab had 3) | 3 per door | **moved to world boxes** (free, but untextured and not part of the prefab: every placement re-authors its frame) |
| the "power" indicator per door | 1 per door | **kept** (the lamp): readability over budget |
| buttons on both faces of a door | 1 per door | **kept**, sharing one through-wall plate prop instead of a second panel |
| pressure plates, "hold to open" | would need `left` + more relays | not possible anyway (no `left` event) |
| per-area signs (control, pump, core, tunnel) | 1 each | **4 kept**, the rest cut; room identity is colour and layout |
| a second anomaly reaction (grouped players) | 1 relay | **cut**; Lua reuses the same `anomaly` relay |
| a lamp per breaker, per console | 1 each | **cut**; sounds carry it |
| lights that change on power (more than one) | 1 each | **one** dock light; the rest of the "lighting" is static coloured boxes |
| more rooms / the 15-30 minute target | ~10-20 | **not attempted**: 2 entities left |

Without the cap, the natural next content (a fourth door, a second hazard
room, a real generator puzzle, per-room signs) would add ~20-30 entities.
The 64-entity limit is the first thing a second Night Shift world would
hit. See [FINDINGS](FINDINGS.md) for its rank.

## Ways to fit more (not done; for the record)

- An entity that never appears on the wire still takes a WORLD_STATE
  index today. On the wire an index names a mover (its state) or a
  sound's position (`HTA_NET_FX_WORLD_SOUND`). Here 19 movers and 14 other
  sound positions do; the other **29** (9 relays, 12 interactables
  including the Lua hook, 4 signs, 4 silent triggers) never do. Indexing
  only wire-visible entities would leave 33 of 64 used (engine and
  WORLD_STATE work: not done, protocol unchanged).
- Props could be baked into world geometry by OAL when they never move
  (textures on world boxes would remove most sign props).
- A door prefab could lose its lamp if a relay's state were visible to
  joiners (replicated relays or a material swap).
