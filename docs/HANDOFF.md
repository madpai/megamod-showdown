# Halo Trial Android — handoff

**Read this file. You should not need anything else to start.**
`docs/JOURNAL.md` is the session-by-session history — go there only when you
want the *why* behind something, and search it by symptom.

**Date of this revision:** 2026-09-22
**Repo:** `/home/commander/projects/halo-trial-android`

---

## What this is

A clean-room native ARM64 Android engine that reads the owner's own legally
obtained **Halo: Combat Evolved Trial** data and plays Blood Gulch. C, Vulkan,
AAudio, no engine dependencies. Every number it can get from a tag, it gets
from a tag.

**Hard constraints — do not break these:**

- **No Halo assets, executables or DLLs are ever committed or bundled in an
  APK.** The owner supplies their own Trial copy; the app asks for it on first
  run. No DRM is involved or circumvented.
- **Never commit Trial `.map` files.** They live outside the repo at
  `/home/commander/halo-trial-data/extract/maps/`.
- Project code is **GPLv3**.
- Git author on this repo is **`Phase2 <schultz0@proton.me>`**.
- **Do not push unless asked.**

---

## The loop

This working rhythm is the owner's, it is good, and it should be kept.

### 1. Change something, then verify

```
cd /home/commander/projects/halo-trial-android
HTA_MAP=/home/commander/halo-trial-data/extract/maps/bloodgulch.map scripts/verify.sh
```

58 checks: host build, every unit test twice (synthetic, then against the real
map), a synthetic-fixture CLI pass, an offscreen render, the APK build, and the
APK's contents (arm64 only, no bundled audio, right entry points).

**`verify.sh` must be green before you publish.** If you add a module, add its
test to `CMakeLists.txt` *and* to both halves of `verify.sh`.

Note: `src/platform/platform_android.c` is **only** compiled by the Android
target. `cmake --build build-host` will not catch a mistake in it — only
`verify.sh` (via `gradle assembleDebug`) will.

### 2. Publish to the sideload server

```
scripts/publish_apk.sh --title "what changed" --notes-text "a sentence or two"
```

Builds the APK, copies it to the serve root, refreshes `SHA256SUMS`, stamps the
page with the commit and build time, and starts the server if it is not up.
The owner then installs from **http://100.89.1.14:8731** (Tailscale-bound).

- `--notes` takes an HTML fragment; `--notes-text` takes one paragraph.
- `--no-build` publishes whatever is already built.
- The page is a committed template at `scripts/sideload/index.html.tmpl` —
  edit **that**, not the generated `scratch/serve/index.html`.
- The serve root is `scratch/serve/` (gitignored, so maps and APKs never enter
  git). It must **not** live in a session scratchpad under `/tmp`: that
  directory dies with the session and the page then advertises a build from
  hours earlier while claiming to be current. If `publish_apk.sh` warns that a
  server is running with a different root, kill it and rerun.

**Write real release notes.** The owner reads them, and they are how a change
gets tested deliberately rather than stumbled into.

### 3. The owner tests on device and sends screenshots

Screenshots uploaded from the phone land in `scratch/serve/uploads/` and are
mirrored to `scratch/uploads/`. List them newest-first:

```
ls -lt scratch/uploads/ | head
```

Then read them directly — they carry the HUD readout (position, ground/air,
**fps**) which is often the whole diagnosis. The 120 fps idle reading is what
proved the particle-lag was CPU raycasts and not fill.

### 4. Say what to look at next

End a session by naming the **current testing objective** — what specifically
to try on device and what a failure would look like. Keep it in this file, in
the section below, and update it every time.

---

## CURRENT TESTING OBJECTIVE

> **Network slice in progress (2026-09-21).** Read
> [`NETWORK_PROGRESS.md`](NETWORK_PROGRESS.md) and
> [`NETWORK_ARCHITECTURE.md`](NETWORK_ARCHITECTURE.md). The old 57-check
> baseline is tagged `net-baseline-2026-09-21`; the current gate is 58/58.
> An automated two-process desktop run on the real Blood Gulch map received
> remote movement in both directions, with one captured image showing the
> other Spartan. No two-human or Android device test has happened. Next,
> use `HTA_MAP=... scripts/run_two_players.sh` and check movement, jump,
> crouch, AR/pistol selection and fire/melee/grenade animations in both
> windows. Then test Android ↔ desktop LAN. Movement is provisional client
> submitted transform relay; no authoritative damage or player combat yet.
>
> **Vehicle slice accepted for now (2026-09-21).** The owner says handling is
> much better and wants to move on. There is no active vehicle phone test.
> The latest collision release (`8f6ecca`) passed all 57 verification checks;
> its rebound, powered pivot and jeep-to-jeep impulse have host coverage, but
> no separate phone report confirming those exact behaviors. Keep the planar
> collision, damage and seat gaps below visible when returning to vehicles.
>
---

