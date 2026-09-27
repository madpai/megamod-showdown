# Event bindings (X7): declarative events, conditions and actions

**Status:** implemented 2026-09-27. This describes the code as it is.
Code: `src/asset/world_def.{h,c}` (vocabulary tables, the binding parser,
`hta_wbind_check_one`, world resolution, prefab expansion),
`src/asset/prefab.c` (prefab schema 2 bindings), `src/asset/package.c`
(a prefab binding's sound, from the provider's view),
`src/engine/world_entities.{h,c}` (compiled dispatch, conditions, the
action seam, cascades, relay state, mover arrival events, damage and sound
outputs, the trace), `src/app/session_tick.c` (damage through the game,
the world-sound effect to joiners), `src/net/protocol.{h,c}`
(`HTA_NET_FX_WORLD_SOUND`), `src/asset/resource_contract.c` and
`src/tools/resources.c` (introspection). Open Asset Lab:
`assetlab/bindings.py`, `prefabs.py`, `world.py`, `dependencies.py`,
`fixtures.py` (`x7.facility`, `x7_facility_world`, `x7_second_world`).

> Simple behaviour should be data; unusual behaviour should be code.

X7 unifies gameplay the engine already had -- X1 links, movers, relays,
triggers, teleports, X3's damage path, X5's sounds -- behind one small
declarative vocabulary. It is not a second gameplay engine and not an ECS:
there is no component bag, no expression language, no timer, no new entity
kind, no Lua API change and no new network message.

Evidence labels: **TESTED** (a test asserts it), **OBSERVED** (seen in a
run), **INVENTED** (our number).

## The model

```
EventBinding
  id          a local ID (the X6 grammar), unique in its world or prefab
  source      an entity (a placed ID in a world; a child's local ID in a prefab)
  event       one the source's kind emits
  conditions  0..4 read-only engine predicates; ALL must hold
  actions     1..8 requests of existing engine capabilities, in order
```

```json
{"actions": [{"action": "toggle", "target": "door"}],
 "conditions": [{"condition": "relay_state", "entity": "power", "is": "active"}],
 "event": "used", "id": "toggle_door", "source": "button"}
```

