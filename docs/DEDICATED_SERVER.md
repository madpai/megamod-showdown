# Dedicated server

A headless host that runs matches with no player of its own: on the dev
desktop, a VPS or a Raspberry Pi; reachable on the LAN, over Tailscale, or
(later, with a password) from the internet. Phones and the PC joiner
connect to it exactly as they connect to a phone host today.

## Where it stands (2026-09-25)

| Piece | State |
|---|---|
| `megamod-server` binary, config file, `--check` | done |
| Bind to all / LAN / Tailscale / one IP, with the reason when it cannot | done, tested |
| LAN discovery answers with the server's name, map and player cap | done |
| JSON status file (players and their addresses, packets, uptime) | done |
| `sim = scripted`: a stand-in match to test clients and reachability | done, a real joiner played it over UDP |
| `sim = match`: the real game, headless | **S1 written (host_net.c), awaiting a phone check; S2-S3 to do** |
| Join password (protocol v10) | designed below; parsed, not enforced |
| Rate limiting per address | done: 600 packets/s, burst 1200, per IPv4 source; `rate_limited` in the status file |

## Running it

```sh
cmake --build build-host --target megamod-server
cp scripts/server.cfg.example ~/megamod-server.cfg        # edit it
build-host/megamod-server --config ~/megamod-server.cfg --check
build-host/megamod-server --config ~/megamod-server.cfg
```

As a service: `scripts/megamod-server.service.example` (systemd user unit;
fix its `ExecStart` path to where the repo lives on that machine).

### Who can reach it

| `bind =` | Listens on | Reachable by |
|---|---|---|
| `tailscale` | this machine's Tailscale address (100.64.0.0/10) | devices on your tailnet, anywhere -- **the default to use** |
| `lan` | this machine's private LAN address (Docker/VM bridges skipped) | the local network |
| `all` | every interface | the LAN, the tailnet, and the internet if a router forwards the port |
| `192.168.1.23` | exactly that address | whoever can reach that address |

`--check` prints the address it resolved and refuses (with the interface
list) when the machine has no such address -- it never falls back to
listening everywhere. Do not port-forward to it until v10's password is in (below); rate
limiting is.

### The config