## Where things stand

### Working, confirmed on device

Blood Gulch renders with lightmaps, detail maps, sky and glass; scenery and
vehicles are placed and collidable. First-person movement on the Trial's own
biped physics — run, crouch, jump, slopes, headroom, fall damage.

**Weapons.** All eleven playable, with real first-person models, hands from
`globals`, and their own animation graphs: ready, idle, fire, reload (shotgun
shell-at-a-time chaining included), melee, and `throw-grenade`. Muzzle flashes
from the firing effect's own particle. Spent brass from the ejection marker.
Per-weapon zoom with the sniper's scope furniture. On-gun round counters drawn
with the HUD font.

**HUD.** Crosshair, shield and health meters, ammo pips, the rounds counter,
scope overlays, and a full-screen fade.

**Sound.** Positional and panned, the weapon's own firing sounds, footsteps by
material, impacts by material, the flamethrower's looping roar, the shield's
five HUD sounds (recharge hum, hit, low, depleted, heartbeat), and the Chief's
death lines.

**Combat.** Hitscan and object projectiles, per-material impact effects and
decals, particles with the tags' own physics and colours, grenades, rockets
that explode properly, health and shields with recharge.

**Dying.** Full sequence: death line, the camera leaves your head and watches
your body go down with one of Halo's kill animations, five seconds, respawn at
a spawn point away from where you died.

**Items.** Blood Gulch's own 37 placements with real respawn timers and
weighted choices. Two-weapon carry, the map's own AR+pistol loadout, SWAP to
switch or to pick up.

**World-object filtering.** Bodies, corpses, pickups, grenades and projectiles
now upload mipmaps once alongside their textures. HUD, viewmodel and particle
uploads retain their existing filtering. Device confirmation pending.

**A body to shoot at.** Stands in front of the spawn with the player's health,
shield and collision shape, holds a rifle, flinches, dies, comes back.

**A drivable Warthog.** Human jeeps (`vehi+756 == 1`) come out of the static
world into their own render mesh and their own collision grid, and the twelve
Blood Gulch placements are drivable: enter, throttle, reverse, steer, brake,
exit. Speeds, acceleration, steering lock and turn rate all come from the
vehicle tag; the wheelbase and mass points come from its `phys` tag. Chase
camera, DRIVE/EXIT/BRAKE touch labels and a km/h speedometer. The moving hull
is solid to bullets, grenades, footsteps and the player. The owner has tested
the driving revisions on device and accepted this vehicle slice for now. The
tire meshes spin and steer by model node and travel down to meet terrain. The
chassis can leave the ground over a crest. Wall and vehicle contacts now
exchange a 2-D impulse using the `phys` mass, centre of mass and yaw inertia;
throttle keeps spinning the tires under load and can pivot a blocked jeep.
The exact behavior of the latest collision release is host-verified; a
separate phone result was not reported.

**Placed model textures.** Model UV scales are restored from `mod2+48/+52`;
opaque placed `shader_model` surfaces use scene lighting. This fixes the
Warthog sampling the wrong texture regions and being drawn unlit. Reflections
remain a rendering gap; phone confirmation of this fix is pending.

### Not started

- **Vehicles beyond the driver's seat.** The Warthog now drives (see below).
  Passengers, the turret, vehicle damage and flipping, and the Scorpion,
  Ghost and Banshee are all still untouched.
- **Bots.** The body exists; it needs somewhere to walk, something to walk
  towards, an eye test and a trigger. None of that touches what is there.
- **Authoritative multiplayer combat.** The first LAN transport and visual
  replication slice exists; server-side movement, damage, death, vehicles and
  game modes are still absent. See `NETWORK_PROGRESS.md`.
- **Menus.** `ui.map` holds the entire Halo shell as 811 `DeLa` widget
  definitions, 168 string lists, 6 fonts, 222 bitmaps, a virtual keyboard and
  a map list. A faithful main menu is a widget-tree interpreter, not asset
  work.
