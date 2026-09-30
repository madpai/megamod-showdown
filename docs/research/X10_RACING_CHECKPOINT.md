# X10 racing research and vehicle checkpoint

Status: design checkpoint recorded before implementation, 2026-09-29.
The implementation and measurements are tracked in `../RACING.md`. No external game
code, track, art, names or handling formula is a donor.

## Evidence reviewed

The current vision and capability direction, architecture, handoff,
resources, prefabs, event bindings, world state, research index, cross-engine
synthesis, entity comparison, recommendations and package-format report were
reviewed alongside Open Asset Lab's vision, handoff and research connections.
The source audit covered `engine/vehicle.{h,c}`, `game/game.{h,c}`,
`app/input.*`, `app/session_tick.c`, `app/host_net.c`,
`platform/platform_android.c`, `net/protocol.*`, and OAL's original world,
project and prefab compiler. The corpus has no racing-specific handling
study; its established lesson is to keep specialized native simulation,
host authority, typed content identity and authored world data, without an
ECS rewrite or a general mode framework.

## Current vehicle architecture (FACT)

- A vehicle type is loaded from Trial `vehi`, `phys`, model and collision
  tags. A placed car owns its pose, speed, lateral velocity, steering,
  contacts, seat occupants and gun state. Five Halo vehicle kinds select
  separate drive functions; the jeep uses a speed and steering angle, the
  scout and fighter rotate under velocity. There is no original-content
  vehicle definition or placement path.
- `hta_game` validates entry/exit, maps a seated unit's input into the
  car's control, suppresses ordinary body movement, and tracks its
  existing roster character index. The platform renders the selected
  character on a seat where its imported animation permits it. No general
  seated pose or retargeting guarantee exists.
- The host or solo game simulates vehicles; joiners interpolate packed
  VEHICLES snapshots. The 36-byte/car record includes pose, signed speed,
  wheel/aim presentation and six seat IDs. There is no driving prediction.
  CONTROL sends bounded movement axes and one-shot action count; it has no
  drift/recovery input. GAME sends existing mode/rules and hulls.
- Vehicle simulation substeps at 120 Hz. World contact queries the static
  triangle grid with sphere mass points, including sweeps for non-wheel
  points; vehicle contact uses proxy spheres and planar impulses. Support
  samples ground under wheels and estimates pitch/roll. This may follow a
  bank or ramp but has not been proved on a high-speed authored track.
  Body orientation and collision are not full 3-D rigid bodies. Motion
  against a wall may be reverted for a step. Its performance evidence is
  the historical collision-instance update reduction from 1.5 to 0.03 ms;
  no X10 high-speed or eight-racer benchmark exists.
- Android has a vehicle control, transition, status, sound and camera path.
  Chase camera distance is fixed relative to body radius. Existing
  controls were tuned for Halo vehicle kinds, not a drift button or
  high-speed steering. Roster abilities must be explicitly suppressed
  while racing; no current race mode does so.
- OAL original worlds compile boxes and generic placed entities. Static
  imported geometry can contain triangles, and engine collision consumes
  them, but original-world authoring has no sloped/banked surface primitive.
  Prefabs expand to ordinary entities, so visual gates and boost pad art
  can use them; trigger volumes are axis aligned and emit `entered` only
  for ordinary player bodies in the current shared tick.

## First capability boundary (INFERENCE)

Keep the Halo `hta_vehicles` compatibility path. Introduce a small native
arcade vehicle definition and runtime state that consumes generic throttle,
brake, steer and drift intent, plus host-owned boost and recovery requests.
Race rules should separately own grid assignment, countdown, ordered
checkpoint/lap progress, ranking, finish and reset. OAL should compile
original track geometry and race-critical configuration; the engine should
resolve/check it once when loading the world. This can be one concrete mode
and a bounded data section, without a universal rules language.

The selected roster character remains the same `hta_unit.character` and
session identity. Vehicle handling is vehicle-owned; on-foot movement,
flight, weapons and abilities should not modify it. A hidden driver body is
an acceptable temporary presentation fallback when a seated animation is
missing, provided the roster identity remains visible in UI/network state.

## Questions to measure (UNKNOWN)

- Can the current collision grid and wheel support keep a compact kart on
  a banked ribbon and ramp at the chosen speed without tunneling or spikes?
- What host snapshot cadence, bandwidth and input latency are acceptable
  for high-speed LAN and an Android peer? Prediction is not justified yet.
- Does a readable original track reach a 45–90 second lap in the current
  world unit scale, and which speed curve feels fun on the S24+?
- How many seated imported characters have a usable driver pose? Which
  need the identity-preserving fallback?
- Does original-world triangle authoring remain manageable for one track,
  or does OAL need a later spatial tool?

## Rejected scope

Do not reuse proprietary tracks/formulas; do not rewrite vehicle physics
globally, implement a general ECS, a Party/playlist framework, a track
editor, deterministic client physics, a racing AI or an item roster. The
first track and kart must be original public content. Night Shift stays a
regression fixture.

## Post-prototype evidence (FACT, 2026-09-29)

- The original triangle ribbon, bank, ramp and jump loaded through the
  existing collision grid. A scripted route completed three valid laps in
  181.55 seconds and the round reset. It did not measure human feel.
- The first emulator pass revealed two presentation assumptions were false:
  an original Racing world still inherited Trial sky geometry, and Java
  skipped `drawGame()` for racing. Both paths were corrected.
- Original track geometry is deterministic, but hand-tuning centerline
  stations and gate indices in Python is slow. A visual spatial authoring
  tool may be justified later; X10 did not build one.
- A selected Dragonborn entered Racing in the emulator and touch GAS/RESET
  worked. The driver remains hidden, since the existing import animation
  path does not guarantee seated poses for the full roster.
- The app still needs a Halo-format cache at match bootstrap. A generated
  public synthetic cache now runs the original desktop race without private
  foreign assets; Android standalone bootstrap remains open.
