# Native multiplayer architecture

Status: original design and baseline audit for the first network slice,
2026-09-21. For implemented state and test results, read
`NETWORK_PROGRESS.md` (protocol v7 on Megamod as of 2026-09-24). The
baseline statements below describe the build before networking and are
historical, including references to a future session and old pool limits.
The pre-network build is preserved at
`net-baseline-2026-09-21` (`ecc99b8`); `scripts/verify.sh` passed
57/57 checks with the owner's Blood Gulch map before any networking edits.

## Current engine, as audited

The current playable loop is `android_main` in `src/platform/platform_android.c`.
It gathers touch/gamepad input, advances vehicles and `hta_player_update`, then
weapons, vitals, pickups, bots, projectiles, particles, animation, audio and
Vulkan drawing using frame delta from `hta_time_seconds()` (monotonic clock).
There is no fixed game tick yet. Android owns local weapon slots, ammo, player
vitals, projectiles, grenades, pickups and death/respawn policy. These are
currently authoritative only to the local process. The portable engine modules
already implement player movement/collision (`player.c`), vehicle simulation
(`vehicle.c`), skinned world actors (`actor.c`), a hittable actor/vitals wrapper
(`bot.c`), ammo/weapons, projectiles/grenades, health/shields, pickups and
animation. `engine.c` is a small standalone input/render-state core; it is not
the live gameplay coordinator. The map/cache, scenario spawns, cyborg physics,
weapon tags and render assets are loaded from each player's own Trial data.

`hta_actor` is the world-space Spartan renderer used for corpses and the target
bot. Remote players should use that same actor/model/animation path. The Vulkan
draw path at baseline allowed eight dynamic meshes, seven already used; adding
multiple independent remote-actor meshes needs a capacity change or a batched
actor mesh. The limit has since been raised to ten for the first remote actor.
At baseline, desktop `htaview` was an offscreen inspector, not an interactive
game client. The Android layer was the only playable runtime. Its Java
classes handle setup, asset access and touch HUD; native C handles gameplay.

## Boundaries

```
local input ──> session/simulation ──> replicated game state ──> actor renderer
                    ▲                      │
                    │                      ▼
              server input queue      protocol codec
                                           │
                                      UDP transport
```

Portable `src/net/` modules own protocol encoding, validation, transport,
session registry, snapshots, events and statistics. They must compile in host
tests without Vulkan, audio, JNI, Android or Trial assets. Android and future
desktop runtimes only pump a nonblocking session and present the resulting
state to the existing engine. No packet decoder calls rendering or tag loaders.
Each client loads Blood Gulch independently; no map files cross the wire.

For the first slice, preserve local single-player behavior when networking is
off. Network mode will have a separate portable `GameState`/player registry.
The existing `hta_player_update` and collision routines are the movement
implementation to reuse as the server takes ownership of transforms. The
initial server may run inside a host process; a future headless executable uses
the same registry and collision data with no renderer.

## Authority and staged integration

The server allocates player IDs, owns player existence and publishes snapshots.
Clients send bounded input/intention. The server must eventually run movement,
vitals, damage, weapons, pickups, projectiles, vehicles and game mode state.
The first transport test runs without assets or rendering. A temporary visual
slice may let clients submit their locally simulated transform as a *provisional
movement input* while the portable server loop is being connected to the
existing collision/physics data. That exception must be explicit in code and
must not extend to damage, pickups or arbitrary entities. It is not a secure
authoritative movement implementation. Combat starts as cosmetic events; the
server later validates ammunition, ray hits, damage, health, death and respawn.

Snapshots contain ID, feet position, yaw/pitch, velocity, grounded/crouch flags,
held weapon index and action flags. Remote actors buffer two or more snapshots
and render roughly one snapshot interval behind, interpolating position and
shortest-path yaw. The local player stays responsive and will need reconciliation
once server movement is active. Spawn selection comes from scenario player
starts. A join/leave changes the registry, not the BSP or renderer's packet
code. Vehicles and bots eventually use the same entity registry with different
types; this phase does not serialize them.

## Transport and protocol

Use UDP sockets through a narrow portable C abstraction. UDP is available via
the Android NDK and Linux libc, supports nonblocking send/receive without a
Java network stack, and leaves real-time snapshot scheduling to the session.
No framework, discovery, NAT traversal or reliability library is needed for
this LAN-first slice. Handshake and disconnect use bounded retransmission;
snapshots are replaceable. Events will require sequence/deduplication and
retransmission if visual loss is unacceptable. Socket polling must never block
the render thread. A thread or bounded per-frame drain can both satisfy that.

All wire fields have explicit little-endian encoding and fixed widths. Header:
32-bit magic, 16-bit version, 8-bit type, 8-bit reserved, 32-bit sequence,
32-bit simulation tick, 16-bit payload length, 16-bit reserved. The datagram
length must equal header plus declared payload; no raw C structs go on the
wire. Version 1 starts with HELLO, WELCOME, DISCONNECT, PING and PONG, then
INPUT, SNAPSHOT and EVENT. Unknown types, bad flags/reserved fields, wrong
lengths, nonfinite coordinates, invalid IDs, excessive counts and stale
sessions are rejected before touching game state. Hard caps prevent packet
driven allocation. The server binds replies to the sender address and an
assigned session token; player IDs alone never authorize an update. A newer
wire version must negotiate explicitly or fail.

Halo Trial compatibility, if ever attempted, is a separate adapter from the
replication model. The native protocol neither copies Halo's packet layout nor
depends on its encrypted handshake.

## Sequence of proof

1. Asset-free host test: server, one and then two clients, handshake/ID,
   ping/pong, version mismatch, disconnect, malformed packets.
2. Snapshot and event codec tests, then loopback replication for two players.
3. Interactive desktop host/client path into Blood Gulch, remote actors and
   interpolation. Verify with two running instances and record bandwidth,
   latency and loss behavior.
4. Android client against desktop host, then two Android devices if available.
5. Move movement and combat authority into the headless simulation in small
   increments. Damage/death/vehicles/game modes are outside this phase's
   stopping point unless needed for the requested visual interaction.

Statistics will count datagrams/bytes in and out, invalid/dropped datagrams,
snapshots, and ping RTT over a rolling interval. Local scripts should launch
the server and two clients without assets for protocol tests; interactive
scripts will require the owner-supplied map path. No proprietary data belongs
in Git or an APK.