- **The announcer.** All 39 multiplayer lines are in `bloodgulch.map` already
  (`sound\dialog\multiplayer1\*`): slayer, CTF, double kill, killtacular,
  game over. They need a game mode to mean anything.
- **Music.** There is none in Blood Gulch, and that is correct — Halo CE
  multiplayer maps carry no score. The campaign map has it.

### Multiplayer continuation

- The gameplay state and per-frame update currently live in
  `src/platform/platform_android.c` (`hta_android`, `android_main`). The
  player, vehicles, projectiles, vitals and other mechanics already have
  portable `src/engine/` modules. Read the live loop before deciding where
  shared session state, simulation timing and network messages belong.
- Two automated desktop clients now connect through one headless server and
  receive each other's position, orientation and action events. The desktop
  window reuses `hta_player_update` and `hta_actor`; Android has compiled
  Host LAN and Join LAN flows. Two-human and device runs remain the active
  verification target before adding server-side damage or vehicles.
- Preserve the asset boundary: each client imports its own Trial data. Do not
  send or bundle map assets. Use the existing `scripts/verify.sh` gate before
  publishing an APK for device testing.

### Known gaps worth fixing

| | |
|---|---|
| Dropped weapons | A weapon you swap off vanishes. Drawing one needs a second dynamic mesh holding each weapon model once. `HTA_GFX_MAX_DYNAMIC` was raised to 8 to leave room — six were already in use. |
| Items do not rotate | Halo spins powerups. Doing it means paying the full item upload every frame or splitting powerups into their own dynamic mesh. The latter. |
| Camouflage does nothing | It runs its timer. Nothing to hide from yet. |
| Picked-up weapons are full | `hta_ammo_init` runs on equip; a dropped weapon should carry what was left in it. |
| Motion tracker | Art and behaviour readable (`unhi` 620/724, `hud_globals` range and scale) but placement is not in the tag, and nothing moves to track. Deferred three times. |
| No hit sound on the body | `weapons\*\effects\impact cyborg shield` is in the cache and is the right thing to reach for. |
| The bot's rifle is hardcoded | Should be whatever it is carrying, once it carries anything. |
| Vehicle collision remains planar | Wall and jeep contacts rebound, deflect and apply yaw torque, but there is no full 3-D rigid body or flip. A truly too-narrow passage can still trap the Warthog; reverse or exit if safe. |
| Driving re-poses the whole fleet | About 1.5 ms/frame on the host, mostly rebuilding all 12 jeeps' shared collision grid. Eleven are parked. Per-vehicle grids is the fix if the phone shows a repeatable fps drop. |
| Vehicles take no damage | You cannot destroy or flip a Warthog, and it does not hurt what it hits. |
| Only 1 dynamic mesh slot left | The vehicle fleet took one; 7 of `HTA_GFX_MAX_DYNAMIC`'s 8 are now in use, and dropped weapons still want one. |

---

## How to read Halo's tags

This is the part that makes or breaks a session.

**1. Struct sizes must reconcile.** Walk a struct's fields in
`upstream/invader/src/tag/hek/definition/*.json`, add up the sizes, and check
the total against the declared `size`. If it matches, every offset before it is
trustworthy. If it does not, the walk drifted and you must probe instead.

**2. `"bounds": true` counts as TWO values.** Missing that is what put every
`Weapon` offset past `zoom magnification range` in the wrong place.
`"count": N` multiplies.

**3. Probe when the walk drifts.** Write a throwaway C program against
`libhta_engine.a`, scan the tag data for a dependency whose class is what you
expect, and print the offset. `Projectile`'s `impact damage` at **+548** and
`UnitHUDInterface`'s sounds at **+960** were both found this way, and both
disagree with a naive walk.

**4. Sanity-check the values against the world.** A number that reconciles can
still be the wrong field. The spent-casing radii were checked against real
cartridge sizes (9 mm, 7.62×51, 12-gauge, .50 BMG) and that is what revealed
every particle was being drawn at twice its tagged size.

**5. Velocities in Halo tags are per TICK (30/s).** Projectile initial and
final velocity, falling-damage velocities. Ranges and timers are not.