`scripts/server.cfg.example` lists every key with its default. Unknown keys
are warnings; bad values are errors with their line number. The keys:
`name` (23 characters, what LAN browsers show), `motd`, `bind`, `port`,
`max_players` (2-8), `password` (v10), `maps` (rotation: `bloodgulch` is
the Trial's, anything else a `<name>.oalmap` in `map_dir`), `map_dir`,
`trial_dir` (the owner's Trial maps), `mode` (slayer, team, ctf),
`score_limit`, `time_limit`, `respawn`, `bots`, `bot_skill`,
`spawn_protect`, `classes`, `duplicate_heroes`, `tick_rate` (20-120),
`status_file`, `sim` (match, scripted).

**Known limit of `sim = scripted`:** it has no map, so it puts every
joiner at (-4, -6, 0) and trusts the joiner's reported position after
that. On Blood Gulch that point is probably not on the canyon floor: the
first phone test (report of 13dfb40) showed the player airborne at exactly
(-4.00, -6.00, -111.85), falling. It proves reachability and the protocol,
not play; S3 replaces it with the real match and the map's spawn points.

## Getting the real match in: stages S1-S3

The host simulation lives in `src/platform/platform_android.c`. Loop
extraction stage 1 already put all of its state in `hta_session`
(`src/app/session.h`), so each piece below can move onto `hta_session *`
without touching Android-only state. Each stage keeps the phone build
working and gets one phone check (a hosted LAN match) before the next --
the same rule as docs/ENGINE_ARCHITECTURE.md.

**S1 -- host networking out.** *Status 2026-09-25: the per-tick half is
done -- `hta_host_peers`, `hta_host_mirror_local`, `hta_host_world` in
`src/app/host_net.c`, moved verbatim, with the GPU upload as a callback;
`tests/test_host_net.c` joins over localhost, drives the unit, reads WORLD
with no local player. Still in platform_android.c: `net_begin`'s host half
and the scattered fx/kill sends (they sit inside game-event handling and
move with S3). Note for S3: `hta_host_world` returns unless
`s->net.connected` -- the phone host's own loopback client; a server has
none, so that guard must become "hosting".*
Originally: Move `net_begin` (host half), `net_host_peers`,
`net_host_world` (WORLD, projectiles, vehicles, drops, GAME with props) and
the scattered `hta_net_server_fx` / `_kill` sends into `src/app/host_net.c`
taking `hta_session *`. What they do that is presentation -- the GPU upload
when a peer joins (`game_gpu_upload`), sounds -- becomes a small callback
table (`hta_session_hooks`: `on_unit_added`, `play_at`, ...) that Android
fills and the server leaves empty. *Check:* a hosted LAN match on two phones.

**S2 -- loading without a GPU.** Split `load_map` / `start_game` into a data
half (Trial cache, collision, nav, items, vehicles, imported world, the
game with its bots) and a presentation half (GPU meshes, textures, sound
banks, HUD). The data half goes to `src/app/match.c` and takes paths, not
APK assets (the server reads `trial_dir` / `map_dir`; Android keeps mapping
from the APK through the platform's file interface -- stage 4 of the loop
plan, done here for the server's needs). *Check:* solo and hosted matches
on Blood Gulch and one imported map.

**S3 -- the tick.** The per-frame simulation in `android_main` (game
update, vehicles, projectiles, bots, props via a headless `hta_world_fx`
with no debris or sprites) becomes `hta_session_tick(session, dt)` in
`src/app/session.c`. The server calls it at `tick_rate`; Android calls it
from its frame. A server has no local player: `me = -1` must be a valid
session everywhere (today the host is always a player -- audit every
`s->me` use; this is the riskiest part). *Check:* a hosted phone match
plays exactly as before; then `sim = match` on the desktop with two phones.

Then `megamod-server` swaps its scripted match for `hta_session_tick` and
rotates `maps` when a match ends.

## Before the internet: protocol v10

**Password.** v10 adds a 16-byte challenge-response to the join: the server's WELCOME-CHALLENGE sends a random
nonce; the client answers `HMAC-SHA256(password, nonce || client nonce)`
truncated to 16 bytes; a wrong answer gets `HTA_NET_REJECT_PASSWORD` (a new
reason the phone shows as "WRONG PASSWORD"). The password never crosses
the wire, and a replayed answer fails on a new nonce. The Android join
screen gains a password field (Java) and INFO a `password required` flag so
LAN browsers show a lock. Bump `HTA_NET_VERSION` to 10; v9 and v10 refuse
each other, as always.

**Rate limiting (done 2026-09-25).** `hta_net_server_pump` keeps a token
bucket per source IPv4 address (32 sources, the longest-silent evicted),
600 packets/s with a burst of 1200, checked before a packet is decoded;
over-limit packets are counted in `stats.limited` (`rate_limited` in the
status file and in SEND REPORT) and dropped. The pump now drains up to 1024
packets a call so a flood cannot sit in front of players' packets. Why per
address and those numbers: a phone or the PC joiner sends ~20 CONTROL+STATE
pairs a second, ~50 packets/s with pings and events; players behind one
router share an address, so the budget fits all 8 of them with headroom,
and one sender cannot escape it by spraying ports. What it does not stop:
spoofed source addresses (each gets a fresh bucket) and a flood big enough
to fill the link itself -- that is what keeping `bind` off the open
internet, or a password plus a provider's DDoS filtering, is for. The
separate cap on unauthenticated HELLOs in the first design was dropped: a
rejected client retries HELLO 4 times a second, which a cap tight enough
to matter would have misread as an attack; the bucket covers it.
`tests/test_net_rate.c` floods, refills, runs a real client through a
minute unlimited, and turns it off.

**Also before a port-forward:** a public server's status file should not be
served publicly (it lists player addresses); DISCOVER answers only on
the LAN interface when `bind = all` (so the internet cannot enumerate it).

## Cross-platform

The server needs only UDP, a clock and files: no Vulkan, no SDL, no audio.
Linux builds today. macOS needs nothing new (`getifaddrs` exists). Windows
needs `udp.c` on Winsock (`WSAStartup`, `closesocket`, `ioctlsocket` for
non-blocking) and `GetAdaptersAddresses` in place of `getifaddrs` -- about
a day, best done once S3 lands so there is a real server to port.
