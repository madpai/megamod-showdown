# Session handoff — Halo Trial Android

**Date:** 2026-09-19
**Repo:** `/home/commander/projects/halo-trial-android`
**Map data (not in git):** `/home/commander/halo-trial-data/extract/maps/` (`bloodgulch.map`, `bitmaps.map`; `sounds.map` is there too, still unused)
**Device:** Galaxy S24+, Tailscale node `100.68.201.52` (`node`)
**Build host Tailscale:** `100.89.1.14`

Do **not** commit Trial `.map` files. The APK never bundles Halo assets.

---

## How to resume

```
cd /home/commander/projects/halo-trial-android
HTA_MAP=/home/commander/halo-trial-data/extract/maps/bloodgulch.map scripts/verify.sh
```

Look at the first-person view without a device:

```
./build-host/htaview $HTA_MAP --fp idle   --shots 2 --out /tmp/fp
./build-host/htaview $HTA_MAP --fp reload --shots 6 --out /tmp/rl
```

**Chasing a "stuck here" report.** The HUD readout gives x y z; these two turn
that into a measurement instead of a guess:

```
./build-host/htaprobe $HTA_MAP --at 96.57 -155.72 --z 0.81   # why it pushes
./build-host/htaprobe $HTA_MAP --find 0.81 --near 96 -155    # if a digit is unreadable
./build-host/htaview  $HTA_MAP --eye 96.57 -155.72 1.43 --yaw 180   # their exact frame
```

`htaprobe --at` prints the whole vertical column, the headroom, the standing and
crouching push, and every triangle near the body with a verdict (floor / ceiling
/ ledge lip / wall). Probe **from the player's own z**: a ground query from the
sky finds the roof and measures a different room. `--eye` takes the *eye*
position, so add the 0.62 standing eye height to their feet z.

## Getting a build onto the phone — do this every time

```
scripts/publish_apk.sh --title "what changed" --notes scratch/notes.html
```

Builds the APK, copies it to the serve root, refreshes `SHA256SUMS`, stamps the page
with the commit and build time, and starts the server if it is not already up. Then
**http://100.89.1.14:8731** (Tailscale bind only). Screenshots uploaded from the phone
land in `scratch/serve/uploads/` and are mirrored to `scratch/uploads/`.

`--notes` takes an HTML fragment; `--notes-text "..."` takes one paragraph inline;
`--no-build` publishes whatever is already built. The page itself is a committed
template at `scripts/sideload/index.html.tmpl` — edit that, not the generated
`scratch/serve/index.html`, which is overwritten on every publish.

The serve root is `scratch/serve/` (gitignored, so the maps and APK never enter git).
It used to live in a **session scratchpad under `/tmp`**, which is how the page came to
advertise a build from hours earlier while claiming to be current: the directory belongs
to a session that ended. If `publish_apk.sh` warns that a server is running with a
different root, kill it and rerun — otherwise it keeps serving the old files.

Git author on this repo has been Phase2 `<schultz0@proton.me>`. Do not push unless asked.

---

## Verified

**On the S24+ (2026-09-18)**

- Vulkan, textured Blood Gulch + lightmaps, sky/glass, scenery + vehicles
- COD-style HUD (stick, FIRE, JUMP, CROUCH); look while firing
- Pawn from Trial tags (`matg` + `cyborg_mp`): run 2.25, jump 0.07/tick, cam 0.62, radius 0.2, 45° slope
- Structure collision BSP + scenery/vehicle `coll` tags (Blood Gulch 19+28 → ~10155 verts / 18898 tris)
- Wall pill vs pylons; **walk off the red-base pad into the canyon** (user confirmed)

**On the host, in the offscreen renderer (2026-09-18) — not yet on device**

- **Animated first-person AR**: right hand, lower-right, barrel forward, left hand on
  the foregrip, hands and gun skinned from the Trial tags. Full reload plays.

`HTA_MAP=... scripts/verify.sh` is **31/31**. `test_player` 53 checks, `test_biped` 27,
`test_anim` 46.

---