**6. One world unit is 3.048 m** (ten feet). The standing eye is 0.62 wu.
Comments in this codebase sometimes say "12.5 cm" where they mean 0.125 wu —
do not trust a unit in a comment, check the constant.

### Structs known to reconcile

Projectile 588 · Effect 64 · EffectEvent 68 · EffectPart 104 · EffectParticle
232 · Particle 356 · PointPhysics 64 · ShaderEnvironment 836 · ShaderModel 440
· ShaderTransparentGlass 480 · ShaderTransparentChicago 108 · Sound 164 ·
SoundLooping 84 · Font 156 · WeaponHUDInterface 380 · Globals 428 ·
Vehicle 1008 · GBXModel 232 · ModelCollisionGeometry 664 · DamageEffect 672 · Object 380 · Unit 752 ·
Dialogue 4112 · Scenario 1456 · ScenarioNetgameEquipment 144 · ItemCollection
92 · ItemCollectionPermutation 84 · ScenarioPlayerStartingProfile 104 ·
ScenarioStartingEquipment 204 · UnitHUDInterfaceHUDSound 56 · Equipment 944

### Offsets found by probing (the walk drifts to reach them)

| what | where |
|---|---|
| Projectile `impact damage` | **proj+548** (needler has none; scan for any damaging `jpt!`) |
| Projectile detonation responses | proj+576, 160 each, kind at +2 (reflect == 2) |
| Projectile `detonation timer starts` | proj+384 |
| UnitHUDInterface sounds | **unhi+960**, 56 each, `latched to` at +16 |
| `unhi` motion sensor background / foreground | 620 / 724 |
| ParticleSystem particle types | pctl+92, 128 each |
| — type radius / states / particle states | +44 / +104 (192 each) / +116 (376 each) |
| — state rate / duration | state+88 / state+32 |
| — pstate bitmap / radius mult / blend / colours | +48 / +128 / +226 / +96 and +112 |
| Equipment powerup type / grenade type / time / pickup sound | 776 / 778 / 780 / 784 |
| DamageEffect per-material multipliers | **jpt+512**, one float per MaterialType |
| Unit `melee damage` | 380 + 268 |
| Vehicle type / driving floats | **vehi+756** (u16; 1 == human jeep) / **vehi+760**, 8 floats: forward, reverse, accel, decel, left turn, right turn, wheel circumference, turn rate |
| Object model / animation graph / attachments | +40 / +56 / +320 (72 each) |

**MaterialType 21 is cyborg armour, 22 is cyborg energy shield.** Those two
are how much of a hit a player actually takes.

---

## Traps that have already cost a session

Each of these bit once and is now defended by a test. Search `JOURNAL.md` for
the full story.

- **Overlay animations (`type 1`) must not be played as ordinary clips.** They
  keyframe a handful of nodes; every other node takes the *animation's own
  defaults*, not the pose underneath. The body turns inside out. Use
  `hta_anim_animates` and compose. This has bitten **twice** — the needler's
  ammunition and the cyborg's flinch. **Check `anims[i].type` before playing
  anything new.**
- **`hta_anim_find` matches a SUBSTRING.** The cyborg has 254 animations and a
  dozen seated ones; plain `"idle"` finds `B-driver unarmed idle`, a body
  sitting in a Banshee, hanging in the air.
- **Fix shading before trusting animation.** The needler was "broken" through
  three animation rewrites; it was a `sgla` shader nothing handled.
- **A texture table must exist before anything interns into it.**
  `hta_model_append_skinned` fills `mesh.textures`, it does not create it.
- **`hta_submesh_init` exists because zero is a valid texture index.** A
  memset left `detail_tex = 0`, which multiplied every model by its own
  texture 0.
- **`add_elem` sets its fields one by one**, so a new field on `hta_hud_elem`
  is garbage on every other element until you initialise it there.
- **A tint alpha of zero means "no tint"** to the HUD draw loop, which then
  falls back to opaque white. A fade of nothing must collapse the quad.
- **Relink probe binaries after `cmake --build`.** A stale probe against the
  old static library gives confident wrong answers. This has recurred.
- **Read one-shot vitals flags BEFORE `hta_vitals_update`**, which clears them.
- **Blast deaths land a frame late** because projectiles update after vitals.
  That is invisible and fine.
- **A corpse must stop no bullets**, or a dead body soaks the magazine meant
  for the next one.
