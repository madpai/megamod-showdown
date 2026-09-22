# Network phase checkpoint — 2026-09-22

## What exists

The baseline at `net-baseline-2026-09-21` passed all 57 existing checks.
`NETWORK_ARCHITECTURE.md` records the live code audit and intended authority
boundary. The new portable `src/net/` code provides nonblocking IPv4 UDP,
explicit little-endian protocol version 1, a bounded eight-player registry,
HELLO/WELCOME with assigned player ID and session token, ping/pong,
disconnect, 20 Hz snapshots, and cosmetic action event relay. Host tests cover
serialization, malformed packets, version mismatch, unauthorized input,
two clients, snapshots, events, ping and disconnect without Trial assets.

The headless `htanet server` needs no renderer or assets. `htanet client` is a
synthetic movement probe. `scripts/run_two_clients.sh` launches all three
processes for the asset-free test. `htaplay` is a thin SDL2 desktop window that
loads the owner's own Blood Gulch map, uses existing `hta_player_update` and
`hta_actor`, shows one remote Spartan, and sends movement and fire/melee/
grenade/weapon intentions. It renders via the existing offscreen Vulkan path.
`scripts/run_two_players.sh` starts one server and two interactive desktop
windows. The desktop client uses the map's BSP and placed-object collision,
and its window title reports the connection and remote player ID. The Android
setup screen has Host LAN game and numeric IPv4 Join LAN
server actions. Host runs the same nonblocking server inside the Android
process and joins it through loopback; this path is compile tested only. Android
uses the same client session and actor path, with interpolation and actor clips
for crouch, airborne, running, AR/pistol, firing, melee and grenade.
Network mode removes the local stationary practice target so it cannot be
mistaken for the other player or take local-only combat damage.
The Android log prints ping, packet/byte rates, snapshot rate and invalid or
dropped packet totals every two seconds; the touch HUD shows ping and IDs.

**Authority limit:** the server owns IDs, peer presence and relay, but the
client currently simulates movement and submits a bounded transform. This is
an explicitly provisional visual slice; the server does not yet own collision,
weapon inventory, health, damage, projectiles or vehicles. Action events are
cosmetic. Neither desktop nor Android can damage the other player. The desktop
client is a narrow multiplayer probe and does not yet carry the Android
gameplay loop's first-person weapon, pickups, audio or vehicles.

## What was demonstrated

- `scripts/verify.sh` with the owner's Blood Gulch map: **60 passed, 0 failed**.
  This includes host build/tests, map-backed engine checks, offscreen gameplay
  renders, a three-process multiplayer Blood Gulch regression, and the arm64
  Android APK build, rerun after Host LAN was added.
  The APK was **not device tested**.
- `scripts/run_two_clients.sh 32271 4`: two distinct client processes joined
  one headless server, IDs 1 and 2; each received the other's state and fire
  events. No invalid packets. About 20 snapshots/s/client, loopback ping about
  5 ms. At 3 seconds, a client received 5,623 bytes and sent 3,710 bytes:
  roughly 1.9 KiB/s in and 1.2 KiB/s out, including headers.
- A separate 6-second SDL dummy-video run launched one headless server and two
  `htaplay` processes against the owner's real Blood Gulch map. Both exited 0,
  joined as different players, and logged `remote=2` / `remote=1`. One
  offscreen captured frame visibly shows a Spartan on the map. Client logs at
  4 seconds reported 7,326 bytes received / ~4,500 bytes sent and 7 ms ping.
  This is **local loopback** evidence, not a LAN latency measurement or a
  two-human playtest. A later 6-second rerun after the AR/pistol actor change
  also exited 0 in both clients; both logged the other player's ID at 2, 4
  and 6 seconds, and a frame in `scratch/net-session/b.png` shows the moving
  remote Spartan. Those scratch images and logs are local only and are
  intentionally excluded from Git because they render proprietary assets.
- A further automated 6-second run checked replicated action and motion flags
  end to end: both clients observed the other airborne, crouched and holding
  the pistol; each received five remote events and successfully started four
  matching actor clips (`scratch/net-session/a2.log`, `b2.log`). Both exited 0.
  The shared interpolation helper has host checks for midpoint, clamping and
  shortest-path yaw across the ±π wrap.
- `scripts/test_two_players.sh` now makes that asset-backed check repeatable
  and part of `scripts/verify.sh` when the map and desktop graphics dependencies
  are available. The latest full gate passed 60/60 and checks that the APK
  actually declares Android's `INTERNET` permission.

## Known limits and risks

- Android ↔ desktop and Android ↔ Android have not been run: no ADB device is
  attached. The APK compiles and has `INTERNET`, but runtime connection and
  remote rendering on the phone remain unverified.
- Two humans have not operated two desktop windows. The automated clients use
  scripted movement; the interactive window path needs a human playtest.
- LAN ping and loss behavior are unmeasured. The loopback number includes the
  clients' 5 ms pump interval. The current protocol uses no event ACK/retry,
  sequence wrap logic, clock sync or client reconciliation.
- Only the first remote player is rendered per client even though the server
  registry holds eight. Snapshot state reaches all clients; extra actors wait.
- The desktop probe sends action events without playing the full local weapon
  behavior. Remote fire/melee/grenade actor clips are cosmetic and lack remote
  muzzle flash, projectile and sound. Weapon selection is limited to the map's
  AR/pistol starting slots for visible models.
- A client can submit a finite but implausible transform; server movement
  authority and plausibility validation are the next major architecture step.
- The client clears stale remote state and retries HELLO after ten seconds of
  server silence; the server expires peers after ten seconds. There is no
  instant disconnect notification for an unexpectedly lost host.

## Reproduction

```
HTA_MAP=/path/to/your/bloodgulch.map scripts/verify.sh
scripts/run_two_clients.sh 32270 8
HTA_MAP=/path/to/your/bloodgulch.map scripts/test_two_players.sh
HTA_MAP=/path/to/your/bloodgulch.map scripts/run_two_players.sh 32270
```

Desktop controls: WASD move, mouse look, Space jump, Left Ctrl crouch, left
click fire event, right click melee event, G grenade event, 1/2 weapon slots,
F1 release/capture mouse, Escape quit. The Android app's setup screen offers Play (unchanged solo path),
Host LAN game (other players join this device's LAN IPv4), and Join LAN server
(numeric desktop or Android host IPv4, UDP port 32270). Start
`./build-host/htanet server 32270 86400` on the desktop first. Both peers must
own their Trial map files; no assets are transmitted or committed.

## Next testing objective

Run `scripts/run_two_players.sh` with two human-controlled desktop windows and
observe both directions while walking, turning, jumping, crouching, switching
between AR/pistol, and pressing fire/melee/grenade. Check for correct third-
person clips and no sudden origin flash at join. Then run the desktop server
and join from the Android APK on the same LAN. Capture device logs and screen
before changing authority or adding damage. A second Android device is optional
after Android ↔ desktop works. **Do not claim phase success until two players
have visibly run together in Blood Gulch.**