## Slopes: what changed this session (2026-09-19)

The owner reported a doorway that **fits going up and not going down**. That
asymmetry is not geometry — it is the integrator, and it reproduces on a bare
synthetic ramp with no map at all.

Gravity alone does not keep a walking pawn on a downward slope. In one frame the
surface drops further than a standing start falls, so the pawn leaves it, and
keeps leaving it: **161 of 180 frames airborne walking down a 20-degree ramp**,
floating ~0.015 wu above it the whole way. Walking up, the ground snap puts the
feet exactly on the surface every frame: 0 airborne, 0.000 float.

That float lifts the head by the same amount. A leaning lintel with clearance
between 0.700 (standing) and 0.7148 (standing + float) therefore blocks one
direction only. It also reports "air" for the entire descent, which silently
costs the jump and the sneak speed.

**Fix:** in `hta_player_update`, when the pawn began the frame walking and is not
rising, snap it to the ground at the new XY *before* depenetration — but only as
far as the steepest walkable slope could have carried it this frame
(`0.18 + moved * tan(max_slope)`). Further than that is a ledge, and a ledge is
still a fall; `test_player`'s walk-off checks pin that.

Pinned by `tests/test_player.c` `[down a slope]`: without the fix, "stays on its
surface" and "does not go airborne" both fail.

## Guns: what changed this session (2026-09-18)

The hold was never an offset problem. The tags say so directly:

- `weap` trigger `first person offset` (trigger+136) is **(0,0,0)** for both the AR and
  the pistol. The old `fp_offset` default of `(0.18, 0.08, -0.12)` in `weapon.c` was
  invented, and is gone.
- Halo builds the FP view from **two meshes on one skeleton**: the hands
  (`matg` → first person interface → `mod2 characters\cyborg\fp\fp`, 37 nodes) and the
  weapon's own FP model (`weap+0x45C` → `mod2 weapons\assault rifle\fp\fp`, **5 nodes,
  no arms**). The `antr` at `weap+0x46C` has **42 = 37 + 5** nodes, and `frame gun` is a
  child of `frame r wriste`.

So the gun is in the right hand because the skeleton puts it there.

### Animation format (validated against real data)

`ModelAnimationsAnimation` is 180 bytes. Three 64-bit flag sets say, per node, whether
rotation / transform / scale vary per frame; varying components are packed per frame in
node order (rot 8B, trans 12B, scale 4B) and the rest appear once in default data, same
order. Therefore

```
default_size + frame_size == node_count * 24
```

which holds for all 13 AR clips and is asserted in `hta_anim_load`. It is the only thing
that catches the field order — **transform flags are at +0x5C, rotation at +0x6C**, and
swapping them still parses, it just silently trades 8- and 12-byte reads.

Two gotchas that cost real time, both now covered by tests:

1. **Rotations are stored conjugated** relative to the child→parent convention the
   transform algebra composes in. Taken as stored, the AR's barrel (its local +X, a
   0.289 wu / 88 cm span) points `(-0.10, +0.81, +0.58)` — up and to the left, which
   renders as a giant gun across the screen. Conjugated it is `(+1.000, +0.006, +0.008)`,
   straight down the view, and the gun moves to Y ≈ −0.04 (right). Conjugation happens at
   the two read sites: `read_node` in `anim.c` and `skin_bind_nodes` in `model.c`.
2. The hands model sets `mod2` flag `0x2` (**parts have local nodes**), so its vertex
   node indices are per-part table lookups (`part+107` count, `part+108` 22 bytes). The
   weapon FP model does not. Getting this wrong silently mis-binds the arms.

Cross-check that proved the parse before any rendering: the `antr` default-pose
**translations match the `mod2` bind translations exactly** for all 37 hand nodes and all
5 gun nodes (only the two roots differ, as they should — the animation places them
relative to the camera).

### Tag values now parsed