- **`hta_collision_build` memsets the grid**, which clears the optional
  `extra` link to a moving-object grid. Re-point it after any rebuild, or
  vehicles silently stop being solid.
- **Vehicle physics must walk the STATIC grid** (`extra = NULL`). Given the
  whole world, every jeep collides with its own hull and cannot move.
- **Dynamic meshes write one vertex slot per in-flight frame.** Uploading a
  change once leaves stale geometry in the other slots. The count belongs to
  the swapchain, so re-upload for several frames.

---

## Numbers that are OURS, not the tags'

Every one of these is invented because no tag carries it. If something feels
wrong, this list is the first place to look — they are all one constant.

| constant | value | what it is |
|---|---|---|
| `HTA_HUD_PHONE_SCALE` | 1.75 | HUD size on a phone |
| `HTA_HUD_WEAPON_SCALE` | 0.5 | the weapon block |
| `HTA_SOUND_NEAR` / `_FAR` | 3 / 60 wu | distance attenuation |
| `HTA_GRENADE_THROW` | 9.0 wu/s | throw speed |
| `PROJ_BOUNCE` | 0.35 | projectile restitution |
| `PART_REST_SPEED` | 0.15 wu/s | below this a bounced particle has landed |
| `HTA_PART_AREA` | 120 sq wu | live particle quad budget — **the GPU knob** |
| `HTA_PART_RECUR` | 0.25 s | how often an effect is assumed to recur |
| `HTA_RESPAWN_DELAY` | 5 s | player respawn (gametype value, not in the map) |
| `HTA_DEATH_*` | — | death camera pull-back, fade timings |
| `HTA_VITALS_LOW` | 0.25 | when "low shield"/"low health" sounds start |
| `HTA_OVERSHIELD_MULT` | 3.0 | how much overshield gives (tag says how long only) |
| `HTA_PICKUP_REACH` | 0.5 wu | pickup radius |
| `HTA_PICKUP_LIFT` | 0.06 wu | how far an item floats off its placement |
| `HTA_ITEM_RESPAWN_DEFAULT` | 15 s | when neither placement nor collection says |
| `HTA_MELEE_REACH` | 0.5 wu | how far a swing reaches |
| `HTA_VM_KEY_FRACTION` | 0.35 | when a clip "does its thing" if `key frame` is 0 |
| `HTA_BOT_RESPAWN` | 5 s | how long a body lies there |
| `HTA_ITEMS_UPLOAD_FRAMES` | 8 | ≥ any swapchain image count |
| `HTA_VEHICLE_ENTER_REACH` | 0.9 wu | how close to a driver's seat DRIVE appears |
| `HTA_VEHICLE_EXIT_SPEED` | 0.5 wu/s | below this a jeep counts as stopped, for entering and exiting |
| `HTA_VEHICLE_ADHESION_SPEED_FRACTION` | 0.25 of tagged forward speed | below this, the `phys` ground depth can keep the chassis in contact over rough terrain; above it, the jeep can fly off a crest |
| `HTA_VEHICLE_SETTLE_TIME` | 0.5 s | time an undriven jeep keeps posing its chassis on terrain before sleeping |
| `HTA_VEHICLE_RESTITUTION` | 0.20 | normal-velocity rebound on wall and jeep contacts; no crash restitution is tagged |
| `HTA_VEHICLE_YAW_DAMP` | 2.0 /s | decay of collision-induced yaw velocity; no tagged yaw damping law |
| `HTA_VEHICLE_STEP` | 1/120 s | fixed physics substep, so frame rate cannot change handling |
| `HTA_VEHICLE_CLEARANCE` | 0.04 wu | how far a mass point may be pushed before it counts as blocked |
| `HTA_VEHICLE_MAX_SLOPE` | 0.75 rad | cap on the pitch/roll the wheels may pose the body to (43°) |
| `HTA_VEHICLE_CAMERA_BACK` | 2.5 wu | chase camera distance, pulled in by terrain |
| `HTA_VEHICLE_CAMERA_UP` | 0.7 wu | chase camera height above the hull |

---

## Tools

