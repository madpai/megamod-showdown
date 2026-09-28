# Night Shift -- package graph

Three packages, built by Open Asset Lab from `projects/night_shift`
(`assetlab project build projects/night_shift --output BUNDLE`), loaded by
MegaMod from a bundle directory (`maps/`, `packages/`). No package is
committed to either repository: they are rebuilt, byte for byte, from the
source (`scripts/test_night_shift.sh` checks two builds are identical).

```
maps/night_shift.oalmap            nightshift.world01    (OALMAP v3, world_entities schema 6)
  provides  nightshift:world/harrow_annex
            nightshift:mover/facility_cycle         the plant's hidden clock
            nightshift:script/anomaly               the one Lua script
  requires  nightshift.facility  -> 11 prefabs      (every prefab it places)
            nightshift.assets    -> 4 sign models + 11 sounds its own bindings play
     |
     +--> packages/nightshift.facility.oalasset   (library, prefab schema 2)
     |      provides  11 prefabs  nightshift:prefab/{security_door, breaker_panel, console, valve,
     |                            status_lamp, ceiling_light, alarm_light, shutter, data_core,
     |                            steam_vent, pump}
     |      requires  nightshift.assets -> 13 models + 8 sounds the prefabs draw and play
     |         |
     +---------+--> packages/nightshift.assets.oalasset   (library, X5 asset payload)
                      provides  17 textures, 17 materials, 17 models, 18 sounds
                      requires  nothing
```

`megamod-resources --bundle BUNDLE --world night_shift` (engine view):

```
"package": {"declared": true, "id": "nightshift.world01", "provides": 3, "requires": 2, "imports": 26,
  "set": [{"package": "nightshift.assets",   "direct": true, "digest": "de59ce387f6d21e1"},
          {"package": "nightshift.facility", "direct": true, "digest": "ba2f6d488d942451"}]}
```

plus `prefab_instances` (19, each with its expanded entity range),
`bindings` (59, each with origin: world or prefab + instance), `resolved`
(every typed reference, by field) and `assets`. OAL's own view:
`assetlab resources check maps/night_shift.oalmap --packages-dir BUNDLE`.

## Sizes (final slice)

| Package | Bytes | Digest / key | Contents |
|---|---|---|---|
| `nightshift.assets` | 1,692,347 | `de59ce387f6d21e1` | 17 textures (16x16 .. 256x64 RGBA8), 17 box models, 18 sounds (22.05 kHz mono PCM, ~1.6 MB of the total) |
| `nightshift.facility` | 9,988 | `ba2f6d488d942451` | 11 prefabs, 32 bindings, 41 expanded children when placed |
| `nightshift.world01` | 285,561 | world key `356266bd` | 2,724 triangles (flat-colour boxes), 21 hand-placed entities, 19 prefab instances, 27 bindings, 1 script |

## Resource naming

Logical IDs only; storage paths inside a package (`sounds/alarm.pcm`) are
never identity. Examples:
`nightshift:model/door_panel`, `nightshift:material/door_panel`,
`nightshift:texture/sign_lift`, `nightshift:sound/locked`,
`nightshift:sound/alarm`, `nightshift:prefab/security_door`,
`nightshift:script/anomaly`, `nightshift:mover/facility_cycle`,
placed entities `nightshift:entity/d1__door` (instance `d1`, child `door`).

## Important resource relationships

- `security_door` -> `door_panel`, `door_button`, `door_lamp` models;
  `door_servo` (the mover's own sound), `locked`, `power_on`, `power_down`
  (its bindings). Placed three times: D1, D3, the lift gate -- the lift is
  the same door on a different power feed.
- `breaker_panel` -> `breaker_box`, `click`. Placed twice (aux, lift).
- `steam_vent` -> `steam_plume`, `steam`; its bindings need its own
  plume's `mover_state`, the world only fires it.
- The world's bindings import the sounds they play directly
  (`generator`, `machinery`, `alarm`, `shock`, `distant_bang`, `knock`,
  `anomaly`, `confirm`, `power_on`, `lift`, `shift_over`) -- a world cannot
  borrow a prefab library's imports.
- Cross-package reuse proven by the graph: the world imports prefabs from
  `nightshift.facility` and art from `nightshift.assets`; the facility
  imports art from `nightshift.assets`; both resolve the same asset
  library (one digest in the set).

## Compatibility (tested)

| Change | World bytes | Key | Joiner |
|---|---|---|---|
| a world binding's damage 20 -> 21 | changed | `356266bd` -> other | refused before spawn |
| a prefab binding `toggle` -> `open` (facility only) | **unchanged** | changed | refused |
| one sound sample's byte (assets only) | unchanged | changed | refused |
| provenance of a prefab and a sound (both libraries) | unchanged | **unchanged** | admitted |

No Night Shift-specific compatibility logic exists: this is X4/X5/X6/X7's
world key over the whole package set.

An X6 engine (`scratch/engines/x6`, commit 2eae9df) refuses the set. Its
`megamod-content` prints, verbatim: `FAILED: package: package
nightshift.world01 requires package nightshift.facility:
packages/nightshift.facility.oalasset: package nightshift.facility: prefab
nightshift:prefab/breaker_panel: unknown f` -- the reason (per scripts/test_cross_version.sh: `unknown field
'bindings'`, X7's prefab schema 2) is cut off by the 192-byte error buffer
(`HTA_ERRLEN`, src/asset/cache.h), which this build shares
(ENGINE_FRICTION: long refusals are truncated).
