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

## The counter on the gun, and a strip bug it uncovered (2026-09-19)

**Confirmed on the S24+:** magazine, reload and muzzle flash all work.

The owner asked for the AR's own little LCD round counter. Chasing it found a
model-parsing bug that had been silently damaging **every** model in the game.

### Triangle strips were two triangles short, everywhere

`ModelGeometryPart`'s field is **`triangle count`** -- triangles, not indices.
A strip of N triangles is **N + 2** indices. The parser read N, so every strip
part lost its last two triangles. On a 1682-triangle gun body that is
invisible; on a 2-triangle part it is total. The assault rifle's two ammo
digit quads are exactly 4 vertices and 2 triangles, so they emitted nothing
and were dropped by the `emitted == 0` guard.

Note the loop bound and the bounds check are separate: fixing `index_words`
alone does nothing, because the strip decoder iterated `tcount`.

After the fix the FP model yields all **8** parts instead of 6, and every mesh
in the game gained its missing tail triangles (gun body 3123 -> 3129 indices).
`test_anim` pins it: a two-triangle strip part must survive.

### The readout

It is two quads on `frame display`, each a `shader_transparent_chicago` whose
**numeric counter limit is 60** -- the weapon's magazine size. That is the tag
saying "this counts that", and it is how the digits are told apart from the
AR's compass, which is also a counter but with a limit of 8.

`numbers_plate` is **not a sprite sheet**: it is a ten-frame bitmap, one image
per digit. The ten frames are decoded once into a single wide atlas
(`hta_mesh_intern_atlas`), so choosing a digit is a shift in U with no
per-frame cost and no descriptor-set churn. The model's own UVs are kept and
squeezed into the digit's tenth, because the glyph occupies a sub-rectangle of
each frame.

The more +Y quad is the tens digit -- Halo FP axes put +Y to the left.

### Chicago shaders now use their declared blend

`hta_shader_draw_mode` guessed from tag *names* ("light", "teleporter") and
fell back to ALPHA for every chicago shader. They declare a framebuffer blend
function at **+44** (Shader base is 40). Drawn as ALPHA the display's black
background painted a box over the gun.

19 of Blood Gulch's 54 chicago shaders change, all ALPHA -> ADD, and every one
is something that should glow: the AR display and compass, the Warthog
speedometer and sensor, the needler's luminous core, active camouflage, a door
blinker. Nothing loses transparency. Checked against canyon renders.

## Ammo, reload and the muzzle flash (2026-09-19)

**Confirmed on the S24+:** the rifle sounds right and full auto is clean.

Two gaps the owner hit: no reload could be heard because nothing triggered a
reload, and there was no muzzle flash to judge audio/visual sync against.

### Magazine

All from the Trial's own `WeaponMagazine`. **"Rounds total initial" counts the
loaded magazine**, so the AR's 240 is 60 loaded + 180 reserve -- Halo exactly --
with 60 per reload and 3.40 s. Firing empty clicks the weapon's own empty
effect and auto-reloads, with a 0.35 s cooldown so a dead trigger does not
click every frame once the reserve is gone too.

Ammo must gate the shot **before** `hta_gun_fire`, which spends the cooldown
whether or not the magazine could pay -- hence `hta_gun_ready`.

Reloading plays the FP reload clip, which is what finally fires the animation
graph's own reload sound frames. Those were wired to the mixer already and
simply had no clip to fire in.

### Muzzle flash

Which particle is "the" first-person muzzle flash needs no guessing and no
matching on tag paths. The tag says so four ways over, and
`hta_effect_fp_flash` uses all four:

- attached to the `primary trigger` marker (effect **locations** are just
  marker names, and location 0 is the muzzle, location 1 the ejection port)
- `create in` is air or any -- the water variants are different particles
- `create` is not third-person -- what other players see is not what we see
- its `part` **blends additively**; the smoke at the same marker alpha-blends,
  and that is what tells them apart

On the Trial's AR exactly one particle satisfies all four: `flash h ar`, whose
bitmap is literally called `flash h ar fp`. Radius 0.125, life 75 ms, oriented
*parallel to direction* -- along the barrel, not screen-facing.

Three things this cost, all visible in one render and worth not re-learning:

1. **The viewmodel pass ignored `draw_mode`** and drew everything with the
   opaque pipeline, so the flash sprite's black background painted over the
   world. It now runs the same three passes the world pass does.
2. **The flash bitmap is a sprite sheet**, `type` 3. Nine variants, each its
   own "bitmap group sequence" holding one sprite; Halo picks a sequence per
   shot. Using 0..1 UVs draws the entire sheet at once.
3. The quad's four vertices are **not skinned** -- they are placed by hand
   from the marker -- so they are deliberately unbound, and `test_anim`'s
   "every vertex is bound" invariant had to be narrowed to the skinned range.

Geometry lives in the viewmodel's own mesh: four vertices and one ADD submesh
appended after the gun, so it is posed and drawn in view space with everything
else -- no second pass and no world transform to get wrong. Unlit, the four
vertices collapse onto the muzzle so the triangles have zero area; that beats
skipping the submesh, since the index buffer is uploaded once and never edited.

**Known simplification:** the nine sprite variants span two bitmap sheets, and
only the six on sheet 0 are used, to keep this to one texture and one submesh.

The flash needs `bitmaps.map`; without it `have_flash` stays false and the
weapon simply has no flash.

## Sound (2026-09-19)

