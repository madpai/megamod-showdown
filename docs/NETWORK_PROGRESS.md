# Network phase checkpoint — 2026-09-22

## Current Android match status (2026-09-22 evening)

Android hosting now drives an on-foot Slayer match from the host's portable
`hta_game` simulation. Remote phones send bounded movement, look, fire and
one-shot action controls. Host movement and collision determine remote bodies;
host damage, ammo, pickups, bots, scores, kills and respawns are replicated at
20 Hz to every joiner. Projectile snapshots and fire/impact/detonation effects
are sent separately; kill feed is acknowledged and retried. The host rejects
full sessions and mismatched maps. Android renders all match units rather than
one provisional remote actor. `tests/test_game.c` exercises remote fire and
damage against a real Trial map; `tests/test_net.c` exercises the two-client
protocol, world and projectile delivery, kill acknowledgment, map mismatch and
full-session rejection. The full verification gate passed 68/68, including the
asset-free Android APK. No two-device Android combat run has happened yet.
Protocol version 2 is incompatible with the older visual-only version 1
build; both phones should install the current QA build. The current APKs and
test checklist are in `HANDOFF.md`.

Vehicles remain solo only. A headless desktop `htanet` server and the SDL
`htaplay` clients still exercise the earlier visual transport path; they are
not an authoritative match. Internet direct IP requires UDP 32270 reachability
and has no directory or NAT traversal. The material below documents the
earlier checkpoint and should not be read as the current Android feature set.
The next engineering objective is online vehicles, including visible seated
players and all Blood Gulch vehicle types. The staged plan is in
`HANDOFF.md`; no vehicle implementation has begun in this update.

Important follow-up risks: host-local traveling rounds use wire pool IDs 4/5
and a client's currently equipped first-person projectile mesh for display.
That mesh may be absent or show a different weapon's round; damage and
detonation are still decided by the host. The snapshot caps visible traveling
rounds at 32, so host-local rounds can be omitted when portable game pools
fill all 32 slots. Remote movement uses host correction without input
reconciliation, and real-device latency/loss has not been measured.

> **Archive below:** This is the original visual-only LAN checkpoint. It is
> retained for the transport experiments and measurements, not as current
> Android feature or APK information.

## Archived starting point (2026-09-21)

- Published APK code commit: `97d7cea` on `fp-animated-guns`. Pre-network
  known-good tag: `net-baseline-2026-09-21`. This handoff documentation is a
  later commit; check `git status` before starting work.
- The Android LAN test APK is published at the private Tailscale sideload page
  `http://100.89.1.14:8731/` as `scratch/serve/halo-trial-poc.apk` (build
  `97d7cea`, SHA-256
  `485dd4a68ba691b7efca95fe54f51777b40d0e542c7d4afd2f4e27d1b95e5f4d`).
  A GET download matched the built APK. The APK contains no Trial assets.
- The next decisive test is **Android host + Android joiner on the same Wi-Fi**
  if a friend/device is available, or Android client + desktop server first.
  No Android runtime session has yet been observed; `adb devices` was empty.
- Server registry and snapshots accept up to eight peers, but **Android and
  desktop currently render only one remote Spartan each**. Two visible players
  are the tested scope. A third device can connect, but a complete three-player
  game needs per-peer remote actor and interpolation slots.
- **Vehicles intentionally cannot be entered in LAN mode** because their state
  is not replicated. Solo vehicle driving remains available. Player-vs-player
  health, shields, damage, death and respawn are also not networked.

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
Vehicle entry is disabled in network mode until vehicle state can be owned by
the server; the SWAP control remains weapon selection near parked Warthogs.
The Android log prints ping, packet/byte rates, snapshot rate and invalid or
dropped packet totals every two seconds; the touch HUD shows ping and IDs.
The setup screen exposes the host phone's Wi-Fi IPv4 address and UDP port
32270, so another phone can enter it in Join LAN server. Each device must
pick its own map and external resource files. This host-and-local-join path
is still awaiting a device runtime test.

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
- On 2026-09-22 the owner unlocked the desktop and observed two live Blood
  Gulch windows side by side. The owner moved one client player in front of
  the other and reported that the players saw each other. A captured desktop
  frame (`scratch/net-live/desktop-x11.png`, local only) independently shows a
  Spartan in each window. The live logs show distinct IDs (`id=1 remote=2`,
  `id=2 remote=1`) and the second client saw the first player's position change
  from about `(98.21, -156.49)` to `(96.82, -156.99)`; both remained connected
  without invalid packets. A subsequent desktop input probe delivered pistol
  selection and two action events to the other client. This establishes a
  human-controlled desktop movement and reciprocal rendering check, but only
  one human operated the session. It does not establish two people playing
  simultaneously, Android runtime behavior, or LAN latency. The observed
  loopback ping was 16-17 ms with the graphics clients running.

## Known limits and risks

- Android ↔ desktop and Android ↔ Android have not been run: no ADB device is
  attached. The APK compiles and has `INTERNET`, but runtime connection and
  remote rendering on the phone remain unverified.
- One human moved a player in a live two-window desktop session and observed
  reciprocal player rendering. Two humans have not yet controlled both windows
  simultaneously or checked every action visually in a live session.
- LAN ping and loss behavior are unmeasured. The loopback number includes the
  clients' 5 ms pump interval. The current protocol uses no event ACK/retry,
  sequence wrap logic, clock sync or client reconciliation.
- Only the first remote player is rendered per client even though the server
  registry holds eight. Snapshot state reaches all clients; extra actors wait.
  A third player may join but each client sees only one other player.
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

### Android LAN playtest

1. Install the published ARM64 APK on each phone. On each device, use the
   setup picker for its own `bloodgulch.map` and `bitmaps.map`; `sounds.map`
   is optional for audio. Connect devices to the same Wi-Fi.
2. On the first phone, note the **Hosting address** shown on the setup screen,
   then tap **Host LAN game**. This opens UDP port 32270 and joins its own
   in-process server over loopback. Keep the game open while others join.
3. On the second phone, enter the first phone's numeric Wi-Fi IPv4 address
   and tap **Join LAN server**. A desktop host is also possible via
   `./build-host/htanet server 32270 86400` on the same LAN.
4. Check that both devices show IDs and ping in the debug HUD and each sees
   the other's Spartan move, turn, jump, crouch, switch AR/pistol, fire, melee
   and throw a grenade. Report the exact step that fails and screenshots from
   both devices. If ADB is available, capture
   `adb logcat -s halo-trial-android:I` from each device; the native log
   prints connection and packet statistics every two seconds.

The phone's displayed address comes from its Wi-Fi interface. If no address
appears, connect to Wi-Fi and reopen setup. Router client isolation or a
firewall blocking UDP 32270 can prevent a LAN join even when both phones show
the same SSID. Do not use ADB forwarding as the runtime network path.

## Next testing objective

Run the published Android APK with a second device on the same LAN (or use a
desktop server if a second phone is unavailable). Capture both screens and
connection logs, and check movement plus each listed action before changing
authority or adding damage. If Android connects and renders correctly, use two
people to check simultaneous control. Then decide whether to add per-peer
actors for three players or begin server-side movement authority; do not expand
combat or vehicles as part of the visual slice. **Do not claim Android or
two-human success until it is observed.**