The tables below are printed by `megamod-resources --markdown` from the
same tables the parser, the checks and the runtime use (they are in
[RESOURCES.md](RESOURCES.md#introspection-megamod-resources), kept current
by `test_resource`); `--json` has them under `bindings`.

### Events (what happened)

| Event | Sources | Actor | X1 link name | The engine transition |
|---|---|---|---|---|
| `used` | interactable | always | used | a player's press passed the reach test (`hta_went_interact`), or a `use` action |
| `activated` | relay | the chain's | fired | it was told `activate` (a link, a binding, Lua) and the queue dispatched it |
| `entered` | trigger | always | entered | a body went from outside to inside (`hta_went_sense`) |
| `deactivated` | relay | the chain's | - | it was told `deactivate` |
| `opened` | mover | - | - | its phase became open (it arrived, or was told to open at the end of its travel) |
| `closed` | mover | - | - | its phase became closed |

`fired` and `activated` are ONE engine event (`HTA_WEV_FIRED`): the X1 link
name stays for links, a binding uses the X7 name. `left`, `damaged`,
`killed` and `round_started` were not added: no proof needed them yet
(add one when a world does). Only a real transition emits: `open` on an open
door does nothing and emits nothing (**TESTED**).

### Conditions (should this binding run)

| Condition | Reads | Values |
|---|---|---|
| `mover_state` | a mover | `closed`, `opening`, `open`, `closing` (Lua's `world.state` vocabulary) |
| `relay_state` | a relay | `inactive`, `active` |

A relay now remembers what it was last told (`active` after `activate`,
`inactive` after `deactivate`; every relay starts a round inactive). This
is host state only: nothing but conditions reads it, so it is not
replicated. `activate` still fires every time, as in X1.

### Actions (request an engine capability)

| Action | Target affords it | Needs | Takes | Engine path |
|---|---|---|---|---|
| `open`, `close`, `toggle` | mover | target | target | the world-event queue, as a link's input |
| `activate`, `deactivate` | relay | target | target | the queue; `deactivate` is X7's one new input |
| `teleport` | teleport | target | target | the queue, as a link's: the event's actor, once per step |
| `use` | interactable | target | target | `hta_went_use`: what a player's press does after its reach test |
| `damage` | - | amount | amount | `hta_game_hurt`, the native pipeline (as Lua's `game.damage`), on the event's actor, credited to no player |
| `play_sound` | - | sound | sound, at | an X5 world sound cue (as a mover's), at `at` or the source; to joiners as a world-sound effect |

Arguments are typed at load, never a string map at run time
(`hta_waction`: op, queue input, target index, sound index, `at` index,
amount). A target must be a kind that **affords** the action; `at` must be
a kind with a position (not a relay); `damage` and `teleport` act on the
event's actor, so they are refused on `opened`/`closed`, which never carry
one; `sound` is an X5 `sound` resource resolved through the typed resolver
(imported by the world, or, in a prefab, by its own library). Everything
is checked when the world loads; the words are the engine's and Open Asset
Lab's alike, e.g.

- `binding shock: event 'opened' is not supported by trigger entity x7:entity/shock_pad (mover emits it)`
- `binding shock: action teleport targets x7:entity/shock_pad, a trigger, which does not afford teleport (teleport needs a teleport)`
- `prefab x7:prefab/security_door binding chime: action damage acts on the event's actor, and 'opened' never carries one`
- `binding maintenance_click: sound x7:sound/locked is provided by package x7.facility, which package x7.facility_world requires but does not import it from`

## The action seam (and what future agents will reuse)

Every requester asks for the same validated action:

```
a player's press   -> reach/facing test -> hta_went_use ------------------+
a binding          -> conditions        -> queue (action op, target, actor)|
Lua world.send     -> handle checks     -> hta_went_request ---------------+-> bounded queue -> mover / relay /
(a future agent)   -> reach its point   -> hta_went_request / hta_went_use +   teleport / use / damage / sound
```

`hta_went_request(w, op, target, actor)` validates that the target's kind
affords the action (`hta_wdef_affords`, the same table the loader checks
bindings with) and queues it; it answers queued, rejected, refused here (a
joiner) or queue full (**TESTED**). Lua's `world.send` now goes through it
(its five names are actions of the same names; its words, limits and
results are unchanged: `test_script` and `test_x3.sh` pass as before). A
`use` action goes through `hta_went_use`, the one path a press takes after
its reach test: the target's cooldown, its `used` event (links, then
bindings) and its script's `on_used` (**TESTED**: a legacy link -> relay ->
binding `use` on north's power button runs the whole power chain).

The contract publishes each kind's capabilities from the same tables
(`megamod-resources --json`, `bindings.affordances`: the actions a kind
affords, the events it reports, the state a condition can read, whether it
has a position). This is what Open Asset Lab's AI research
(`docs/research/ai/ENTITY_IO_AND_AFFORDANCES.md`,
`MEGAMOD_AI_ARCHITECTURE.md`) asks of X7: names that are world
capabilities (`use`, `open`, `activate`, `damage`, `teleport`), not UI or
NPC words; a single validated path players, Lua, bindings and a future
agent share; discoverable capabilities; host authority; bounded, traced
execution. X7 builds **no** affordance database, navigation, perception,
blackboard or agent: an agent would reach an interaction point with its
own movement, then request the same action. What X7 does not yet offer
such an agent: an action's asynchronous progress (`running`/`completed` --
today a request is queued or refused, and a mover's `opened`/`closed` is
the completion event), cancellation, and per-actor reach validation outside
a player's press.

## Package form

**World: `world_entities` schema 6** adds `bindings` (canonical ID order,
each once). A schema 6 world's own bindings may name any entity, a prefab
child by its placed ID (`x7:entity/north_door__door`) included. Schemas
1-5 load exactly as before; an X6 engine refuses schema 6 ("unknown field
'bindings'", measured with the real X6 binary: below).

**Prefab: prefab schema 2** adds a prefab's `bindings`, naming its children
by local ID. A member whose prefabs have none is still written as schema 1
by Open Asset Lab, byte for byte (the X6 library's SHA is unchanged:
**TESTED**). Schema 1 with bindings is refused
(`prefab x7:prefab/security_door: bindings need prefab schema 2 (this member is schema 1)`).
A schema 5 world may place a schema 2 prefab; an X6 engine refuses the
library.

OALMAP stays v3; `HTA_WORLD_KEY_SCHEMA` stays 1.

## Runtime compilation

At load (`world_def.c`), once:

1. Each binding object is parsed by one function for worlds and prefabs
   (`hta_wbind_parse_text`): names against the vocabulary, arguments
   against each action's schema, counts.
2. World: references resolved through the typed resolver (new fields
   `world_entities.bindings[].source`, `.conditions[].entity`,
   `.actions[].target`, `.actions[].at`, `.actions[].sound`), after prefab
   expansion so a child's placed ID resolves. Prefab: local IDs to child
   indices; the sound, when the package set loads, from the provider's
   view (`prefabs.prefabs[].bindings[].actions[].sound`).
3. Every instance appends its own copy of its prefab's bindings, local
   indices mapped to `first + index`: `north_door__button -> north_door__door`,
   `south_door__button -> south_door__door`. After this nothing knows a
   binding came from a prefab except its `instance` (for logs).
4. `hta_world_defs_check` checks every binding again against the expanded
   world (defence in depth).

At `hta_went_load`: bindings indexed by (source, event) with a stable
counting sort -- an event looks only at its own run -- and each action's
target turned into a generation-checked handle (re-made at a round reset,
like a link's). Nothing compares a string during play.

**Measured** (`test_bindings`, desktop x86-64): a world at the limits (64
entities, 128 bindings, 512 actions) costs 0.20 us for an idle step and
0.35 us for a step with a use every fifth step.

## Tick ordering (the host)

1. **host.interact** (before `hta_game_update`): a press at an
   interactable -> `used`: its links' inputs queued (authored order), then
   its bindings (canonical order) evaluated -- conditions read now, the
   same state for every binding of this event -- and their actions queued
   (authored order); a scripted interactable's call recorded.
2. `hta_game_update`; round restart if due (relays inactive, movers
   closed, the queue and cascades emptied, handles stale).
3. **host.world.sense**: trigger entries -> `entered` (links, bindings).
4. **host.world.script**: Lua `on_used` / `on_ability`; `world.send`
   requests queue after everything above.
5. **host.world.dispatch** (`hta_went_step`): the queue, first in first
   out, at most 256 per step. A relay's `activated` / `deactivated` and a
   mover told to open at the end of its travel emit here: their links' and
   bindings' actions queue behind and dispatch in the same step.
6. **world.movers**: movers move; one arriving open or closed emits
   `opened` / `closed` -- a new root, dispatched in the NEXT step.
7. **host.apply** (`session_tick.c`): teleports, damage
   (`hta_game_hurt`), sounds (and the world-sound effect to joiners).
8. network: snapshots, WORLD_STATE.

So `button used -> relay activate -> activated -> door open` completes in
the tick of the press; `door opened -> chime` happens when the door
arrives. **OBSERVED** (`test_x7.sh`, `--trace-events`):

```
[bind] step 609: event x7:entity/north_door__power_button used by unit 0 (depth 1)
[bind]   x7:prefab/security_door binding power_off (north_door): relay_state x7:entity/north_door__power is active -> false
[bind]   x7:prefab/security_door binding power_on (north_door): relay_state x7:entity/north_door__power is inactive -> true
[bind]   x7:prefab/security_door binding power_on (north_door): action activate x7:entity/north_door__power queued
[bind] step 610: event x7:entity/north_door__power activated by unit 0 (depth 2)
[bind]   x7:prefab/security_door binding powered (north_door): action open x7:entity/north_door__door queued
[world] sound x6shared:sound/door_hiss: x7:entity/north_door__door started opening
[bind] step 674: event x7:entity/north_door__door opened (depth 1)
[bind]   x7:prefab/security_door binding chime (north_door): action play_sound x7:entity/north_door__door queued
[world] sound x7:sound/chime at x7:entity/north_door__door (binding chime)
```

## Legacy links

Links are not rewritten or reinterpreted: old packages are byte for byte
unchanged and parse into the same `link[]`. At dispatch one function emits
an event: **its links first (authored order), then its bindings**; both
push onto the same queue with the same depth, handle and actor rules.
For a world without bindings the pushes are exactly X6's, in the same
order (**TESTED**: every X1-X6 unit test and end-to-end script passes
unchanged; X1-X6 keys and bytes unchanged). Links keep X1's vocabulary
(`used`/`fired`/`entered`, five inputs): `deactivate`, `use` and the new
events are for bindings only, so an X1-X6 link means what it meant. Mixing
a link and a binding on one event is allowed and ordered (**TESTED**).
Static link cycles are still refused (links are zero-delay by design);
binding cycles are allowed and bounded (below).

## Prefabs and instance isolation

`x7:prefab/security_door` (library `x7.facility`, reusing X6's art
library): children `button`, `power_button`, `power` (a relay), `door`,
props; bindings `power_on`/`power_off` (the power button, by the power's
state), `powered`/`unpowered` (the power's events -> the door),
`toggle_door`/`locked` (the button, by the power's state), `chime` (the
door opened). No Lua. Placed twice in `x7_facility_world` (north, and
south turned -90) and once in `x7_second_world` (turned 90): the behaviour
is authored once, in the library.

**OBSERVED** (`test_x7.sh`): north's unpowered button only sounds locked
and the door still blocks; north's power button opens north's door and
chimes; south's relay stays inactive and its door shut; a late joiner
finds north open and south shut. **TESTED** (`test_bindings`): the
compiled bindings point at each instance's own children; south's power
chain runs without touching north's.

## Lua coexistence

No Lua API change: `megamod.v1`, two callbacks, six functions, as before;
`world.send` goes through the action seam with the same names, limits and
words. A world's maintenance button has a binding (a click) and a script
(`x7:script/maintenance`: every second press opens north's door). On one
press the binding's action is queued at interaction, the script runs in
the script phase and its request queues after -- so the click dispatches
first, then Lua's `open`, and north's `chime` binding answers Lua's open
like any other (**OBSERVED** and asserted by `test_x7.sh` from the trace's
`dispatch` lines). Joiners never run a script or a binding (**TESTED**).

## Loops, storms and limits

| Limit | Value | On excess |
|---|---|---|
| bindings per world (expanded) | 128 | refused at load |
| conditions per binding / world | 4 / 256 | refused at load |
| actions per binding / world | 8 / 512 | refused at load |
| bindings on one source's one event | 16 | refused at load |
| bindings per prefab (conditions, actions) | 32 (64, 128) | refused at load |
| chain depth (links + bindings from a root) | 16 | dropped, named: `chain too long (a cycle?) at binding loop_b` |
| binding actions per root cascade | 128 (`HTA_WENT_CASCADE_BUDGET`) | the rest of that cascade dropped, named once |
| queue / dispatches per step | 512 / 256 (X1's) | dropped / deferred, counted |
| damage actions / sounds per step | 16 / 16 | dropped, counted |
| damage per action | (0, 500] (Lua's) | refused at load |

All **INVENTED** (ours), sized for a phone. A **root** is an event that
starts outside dispatch (a use, a trigger entry, a script request, a mover
arriving); everything it causes shares its cascade, tracked in a table
that can never fill (more slots than the queue has events). Pure-link
cascades are never cut by the cascade budget, so X1-X6 behaviour is
unchanged. **TESTED**: `A activated -> activate B`, `B activated ->
activate A` stops after 16 hops, names the binding, and the next press
works; a fan-out-8 storm through 16 relays stops at 128 binding actions
for its root (1808 dropped, one step, 0.04 ms) and a later use gets a
fresh budget; 2000 random binding graphs, every cascade within budget.
**OBSERVED**: the loop world (patched past Open Asset Lab, which refuses an
unconditional binding cycle) hosts, logs the chain limit naming the
binding, and north's power still opens north's door.

A door that reopens itself on `closed` is an oscillator, not a runaway:
`opened`/`closed` are new roots one step apart, so it costs one dispatch a
step. Open Asset Lab's cycle check follows zero-delay relay edges only.

## Compatibility identity

Bindings live in `world_entities` (a played member) and a library's
`prefabs` (in the library digest): the world key covers them byte for
byte, with no key schema change. **TESTED** (unit, OAL and `test_x7.sh`):
changing an event, an action (`toggle` -> `open`), a target, an amount,
a condition's value or the order of actions changes the key; a changed
prefab binding is refused before spawn with the world's bytes unchanged;
provenance does not change it. X1-X6 keys are unchanged (x1 `552c1757` ..
x6 `55b83b8b`, `f99b737d`). X7 keys: `x7_facility_world` `f8f15ddb`,
`x7_second_world` `5908f3ab` (engine == Open Asset Lab).

## Networking, authority and late join

Protocol **v10**; no new message type. The host evaluates every binding;
a joiner's `hta_world_entities` is `remote` and never emits, evaluates or
dispatches (**TESTED**). Results travel as they always did: mover state in
WORLD_STATE, positions in WORLD, damage and deaths in the game snapshot
and kill feed. The one presentation a joiner cannot derive from state -- a
binding's `play_sound` -- is sent as an FX packet of a new kind,
`HTA_NET_FX_WORLD_SOUND` (the world entity and the sound's asset index),
through the existing FX message. An older decoder would refuse that kind,
but no older build can load a world with bindings, so it is never in such
a match (X1's WORLD_STATE rule); the codec bounds entity (< 64) and sound
(< 512) and is fuzzed with the others. Late join is state, never event
history: a late joiner finds north open and south shut (**OBSERVED**).
Joiners' use presses still travel as CONTROL's action counter; the host
decides.

The 64-entity limit (WORLD_STATE's index) still bounds a world; bindings
count against no network budget.

## Introspection and debugging

- `megamod-resources --json`: `bindings` (events with sources, actor and
  link name; conditions with values; actions with targets, needs, takes,
  subject and path; argument meanings; limits; affordances per kind; the
  action seam; semantics; the tick phase), `world_entities.schema` 6,
  `prefabs.schema` 2 with its `bindings`, six new reference fields.
- `--markdown`: Events, Conditions and Actions tables (RESOURCES.md).
- `--bundle DIR --world NAME`: `bindings` -- each one's index, origin
  (world, or prefab and instance), source, event, conditions and actions
  by placed ID and entity index, sound and where it plays.
- `megamod-match --trace-events`: `[bind]` lines for every event with
  bindings, each condition's result, each action queued, each dispatch
  (and whether a binding, a link or Lua asked); bounded to 64 lines a step.
  Normal logs carry only drops and the bindings count.
- `megamod-match` ends with `bindings: N; matched, skipped by a condition,
  actions queued, dropped by the cascade budget, without an actor; damage
  applied` and each relay's state.

## Tests

| Test | Covers |
|---|---|
| `tests/test_bindings.c` (ctest `bindings`) | through the real package-set loader: compiled indices per instance; conditions; the power chain; instance isolation; damage, teleport and sound once per entry; link-before-binding order; `use` through a legacy link; the action seam's results; reset; a joiner never evaluates; A<->B loop and a storm; 37 refusals; the world key; 3000 mutated worlds/prefabs (twice each, same verdict and printable words); 2000 random binding graphs; 200 load/play/free cycles; cost |
| `tests/test_net.c`, `test_net_fuzz.c` | the world-sound FX kind round trip and bounds; fuzz with canonical re-encode |
| `scripts/test_x7.sh` (in `verify.sh`) | contract copies; keys engine == OAL; `--bundle` bindings; 15 refusals, OAL and engine in the same words; OAL's cycle refusal; host + joiners A, B (late), C: the proofs above; the second world; the loop world; compatibility |
| `scripts/test_cross_version.sh` | the real X6 binary refuses the X7 world, a schema 5 world placing the X7 prefab, and a schema 6 world; X6 worlds keep their keys, and X6 and this build admit each other's joiners on them |
| ASan/UBSan/LSan (`build-asan`) | `test_bindings`, `test_prefab`, `test_world_entities`, `test_resource`, `test_script`, `test_net_fuzz` clean |
| OAL `tests/test_bindings.py` | vocabulary and affordances from the contract; canonical records; refusals in the engine's words; prefab schema 1 kept; cycle check; keys pinned; X1-X6 package bytes pinned to c420f2a's |

## Emulator evidence (Android 14 x86_64)

**OBSERVED**, 2026-09-27, with the uncommitted X7 build (x86_64 beside
arm64, carrying only `x7_facility_world`, `x7_second_world`,
`x7.facility` and `x6.shared_assets`, bytes identical to Open Asset Lab's):
Create Game on `X7_FACILITY_WORLD`, 0 bots. The phone logged `[world] 21
world entities, 0 links`, `[world] 16 event bindings (2 the world's own, 14
from prefab instances): run here (host)`, both instance expansions, `[script]
1 scripts loaded (megamod.v1, host only)`, map check `1f11d1f9`, and drew the
north door with its two red panels from the player's start. Desktop joiners
through `scripts/emu/udprelay.py`, with `test_x7.sh`'s routes (map check
`1f11d1f9` on both sides): A found both doors shut, was blocked, pressed
north's button -- the phone's bindings `sound x7:sound/locked at
x7:entity/north_door__button (binding locked)`, A heard it and was still
blocked -- pressed north's power button: north opened, `sound x7:sound/chime
... (binding chime)`, A walked through; south stayed shut. Late joiner B
found north open, south shut; north's button closed north; the maintenance
button twice: the phone's Lua `maintenance press 1`, `maintenance press 2`,
`maintenance override opens entity x7:entity/north_door__door which was
closed`, each press with its click binding, then north's chime. C walked
onto the shock pad: `[world] unit 1 hurt 40 by x7:entity/shock_pad (binding
shock)`, `teleported to (-4.50 -4.00 0.05)`, C saw its damage and one
teleport and heard the pad. A joiner with a changed prefab binding was
refused (`different map`). No joiner logged a script or a binding line.
**Not checked in the emulator:** the phone host's own player pressing a
button (the same `hta_went_interact` as X6's, which the emulator checked).

## Not in X7

- NPCs, navigation, perception, blackboards, behaviour trees, utility AI,
  GOAP (see Open Asset Lab `docs/research/ai/`); an affordance database.
- Delays, timers, counters, arithmetic, expressions, per-binding state;
  `left`, `damaged`, `killed`, `round_started`.
- Actions on anyone but the event's actor (no `source`/`entity:` player
  selectors: an entity is not a player); attribution of binding damage to
  a player.
- Asynchronous action results and cancellation.
- A visual binding editor (Open Asset Lab has the data model and CLI).
- Nested prefabs, runtime spawning, Lua timers, protocol or entity-limit
  changes.
