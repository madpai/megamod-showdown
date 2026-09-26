# X1 (world events) readiness, from the N1/N3/N2 implementation

**Status:** 2026-09-26, planning note. X1 is **not started**. This answers
the questions left open by
[WORLD_EVENT_SLICE_RECOMMENDATION](research/WORLD_EVENT_SLICE_RECOMMENDATION.md)
using what the content-compatibility and ID-audit work showed.

## Can stable placed IDs be represented cleanly?

Yes, without a new binary format:

- **Authored:** a new manifest section in the OALMAP (JSON, like
  `breakables` and `flag_points` already are), e.g. `"placements":
  [{"id": "door_a", "kind": "door", ...}, ...]`. Placed IDs use the name
  segment of the ID grammar (`[a-z][a-z0-9_]*`, <= 48;
  [CONTENT_IDS.md](https://github.com/madpai/open-asset-lab/blob/main/docs/CONTENT_IDS.md)),
  unique within the world; links name targets by placed ID.
- **Runtime:** resolved once at world load into a dense table (slot +
  generation), as props are today.
- **Network:** the dense slot is safe to send, because the whole manifest is
  already in the map check (`world_ext.key` hashes every manifest byte;
  [CONTENT_COMPATIBILITY.md](CONTENT_COMPATIBILITY.md)). Two peers with the
  same package build the same table in the same order. No placed ID string
  needs to cross the wire.
- The manifest scanner in `src/asset/external_map.c` is key-by-key and
  order-sensitive; a placements section needs a real bounded parser (count,
  string length, link fan-out limits), fuzzed like the packet decoders.

## How should the first original test world be authored?

**Recommended: A -- a tiny OAL world-description file compiled by the
normal pipeline.** A JSON (or TOML) file listing axis-aligned boxes
(floor, walls, a door slab, a button, trigger volumes) with a material each,
spawn points, and the X1 placements and links. A small OAL module turns the
boxes into triangles and writes an ordinary OALMAP through the existing
writer (`compile_map`'s packaging half), including the placements section.

Why this and not the others:

- **B (a synthetic OALMAP generated in C test code)** is good for MegaMod's
  unit tests of the runtime half, and should exist too, but it skips OAL's
  validation (missing targets, duplicate IDs, cycles) -- half of what X1 is
  meant to prove.
- **C (glTF + metadata)** brings a mesh importer and a scene-graph mapping
  into scope before anything needs them.
- **A synthetic BSP** (OAL's tests already generate one) would push the test
  world through Source entity classes (`func_door`, `logic_relay`), which
  the vision forbids as the generic contract.

A needs about 150-250 lines in OAL (boxes -> triangles, UV per box face,
one solid-colour texture per material) and no editor. The same file format
later serves any hand-made test arena.

## Does Step 5 (the desktop game) improve X1 testing enough to go first?

**Not enough to go first.** This phase showed that two-client tests already
work without it: the emulator hosts the real APK, `megamod-join` joins
through `scripts/emu/udprelay.py`, and both sides log what they did
(`[net] refused a joiner`, `player N joined game unit M`). Late join is the
same path. What X1 needs that `megamod-join` lacks is an **interact**
action (press E at a button) -- a small addition to its input mapping and
the CONTROL counter below, not the whole desktop game. `megamod-match`
(headless, bots only) can drive the world half of X1 in CI.

Step 5 remains the way to a real desktop player; doing it after X1 means
the desktop game is built on the finished world-event model instead of
being reworked for it.

## What network support will X1 need?

- **Client -> host:** an interact intent in CONTROL: a counter (like
  melee/pickup today) plus the target's dense placement slot; the host
  checks reach and state.
- **Host -> client:** durable state of mutable placements (door phase or
  open fraction, trigger enabled) as a compact per-slot array alongside the
  GAME packet's prop mask, sent every snapshot so a late joiner converges
  without replaying events. Teleports need nothing new: the actor's
  position is already replicated.
- **Protocol:** one version bump for the new fields (v11), coordinated with
  the password work (then v12, or folded into the same bump).
- **Guard:** none new -- placements come from the OALMAP, already in the
  map check.

## Suggested order

1. OAL: world-description compiler (A) + placements section + validation
   (unique IDs, known kinds, target existence, fan-out, cycle warning).
2. MegaMod: bounded placements parser; dense table with generations; door
   collider through the existing instance path; host-side event queue with
   a per-tick budget; C unit tests on a B-style fixture.
3. Network: CONTROL interact, per-placement state array; v11.
4. `megamod-join` interact key; emulator + desktop two-client and late-join
   runs on the A-authored world.