**Confirmed on the S24+ this session:** ramps work standing, both directions,
at both bases; the animated first-person AR renders on Adreno at 120 fps
(that was NEXT WORK #1 and is now closed); fullscreen and the position readout
work.

What the Trial ships, measured: 333 `snd!` tags, 951 permutations, 11.4 MiB of
samples, **every byte in sounds.map and none in the cache**. 294 tags are Xbox
ADPCM, 39 are Ogg Vorbis -- and the Ogg is announcer dialogue, not effects. So
no Vorbis decoder is on the critical path; that is only needed for the MP
announcer later.

Xbox ADPCM is IMA with a fixed 36-byte block per channel: 4-byte header (int16
seed, step index) then 8 chunks of 8 nibble codes. **The seed is itself the
block's first output frame**, so a block yields 64 frames and the last code of
the final chunk goes unused. Mirror that or every block after the first drifts.
Every permutation size in the map is a whole multiple of 36.

The decode is **bit-exact against ffmpeg's independent IMA decoder** over all
15488 samples of the AR's first gunshot permutation (ffmpeg keeps the 65th
sample per block that Halo drops; every sample Halo keeps matches).

**The gunshot is not on the weapon.** Halo hangs it off the trigger's firing
`effe`, among that effect's parts: `hta_effect_first_sound` walks
events -> parts for a `snd!`-classed dependency. On Trial that resolves
`weapons\assault rifle\assault rifle` -> `sound\sfx\weapons\assault rifle\fire`,
4 permutations, 0.60-0.80 s. Halo picks between permutations rather than
repeating one, and so do we.

Architecture: the mixer (`src/engine/audio.c`) is portable and knows nothing
about Android, so resampling, summing, clamping and voice stealing are all
tested on the host. `src/platform/audio_android.c` only owns an AAudio stream
and hands its callback buffer to `hta_audio_mix`. The device picks the rate;
each voice carries its own resampling step. Game thread and audio thread share
only an SPSC ring of play requests, so the audio callback never blocks or
allocates.

Gotchas worth not re-learning:

- `hta_audio_init` wipes the clip table, and `hta_audio_android_start` calls it
  with the device's format -- so **start the stream before registering clips**,
  and restore clips and master gain after a route-change restart
  (`hta_audio_android_poll` does).
- Master gain is 0.45. The AR fires 15/s with a 0.70 s sample, so ten shots
  overlap in steady fire; at unity that sums into the clamp and buzzes.
- `AAudioStreamBuilder_setUsage` is API 28 and `__builtin_available` does not
  gate it in the NDK's C path. It is only a routing hint, so it is omitted
  rather than raising minSdk.
- minSdk is 26 for AAudio. The manifest already requires Vulkan 1.1, so the
  real floor was never 24.

Still silent: footsteps, impacts, projectile effects, and anything Ogg.

## The pawn's crown is rounded (2026-09-19)

The ramp block was **not** the slope fix below — that was real and separate. The
reporter's coordinates pinned it exactly: stuck at **x 99.574, y -159.34**, head
at 1.511, against `tri 5530/5531` — the base floor slab's vertical leading edge,
spanning z **1.50 .. 1.70**. The standing column topped out 11 mm inside it. The
crouch column tops at 1.31 and strolls through, which is what the owner saw.

`scratch/walkramp.c`-style simulation reproduces it on the real map to the
millimetre, so this was never a measuring artefact.

**Why 11 mm mattered.** A flat-topped cylinder meets an overhead lip a full
radius early. On a descending ramp the floor is still `radius * slope` higher
back there -- 0.20 * 0.6 = **0.12 wu** here -- so the pawn bangs its head on a
lip it clears completely one step later. Clearance at the lip itself is 0.815
against a 0.70 pawn: it fits, easily. The shape was wrong, not the geometry.

**Fix:** cap the body with a hemisphere of the same radius. Below `z1 - r` it is
the full-radius cylinder it always was; inside the cap the usable radius narrows
to zero at the crown (`reff = sqrt(r^2 - (zmin - cap_base)^2)`, measured at the
LOWEST part of the face in the cap, where it bites hardest). A face reaching any
lower than the cap still blocks at full radius, so walls, pylons and hog flanks
are untouched -- `test_player`'s wall and chest-height-roof checks pin that.

Pinned by `tests/test_player.c` `[lip over a ramp]`: with a flat top the standing
pawn stops dead at `lip_x - radius` while the crouching one walks through.

## Slopes: also this session (2026-09-19)

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

### 2. Sound — done for the gun; see "Sound" above. What is left

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
| Portable voice mixer | `src/engine/audio.c`, `.h` |
| Magazine / reload | `src/engine/ammo.c`, `.h` |
| effe walking, FP flash selection | `src/asset/effect.c`, `.h` |
| Model markers | `src/asset/model.c` `hta_model_marker` |
| Sprite-sheet UVs | `src/asset/bitmap.c` `hta_bitmap_sprite_at` |
| Muzzle flash quad | `src/engine/viewmodel.c` `setup_flash` / `pose_flash` |
| On-gun round counter | `src/engine/viewmodel.c` `setup_counter` / `pose_counter` |
| Digit atlas, declared blend | `src/asset/bitmap.c` |
| AAudio stream | `src/platform/audio_android.c` |
| snd! + Xbox ADPCM + effe->snd! | `src/asset/sound.c`, `.h` |
| Sound inspector / WAV dump | `src/tools/htasound.c` |
| Fit probe at a coordinate | `src/tools/htaprobe.c` |
| Map / bitmap picker | `android/.../SetupActivity.java` |
| Android glue | `src/platform/platform_android.c` |
| Animation + skinning tests | `tests/test_anim.c` (needs `HTA_MAP`) |
| Tag physics tests | `tests/test_biped.c` (needs `HTA_MAP`) |

**Reading Invader's JSON:** count a field with `"bounds": true` as **two** values. Missing
that is what made `Weapon` come out 4 bytes short and put every offset past
`zoom magnification range` (0x3DC) in the wrong place.