| Field | Where | AR value |
|---|---|---|
| FP model | `weap+0x45C` | `weapons\assault rifle\fp\fp` |
| FP animations | `weap+0x46C` | `antr weapons\assault rifle\fp\fp` |
| pickup / zoom sounds | `weap+0x490 / +0x4A0 / +0x4B0` | `ar_ammo`; AR has no zoom |
| magazine | `weap+0x4F0` | 60 loaded / 600 reserve / 240 initial, **3.4 s** reload |
| ROF | trigger+4 (two floats) | 15/s (pistol 3.5) |
| error angle | trigger+124 | 0.0349 → 0.1134 rad |
| projectile | trigger+148 | `proj weapons\assault rifle\bullet` |
| firing effects | trigger+264 | `effe …\fire bullet`, `effe …\empty`, `jpt! …\trigger` |
| graph sounds | `antr+0x054` | `ar_reload`, `ar_melee`, `weapon ready` |

Clips present: idle, firing, ready, reload-full ×2, melee, stealth-melee, put-away,
moving, posing, overlays, light-off, throw-grenade.

---

## NEXT WORK

### 0. Two things are waiting on the owner's next screenshot

- **The viewmodel on device** (below) — still never confirmed on Adreno.
- **Where the doorway actually is.** The slope fix above is the mechanism behind
  the up/down asymmetry, but the reported coordinate was never pinned down: the
  status bar and the camera cutout ate a digit of the X, and no column at
  `9?.57, -155.72` has a floor at z 0.81. The game is fullscreen now and the
  readout dodges the cutout, so the next screenshot should be legible. Feed it
  to `htaprobe --at`.

### 1. Confirm the viewmodel on the S24+

