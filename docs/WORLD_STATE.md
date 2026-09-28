# World State vNext (X8)

X8 removes the 64-object production limit. A world can hold up to 1,024
runtime objects, while its network state has separate capacities for 256
movers and 1,024 relay flags. Night Shift's original 62-object package is
unchanged. A second package restores its two cut doors: 74 runtime objects,
23 movers, 11 relays and 40 host-only objects. Its at-rest `WORLD_STATE`
payload is 39 bytes.

## Identity and authority

| Identity | How it is assigned | Used for |
|---|---|---|
| Package ID and world key | Authored bytes and dependency digests | Peer compatibility. The key does not include a network index or protocol version. |
| Runtime object index | Every placed entity, then every prefab child in canonical instance order | Host simulation, links, X7 bindings, Lua handles, collision, diagnostics and world-sound effects. A 16-bit index names it. |
| Spatial index | Movers only, in runtime order | Phase and progress in `WORLD_STATE`. |
| Logical index | Relays only, in runtime order | Active flag in `WORLD_STATE`. |

The mapping is a pure function of the compiled world definitions. Every
admitted peer loads the same package set and derives the same mapping; no
index is put in a package or a key. Interactables, triggers, teleports and
props have no spatial or logical network slot. Their placement is already
in the package. The host keeps their cooldown, occupancy and other runtime
details. A client's use press is a control request; the host does its own
reach test and chooses the target. X7 bindings and Lua execute on the host.

Relay activity is now a logical replicated bit. Clients hold that bit for
state display and late join; they never dispatch relay events. A complete
snapshot gives a late joiner the current mover phases and relay flags,
without replaying the events or scripts that produced them. The first
value of each mover snaps; later progress updates ease toward the host.

`src/asset/world_repl.{h,c}` defines the classification table and mapping.
`megamod-resources --json` exports that same table, limits and wire costs.
Open Asset Lab's `assetlab/world_state.py` reads its generated copy and
checks expanded worlds before writing a package.

## Protocol v11

The v10 `WORLD_STATE` named a runtime entity in one byte and only carried
movers. V11 carries two indexed channels, with format byte `1`. The message
header is 5 bytes: format `u8`, spatial count `u16`, logical count `u16`.
Spatial records are ascending maximal runs: kind `1`, first spatial index
`u16`, run count `u8` (1..255), then each mover's phase `u8` (0 closed,
1 opening, 2 open, 3 closing). A moving mover adds progress `u16`, from
0 to 65535. A resting mover needs one byte and has exact progress 0 or
65535. Logical records are ascending maximal runs: kind `2`, first flag
`u16`, count `u16`, then `ceil(count/8)` little-endian bitset bytes. All
integers are little-endian. The decoder rejects duplicate, out-of-order,
nonmaximal, unknown, truncated and over-limit records without applying
any part of them. Protocol v10 and v11 refuse each other's sessions;
they do not reinterpret this message.

The host sends a complete state on a discrete change and repeats it twice
more, sends moving-only changes at most every four 20 Hz ticks, sends an
idle keyframe each 20 ticks, and sends immediately when a peer joins.
Even a world with only host-only objects sends the five-byte empty state, so
a late joiner can mark it synced.
Each complete message fits one 1,200-byte UDP packet (20-byte envelope).
For `S` movers, `M` moving and `F` relays, payload bytes are:

`5 + 4*ceil(S/255) + S + 2*M + (F ? 5 + ceil(F/8) : 0)`.

The spatial term is zero when `S=0`. At the declared wire limits of 256
movers and 1,024 flags the packet's maximum payload is 914 bytes. The
1,024-object runtime limit means that a real world with 256 movers can
hold at most 768 relays; that all-moving state is 882 bytes. Host-only
objects add no payload bytes. A world-sound effect names its source by a
16-bit runtime index in v11. Unit effects keep their existing semantics.

## Limits and cost

| Bound | X8 value | Source |
|---|---:|---|
| Runtime objects | 1,024 | `HTA_WDEF_MAX_ENTITIES` |
| Spatial mover states | 256 | `HTA_WREP_MAX_SPATIAL` |
| Logical relay flags | 1,024 | `HTA_WREP_MAX_FLAGS` |
| Prefab instances | 128 | `HTA_WDEF_MAX_INSTANCES` |
| Mover definitions | 256 | `HTA_WDEF_MAX_MOVER_DEFS` |
| GPU instances | 512 | `HTA_GFX_MAX_INSTANCES`; a separate rendering bound, reported per world |

All are load-time limits with explicit errors. The runtime definitions
and state arrays are fixed size, about 391 KB and 1.09 MB respectively.
On the development desktop the synthetic maximum world (256 movers,
768 relays) parsed and checked in 7.56 ms and loaded in 2.21 ms. For an
882-byte message, export was 4.9 µs, encoding 4.6 µs, decoding 5.0 µs,
client application 13.2 µs, and host world step 6.2 µs. These are
measurements from `test_world_state` on this machine, not a device budget.

The 74-object Night Shift X8 world needs 39 payload bytes at rest (59 with
packet envelope) or 85 payload bytes with every mover moving. In a
13-second desktop host and joiner run, four idle states carried 156
payload bytes. In Claude's earlier X7 smoke, 21 objects needed 17 payload
bytes at rest. The host prints actual `WORLD_STATE` count, payload bytes,
largest message and total outbound bytes; the joiner prints received
counts. `megamod-resources --bundle ... --world ...` prints each object's
runtime index and replication channel and index.

In a 230-second desktop playthrough with four sequential joiners powering
security, opening D2 and starting coolant, the host sent 261 state
messages: 10,807 payload bytes (47 bytes/s), largest 49 bytes. All
outbound traffic including player and game messages was 497,643 bytes
(2.16 KB/s). These are observed totals for that route, not a fixed rate.

## Checks

- `test_world_state`: deterministic mapping; a button, relay, door, Lua
  target, trigger and teleport above runtime index 63; host authority;
  late join by state; mismatch refusal; 1,024-object limit and costs.
- `test_net` and `test_net_fuzz`: exact v11 codec, malformed runs,
  randomized mutation and canonical re-encoding, send policy and v10/v11
  refusal. Run the fuzz test under ASan and UBSan too.
- `scripts/test_x8.sh`: Open Asset Lab builds the separate Night Shift X8
  package; OAL and MegaMod keys, counts and costs agree; a desktop joiner
  applies the 39-byte idle state. Players then power security, open restored
  D2, and use the prefab valve at runtime index 69. A late joiner receives
  active relays, open D2 and the running pump from current state.
- `scripts/test_night_shift.sh` and X1–X7 tests: historical content and
  package keys remain stable; `verify.sh` runs them with Trial data.
- Android uses `game/net_world_state.c`, the same client application path
  used by desktop joiners and the C test. The Android APK build and emulator
  check exercise that integration. On 2026-09-27 an Android 14 x86_64
  emulator hosted the 74-object Night Shift X8 package with 71 bindings;
  a desktop peer joined through the UDP relay and applied one 39-byte
  complete state (23 movers, 11 relays, zero refusals). The emulator used
  software graphics and ran at roughly 3–7 fps; this is a functional
  network and load check, not a mobile GPU benchmark.
- A local v10 binary was tested in both directions: the v11 joiner explicitly
  refused the v10 host, and the v11 host recorded a protocol-version refusal
  when a v10 joiner attempted to connect. No cross-version play occurred.

No AI, new rules, lighting or inventory changes are part of X8.
