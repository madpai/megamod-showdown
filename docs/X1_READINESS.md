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
- **Network:** if the placement table is fully specified by the manifest,
  the current map key checks its bytes and order (`world_ext.key` hashes the
  manifest; [CONTENT_COMPATIBILITY.md](CONTENT_COMPATIBILITY.md)). A dense
  slot can then be sent without a placed ID string. The key does **not**
  cover binary geometry/collision, so X1 must separately decide how a
  moving collider derived from that payload is made compatible.
- The manifest scanner in `src/asset/external_map.c` is key-by-key and
  order-sensitive; a placements section needs a real bounded parser (count,
  string length, link fan-out limits), fuzzed like the packet decoders.

## How should the first original test world be authored?

**Reconciled recommendation: B for the integration fixture -- construct an
ORIGINAL normalized OAL world programmatically, then compile it through the
normal validator and package writer.** The test builder supplies simple
floor/wall/door geometry, collision, spawn points, placements and links. It
does not write OALMAP bytes directly or establish a public JSON/TOML world
format. See the [option comparison](research/X1_ORIGINAL_AUTHORING_PATH.md).

The other test paths have narrower roles:

- **A C runtime fixture** is good for fast isolated MegaMod unit tests and
  should exist too; it skips OAL validation, so it cannot replace the
  compiled integration fixture.
- **C (glTF + metadata)** brings a mesh importer and a scene-graph mapping
  into scope before anything needs them.
- **A synthetic BSP** (OAL's tests already generate one) would push the test
  world through Source entity classes (`func_door`, `logic_relay`), which
  the vision forbids as the generic contract.

The programmatic builder needs only enough original geometry for the test;
its reusable seam is the normal OAL validator/compiler, not a second input
format or editor.

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
- **Protocol:** X1's packet fields need an explicit version bump. v11 remains
  reserved for the planned password work; if that reservation holds, X1 uses
  v12 or a later assigned version. No number is assigned by this note.
- **Guard:** manifest placement IDs/links are covered by the current map
  key; gameplay collider geometry in the binary payload is not. Add a
  semantic package check if X1's behavior depends on that payload.

## Suggested order

1. OAL: programmatic original normalized-world fixture (B) through the
   ordinary validator/compiler + placements section + validation
   (unique IDs, known kinds, target existence, fan-out, cycle warning).
2. MegaMod: bounded placements parser; dense table with generations; door
   collider through the existing instance path; host-side event queue with
   a per-tick budget; C unit tests on a B-style fixture.
3. Network: CONTROL interact and per-placement state array; assign a protocol
   version without taking the v11 password reservation.
4. `megamod-join` interact key; emulator + desktop two-client and late-join
   runs on the OAL-compiled original world.
