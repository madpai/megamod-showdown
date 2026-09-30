# MegaMod Racing X10: Cinder Circuit

## Capability and content boundary

`src/engine/arcade_racer.*` advances a compact native arcade vehicle from
bounded throttle, brake, steer and drift input. It owns traction, speed,
steering, useful drift work, tiered release boost, pad boost, airborne
control, wall response and bounded racer contact. It consumes generic
collision triangles and a validated `hta_arcade_tuning`; it reads no Halo
vehicle tag. Historical `src/engine/vehicle.*` remains the Trial vehicle
compatibility path.

`src/game/race.*` owns READY, COUNTDOWN, GO and RESULTS; ordered gate crossing,
laps, ranking, finish order and recovery anchors. `src/app/racing.*` connects
the two to Showdown units, an eight-slot grid and session lifecycle. A
finished race displays RESULTS for eight seconds, then resets without
reloading the engine. An active racer may join only before GO. Later peers
spectate until the next round.

The selected roster `hta_unit.character` remains the player's identity and
is replicated through the normal unit state. The kart supplies handling;
character run speed, flight, weapons and abilities do not act while racing.
The first build hides the driver body and draws the kart while retaining the
character in the setup picker and session state. Imported seated animations
are not sufficiently uniform for a reliable visible driver yet.

## Original authored track

Open Asset Lab's `projects/megamod_racing/` builds `racing.assets`,
`racing.trackkit` and `racing.track01`, yielding `cinder_circuit.oalmap`.
Everything in these packages is original or synthesized. `racing.trackkit`
contains reusable checkpoint-gate and boost-pad prefabs. OAL compiles an
authored centerline into visible and collidable triangle ribbon, including
a banked east curve (outer edge up to 2.2 world units), real ramp crest and
gap. The route has a start straight, broad and technical turns, three pads,
eight ordered gates, an elevated jump and eight staggered grid slots. Its
105 authored stations form a 1,901.6-world-unit centerline.
Materials, local lights and environment use X9. A scripted solo route took
181.55 seconds for three laps, about 60.5 seconds per lap, peaking at the
authored 52 world-unit/s pad/boost cap. It is a development route, not a
human feel measurement.

The `world_entities` schema 8 racing section stores the vehicle model
resource reference, tuning, grid, gate planes, recovery anchors and pads.
The package dependency graph and world key cover all race-critical content,
including triangle collision, checkpoint order and tuning. Parsing resolves
references once and bounds racers to eight, gates to 32, pads to 16 and
all values to finite validated ranges. Schema 1–7 continue to load.

## Driving and presentation

The first Hyperkart tune has base cap 38 world units/s, boost cap 52,
acceleration 18, normal grip 12, drift grip 3.2 and steering response
decreasing from 2.5 to 0.9 across speed. Those are original tuning choices
in the OAL track configuration, with defaults and bounds in
`arcade_racer.*`. Holding drift without useful turn/slip work earns no
maximum boost. Releasing a meaningful drift grants a bounded tier and
duration. A pad applies the same native boost operation. Ground and wall
collision are substepped at 120 Hz with sweeps; racer contacts use bounded
separation and impulse plus a swept relative segment across the host frame,
so opposing karts cannot cross between discrete contact samples. Reset
returns to the most recent valid gate anchor.

Android adds only GAS, joystick steer/brake/reverse, DRIFT and RESET. The chase camera
uses speed/boost field of view and restrained lag. The HUD shows countdown,
position, lap, speed, wrong-way, drift/boost and finish place. Six small
original PCM cues cover motor, drift, boost, impact, checkpoint and finish.
The motor loop changes pitch with speed; it is still a simple synthesized
first pass. Pause's optional diagnostics show speed, lateral slip, steering,
drift time/work, boost tier/time, grounded state, gate, lap and FPS. No Flux,
items, AI, track editor or Party rules are in X10.
During a useful drift the HUD shows one to three charge marks and the drift
cue confirms each threshold; release converts that tier into a bounded boost.

## Host, protocol and validation

The host simulates vehicles, checks gate crossing, advances laps, determines
position/finish and chooses reset. Peers send bounded CONTROL axes/actions;
they cannot announce a lap. Existing WORLD snapshots carry kart pose and
velocity; protocol v13 extends GAME by 52 bytes for phase, countdown,
elapsed race time and eight six-byte racer summaries. Clients interpolate
and extrapolate between host snapshots; there is no rollback or driving
prediction. World/package mismatch and old protocol peers refuse to join.
The desktop localhost one-peer run received 109,615 bytes and sent 19,248
over roughly 16 seconds, around 6.9 and 1.2 KB/s. Latency and correction
under real Wi-Fi packet loss remain unmeasured.
`test_net` exercises a v12-shaped HELLO over a real UDP socket against the
v13 host and a v13 client against a v12 probe answer; both refuse with an
explicit version reason.

`scripts/test_racing.sh` builds the real OAL project twice, compares bytes
and keys, runs it first against a public generated synthetic bootstrap cache,
drives the 3-lap route through all gates, rejects a shortcut and a wrong-world
peer, checks round reset and joins a real UDP peer. C tests cover
parser/rules/handling/codec bounds. The track has 654 render triangles, 624
solid triangles and 27 prefab props. Headless solo simulation cost was about
0.01 ms/frame in the development run; it does not measure rendering, eight
racers or the physical phone. The Android 14 SwiftShader emulator loaded the
track, selected Dragonborn from the shared roster, accelerated on touch and
RESET returned to grid. It rendered around 33–46 FPS with 104–109 draws;
emulator software-GPU FPS is not an S24+ prediction. The full public
205-second route also completed under ASan, UBSan and LSan without memory
errors or retained session allocations (about 0.08 ms/frame under
instrumentation). The headless match tool now frees its session and collision
index; Android shutdown frees the same effects/index state.

## Limits and observed surprises

- The app still opens a Halo-format cache before an original-world match.
  `mkfixture` generates an original synthetic one, so desktop Racing needs
  no private foreign assets. The Android guest app still needs a compatible
  cache supplied by its user; removing this bootstrap coupling is open.
- The first emulator pass inherited Trial sky geometry and hid race HUD text
  through a Java condition. Both were corrected before final build.
- The script route records substantial airborne time; a physical drive must
  distinguish intended jump time from weak ground adhesion. Banked contact
  and human steering feel still require owner review on S24+.
- The OAL track uses authored Python centerline/station indices. It is
  deterministic, but spatial editing is laborious without a plan-view tool.

Physical S24+ FPS, frame pacing, touch latency, subjective speed and thermal
behavior remain for the owner to test in the published APK. The next
milestone should be chosen from that evidence; X10 does not add items.