Everything above is verified in the **host** offscreen renderer only. Build, sideload,
and look. The GPU path changed: the viewmodel now uses
`hta_gfx_mesh_upload_dynamic` (one host-visible vertex-buffer slot per in-flight frame,
rewritten inside `hta_gfx_draw` behind that slot's fence). Adreno has not seen this yet.
Watch for: viewmodel flicker or tearing (slot/fence bug), and CPU cost of skinning 3200
verts per frame.

### 2. Sound — the owner's remaining ask

Nothing audible exists yet. `hta_viewmodel.sound_cue` already fires the tagged `snd!`
tag id when a clip crosses its sound frame; nothing consumes it.

- Decode `snd!` (and the `effe` firing effects) from **`sounds.map`**, which sits next to
  `bitmaps.map` (~77 MB). Tag structs are in `bloodgulch.map`; samples are almost
  certainly external, same pattern as bitmaps (`hta_resource_open` type 2 rather than 1).
- SetupActivity picks only `bloodgulch.map` + `bitmaps.map` — **add a `sounds.map`
  picker**.
- Android audio out (AAudio/OpenSL). **No synthesized or substituted gunshots, and
  nothing bundled in the APK.**

### 3. Projectiles

`proj weapons\assault rifle\bullet` is parsed but unused; `gun.c` is still hitscan with
scorch quads. Spawn from the camera plus the trigger's tagged error angle, then replace
scorches with the projectile's own impact effect.

### 4. Magazines / ammo

Counts and reload time are parsed and unused: no ammo is tracked, reload is never
triggered, and `HTA_VM_RELOAD` is only reachable by calling `hta_viewmodel_play`
directly. Wire ammo → reload clip → `chamber_time`.

Netcode (Phase 5, own protocol) still waits until a local shot looks **and sounds** like
a gun.

---

## Other leftovers (not the next slice)

- Vehicle `shader_model` still dark/flat (unused sun push constants; soso not fully wired)
- Warthog chaingun over the cabin is bind-pose barrels — the same skinning that now works
  for the viewmodel could fix it
- Shrubs/ferns with no `coll` tag still ghost; colliders are hollow (spawn *inside* a hog
  will not shove you out)
- No world weapon pickups; you spawn with the AR
- Gamepad / BACK-to-exit not confirmed on device (BACK now competes with the
  gesture-nav back swipe; the HUD claims the lower half of both edges via
  `setSystemGestureExclusionRects`, which the platform caps)
- FP arms are lit by the world light only; Halo lights the viewmodel separately

### Headroom / overhangs (fixed 2026-09-18, confirmed by the owner on device)

Standing, you could not get into a base doorway unless you crouched. Cause:
`hta_collision_depenetrate` treated **downward-facing** faces as walls and pushed the
pawn horizontally out of them. Blood Gulch's entrances lean over the door at 55-70
degrees; standing, the pill's mid-height probe reached the sloped roof, and ducking
dropped below it. A ceiling is a vertical limit, not a lateral one, so faces with unit
`nz < -0.10` are now skipped for horizontal push. Winding is consistent in the
collision BSP (floors read `nz ~ +0.9`, ceilings `~ -0.96`), so the sign is meaningful.

Sweeping every floor cell on the map for "standing is pushed, crouching is not":
**204 cells before, 49 after**. `tests/test_biped.c` pins the base entrance (12 -> 2).

The two that remain there are wall corners, not roofs, and they point at the real
remaining weakness: the pill test probes the closest point on a triangle from a
**single point at mid-height**, then clamps into `[feet, feet+height]`. That is not a
cylinder-vs-triangle test. A proper segment-vs-triangle closest point would clear
them. Worth doing before netcode, since the server will need the same test.

There is still **no head clamp**: nothing stops you walking into a space shorter than
0.7 wu, you just are not shoved out of it any more.

### Test fidelity: set the slope, or you are testing different physics

`hta_collision_build` defaults `walkable_nz` to 0.5 (60 degrees). The biped tag says
45 degrees (`cos = 0.7071`). Only `platform_android.c` used to override it, so every
host test simulated a more forgiving pawn than the device -- which is exactly how the
doorway bug hid from a green suite. Call `hta_collision_set_slope(&col, phys.max_slope)`
after building, wherever real tag physics are available.

### Collision notes the next agent should not re-break

- Walk-off: do **not** restore "no walkable ground → slide XY back". That was the
  invisible wall at the base lip. Fall instead. Walls stay on the pill.
- Pill: skip faces whose `zmax <= feet + 0.18` (ledge lips). Real pylons rise above that.
- Object `coll` verts are **node-local** (apply GBXModel rest T/R). Render verts are
  already model-space.

---

## Key files

| Area | Path |
|---|---|
| Animation graph (`antr`), transform algebra | `src/asset/anim.c`, `src/asset/anim.h` |
| Viewmodel: load, playback, CPU skinning | `src/engine/viewmodel.c`, `.h` |
| Skinned mod2 append + bind pose | `src/asset/model.c` `hta_model_append_skinned` |
| Weapon / magazine / trigger parse | `src/asset/weapon.c`, `.h` |
| FP hands from globals | `src/asset/weapon.c` `hta_globals_fp_hands` |
| Dynamic vertex buffer + viewmodel pass | `src/gfx/gfx_vulkan.c`, `src/gfx/gfx.h` |
| Host FP preview (`--fp <clip>`) | `src/tools/htaview.c` |
| Pawn physics | `src/asset/biped.c` |
| Movement / pill / walk-off | `src/engine/player.c` |
| Hitscan + scorches | `src/engine/gun.c` |
| Collision BSP + object coll emit | `src/asset/bsp.c`, `src/asset/model.c` |
| Resource maps (bitmaps only today) | `src/asset/bitmap.c` `hta_resource_open` (type 1; type 2 = sounds) |
| Tag layouts | `upstream/invader/src/tag/hek/definition/*.json` |
| HUD, fullscreen, gesture exclusion | `android/.../GameActivity.java` |
| Fit probe at a coordinate | `src/tools/htaprobe.c` |
| Map / bitmap picker | `android/.../SetupActivity.java` |
| Android glue | `src/platform/platform_android.c` |
| Animation + skinning tests | `tests/test_anim.c` (needs `HTA_MAP`) |
| Tag physics tests | `tests/test_biped.c` (needs `HTA_MAP`) |

**Reading Invader's JSON:** count a field with `"bounds": true` as **two** values. Missing
that is what made `Weapon` come out 4 bytes short and put every offset past
`zoom magnification range` (0x3DC) in the wrong place.