```
# look at the first-person view without a device
./build-host/htaview $HTA_MAP --fp idle   --shots 2 --out /tmp/fp
./build-host/htaview $HTA_MAP --weapon "rocket launcher" --fly 0.05
./build-host/htaview $HTA_MAP --drive 4 --steer 0.3   # drive a jeep, then render it

# chase a "stuck here" report: the HUD readout gives x y z
./build-host/htaprobe $HTA_MAP --at 96.57 -155.72 --z 0.81   # why it pushes
./build-host/htaprobe $HTA_MAP --find 0.81 --near 96 -155    # if a digit is unreadable
./build-host/htaview  $HTA_MAP --eye 96.57 -155.72 1.43 --yaw 180

# tags and sounds
./build-host/htainfo  $HTA_MAP
./build-host/htasound $HTA_MAP --dump <tag>
```

`htaprobe --at` prints the whole vertical column, headroom, standing and
crouching push, and every nearby triangle with a verdict. **Probe from the
player's own z** — a ground query from the sky finds the roof. `--eye` takes
the eye position, so add the 0.62 standing eye height to their feet z.

**Writing a throwaway probe** is the normal way to answer a tag question:

```c
cc -O1 -I src probe.c build-host/libhta_engine.a -lm -o probe
```

Put them in the session scratchpad, not the repo. Relink after every rebuild.

---

## Key files

| Area | Path |
|---|---|
| **Tag layouts** | `upstream/invader/src/tag/hek/definition/*.json` |
| Cache, tag lookup, reflexives | `src/asset/cache.c`, `src/asset/bsp.c` |
| Bitmaps, shaders, detail maps, tints | `src/asset/bitmap.c` |
| Models, skinning, markers, LOD | `src/asset/model.c` |
| Animation graphs, transform algebra | `src/asset/anim.c` |
| Weapons, magazines, triggers | `src/asset/weapon.c` |
| Effects, particles, damage, attachments | `src/asset/effect.c` |
| Sounds (`snd!`, Xbox ADPCM) | `src/asset/sound.c` |
| Dialogue (`udlg`) | `src/asset/dialogue.c` |
| Items, collections, starting loadout | `src/asset/items.c` |
| Biped physics | `src/asset/biped.c` |
| HUD font digits | `src/asset/font.c` |
| Player movement, collision grid, **ray query** | `src/engine/player.c` |
| Drivable vehicles, chassis, chase camera | `src/engine/vehicle.c` |
| First-person weapon | `src/engine/viewmodel.c` |
| World-space skinned character | `src/engine/actor.c` |
| A body that can be shot | `src/engine/bot.c` |
| Hitscan, scorch marks | `src/engine/gun.c` |
| Projectiles in flight | `src/engine/projectile.c` |
| Particles | `src/engine/particle.c` |
| Health and shields | `src/engine/vitals.c` |
| Pickups | `src/engine/pickup.c` |
| Magazine and reload | `src/engine/ammo.c` |
| Screen HUD | `src/engine/hud.c` |
| Voice mixer | `src/engine/audio.c` |
| Vulkan renderer | `src/gfx/gfx_vulkan.c`, `shaders/*` |
| Android glue, game loop, JNI | `src/platform/platform_android.c` |
| Touch HUD, DBG pad | `android/.../GameActivity.java` |
| Map picker | `android/.../SetupActivity.java` |
| Tests | `tests/test_*.c` — most take `$HTA_MAP` |

---

## Other docs

| | |
|---|---|
| `docs/JOURNAL.md` | every session, newest first. The *why*. Search by symptom. |
| `docs/BUILD_ENVIRONMENT.md` | SDK/NDK, toolchain, how the build is wired |
| `docs/BLOOD_GULCH_ASSETS.md` | what is in the map |
| `docs/INVADER_ASSET_PIPELINE.md` | how Invader's definitions are used |
| `docs/ANDROID_PORT_INVESTIGATION.md` | the original feasibility work |
| `docs/PROGRESS.md` | early milestone log |

---

## Working notes for the next agent

- **The owner tests every build.** Small, shippable slices beat big ones.
  Publish, say what to look at, and let the next screenshot decide.
- **Say which numbers are invented.** The owner has repeatedly, correctly,
  pushed back on things that felt wrong, and every time the answer was either
  a tag we were not reading or a constant we had made up. Flag them in the
  release notes.
- **When something looks wrong, suspect the data path before the art.** The
  body that "looked ugly" was unlit and unarmed; the model was fine.
- **Measure before optimising.** The particle lag was blamed on fill twice and
  was CPU raycasts both times. The 120 fps idle reading in a screenshot was
  the proof.
- **Write the test that would have caught it.** Every trap above has one.
