# Night Shift -- authoring workflow (as actually done)

How the slice was built, step by step, with the commands that were really
run. Open Asset Lab (OAL) is `../open-asset-lab`; MegaMod is this repo.
What hurt along the way is in [OAL_FRICTION](OAL_FRICTION.md).

## 0. Where things live

```
open-asset-lab/projects/night_shift/
  project.py          libraries() and worlds(): what `assetlab project` builds
  art.py              nightshift.assets: textures, materials, models, sounds, the sign font
  facility.py         nightshift.facility: the 11 prefabs and their bindings
  world01.py          nightshift.world01: HARROW ANNEX -- geometry, placements, bindings
  scripts/anomaly.lua the one Lua script
open-asset-lab/assetlab/project.py      `assetlab project build|budget` (added for Night Shift)
megamod/scripts/test_night_shift.sh     the automated proof
megamod/docs/night_shift/               design and findings (this folder)
```

Built packages are never committed; they are rebuilt from the source.

## 1. Assets -> logical resources (`art.py`)

There were no legitimately redistributable facility assets to import, so
every asset is original and made in code (GPL, provenance recorded):

- **Textures:** a small `Canvas` drawn texel by texel (hazard stripes,
  door ribs, a breaker, a console screen, a valve wheel, a corrugated
  shutter, steam noise). Rows are written bottom-up because a box model's
  face samples v = 0 at its foot (found on the first screenshot).
- **Signs:** a 5x7 pixel font in the file; `sign_canvas()` renders lines at
  2x into a power-of-two texture.
- **Models:** `assets.box_model(id, half_extents, [material])` -- one box
  per model, one material each.
- **Sounds:** 18 clips synthesised from sines, squares and seeded noise
  (buzz, servo, power up/down, generator, machinery, alarm, shock, steam,
  distant crash, knocking, the anomaly chord, core release, pump, lift,
  closing chord).
- Each becomes a resource with a logical ID -- `nightshift:texture/door_panel`,
  `nightshift:material/door_panel`, `nightshift:model/door_panel`,
  `nightshift:sound/alarm` -- in `Library('nightshift.assets', ...)`.

## 2. Prefabs (`facility.py`)

`prefabs.Prefab(id, children, bindings=...)`: children are the six entity
kinds in prefab space (wall plane x = 0, front toward -x), bindings name
children by local ID. The library's `requires` is **computed** from the
models and sounds its prefabs use (hand-written lists drift). Example, the
security door: `door` (mover drawn by a model, with a servo sound),
`button` + `button_back`, a through-wall `plate` prop, a `power` relay, a
`lamp` mover; six bindings (`locked`, `locked_back`, `toggle`,
`toggle_back`, `powered`, `unpowered`).

## 3. The world (`world01.py`)

- **Geometry:** helpers `wall_x/wall_y(plane, from, to, height, gaps=[(a, b, lintel)])`,
  `floor`, `slab` (ceilings), `deco` (fixtures, non-solid decals). All
  static detail is boxes (free against the entity budget, flat colour).
- **Placed entities:** relays for world state, triggers, teleports, four
  sign props, the hidden clock (a mover *definition*: required once props
  exist), the Lua hook.
- **Prefab instances:** `PrefabInstance(id, prefab, position, yaw_degrees)`
  -- e.g. `d1` turned 90 into the dock's north wall, the lift gate
  unturned in the east wall, the dock shutter turned 180 so it slides the
  other way.
- **Bindings:** `EventBinding(id, source, event, [Condition], [Action])`
  with small local helpers (`eid`, `child(instance, name)`, `rs` for a
  relay condition, `snd` for a sound action).
- **Script:** `Script('nightshift:script/anomaly', source, ['on_used'])` on
  the hook entity.
- `package = 'nightshift.world01'`; `requires` computed from what the
  world places and plays.

## 4. Validate and build

```sh
cd open-asset-lab
.venv/bin/python -m assetlab project budget projects/night_shift     # validate, report budgets, write nothing
.venv/bin/python -m assetlab project build projects/night_shift --output BUNDLE [--json]
```

`budget` runs OAL's full validation (IDs, references, affordances,
binding cycles, the runtime's limits, Lua parse when `luac5.4` exists) and
prints entities by kind and by prefab against 64 and bindings against 128.
`build` writes `BUNDLE/maps/night_shift.oalmap` and
`BUNDLE/packages/{nightshift.assets,nightshift.facility}.oalasset`.

## 5. Inspect

```sh
megamod-content --trial TRIAL --bundle BUNDLE --world night_shift   # loads the set; world key (must equal OAL's)
megamod-resources --bundle BUNDLE --world night_shift               # package set, expansion, every compiled binding
assetlab resources check BUNDLE/maps/night_shift.oalmap --packages-dir BUNDLE
```

## 6. Play and look

```sh
megamod-match --bundle BUNDLE --world night_shift --bots 0 --seconds 300 --trace-events --host PORT &
megamod-join 127.0.0.1 PORT --world night_shift --map TRIAL/bloodgulch.map --bundle BUNDLE \
    --route "x,y;Lx,y;E;w1;..." --auto 60 --shot view.ppm
```

`--route` walks waypoints, looks (`L`), presses use (`E`), waits (`w`) and
expects blocks (`B`); `--shot` writes what the joiner saw. Each viewpoint
was a separate joiner (a host keeps the world's state between them); the
PPMs were converted to PNG and looked at. `--trace-events` on the host
prints every binding decision (see STATE_GRAPH for real lines).

## 7. Automate

`scripts/test_night_shift.sh` (in `verify.sh`): build twice (identical),
keys, the resolved graph, a crew of seven joiners through the scenario,
late join, package mutations, the X6 engine.

## 8. Android

```sh
mkdir -p scratch/emu-ns/packages
cp BUNDLE/maps/night_shift.oalmap scratch/emu-ns/ && cp BUNDLE/packages/*.oalasset scratch/emu-ns/packages/
HTA_IMPORTED=$PWD/scratch/emu-ns scripts/emu/start.sh        # emulator APK with only Night Shift
scripts/emu/emu.py stop start wait:8 tap:1870,400 wait:1.5 tap:1870,400 wait:2.5  # MULTIPLAYER -> Create Game
# MAP (686,518) once -> NIGHT_SHIFT; BOTS (686,818) until 0; START (1670,820); SPAWN (696,336)
python3 scripts/emu/udprelay.py 32271 32270 &                # then the same crew joins 127.0.0.1:32271
```

For the owner's phone: put `night_shift.oalmap` in `$HTA_IMPORTED` and the
two libraries in `$HTA_IMPORTED/packages/`, then `publish_apk.sh --with-assets`.
