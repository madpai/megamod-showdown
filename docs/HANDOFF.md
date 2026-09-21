# Session handoff — Halo Trial Android

**Date:** 2026-09-20
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

## Pickups (2026-09-21)

Blood Gulch's own item layout, running. Every position, facing, respawn time
and weighted choice is the scenario's.

### Where it lives

**Not** in the scenery: Halo keeps items in the scenario as **`netgame
equipment`**, at **+900**, 144 bytes each. `Scenario` reconciles at 1456,
`ScenarioNetgameEquipment` at 144, `ItemCollection` at 92 and
`ItemCollectionPermutation` at 84 -- all four, which is why these offsets can
be trusted without probing.

| | offset |
| --- | --- |
| netgame equipment | Scenario +900, stride 144 |
| spawn time (int16 s, 0 = use the collection's) | +14 |
| position | +64 |
| facing | +76 |
| item collection | +80 (id at +92) |
| collection permutations | ItemCollection +0, stride 84 |
| collection default spawn time | +12 |
| permutation weight / item | +32 / +36 |

Blood Gulch places **37**: 17 weapons, 16 grenades, 2 health packs, the
overshield and the active camouflage. Respawns run 15 s to 180 s. The
placement's own time wins over the collection's -- the rocket launcher is
written 90 s over its collection's 120.

### The equipment says what it is; its PATH lies

`Equipment` sits at **+776** (Object 380 + Item 396), and declares 944 =
776 + 168:

| | offset |
| --- | --- |
| powerup type | 776 |
| grenade type | 778 |
| powerup time | 780 |
| pickup sound | 784 |

Powerup type comes out exactly right -- camouflage 3, overshield 2, health 5,
grenade 6, and 0 for the ammo powerups the Trial never places -- which is the
check that 776 is correct.

**Do not classify these by path.** In Bungie's own tags the overshield's
model is `powerups\active camoflage\active camoflage` and the
camouflage's is the overshield's: they are swapped. Anything keying off
names hands out the wrong powerup. (The pickup sounds are not swapped:
`pickup_dbl_shield` and `pickup_health` are right. An earlier probe said
otherwise and was wrong -- a static return buffer aliasing between two
printf arguments.)

Powerup times are the tag's: overshield **60 s**, camouflage **45 s**.

### The runtime

`src/engine/pickup.c`, portable and testable: each placement holds an item
drawn from its collection's weights, hands it over when you walk onto it,
counts down, and **draws afresh**. That last part is the pedestal in the
middle of the map, which is overshield or camouflage fifty-fifty and rolls
again every time -- measured at 98/102 over 200 respawns.

Halo takes grenades, health and powerups as you **walk over** them and makes
you **ask** for a weapon; the difference is between topping up and losing the
gun you wanted. SWAP is the ask: on a weapon it picks it up, off one it
cycles as before, and the new weapon takes the roster slot it replaces.

### Drawing them without paying for it every frame

23,041 vertices across 37 items. Posing that every frame would be **900 KB of
upload a frame to keep thirty-seven stationary objects stationary**.

`dyn[].vertices == NULL` skips the copy, so items are only written when
something is actually taken or comes back. The catch: the dynamic path writes
**one vertex slot per in-flight frame** and the count is the swapchain's
image count, which the platform does not know -- so a change is re-uploaded
for `HTA_ITEMS_UPLOAD_FRAMES` (8) frames, not one. Uploading once would leave
stale geometry in the other slots, visible as an item flickering back.

`HTA_GFX_MAX_DYNAMIC` went 4 -> 6: projectiles, grenades, particles, the
corpse and now the items.

### Ours, not the tags'

- `HTA_PICKUP_REACH` 0.5 wu (about 1.5 m) -- no tag carries a pickup radius.
- `HTA_PICKUP_LIFT` 0.06 wu -- the scenario's z is the ground the item rests
  on, and drawing exactly on it sinks the model halfway in.
- `HTA_OVERSHIELD_MULT` 3.0 -- the tag says how long it lasts, not how much
  it gives. It bleeds down over the powerup time rather than vanishing.
- `HTA_ITEM_RESPAWN_DEFAULT` 15 s, where neither the placement nor the
  collection says (the base weapons are written that way).

### Not done

- **Items do not spin.** Halo rotates powerups where they lie. Doing it means
  either paying the full upload every frame or splitting the powerups into
  their own dynamic mesh -- the latter is the right answer and is the reason
  `HTA_GFX_MAX_DYNAMIC` has headroom.
- **Camouflage does nothing** beyond running its timer. With no other player
  and no third-person view of yourself, there is nothing for it to hide.
- Weapons picked up arrive with a full magazine, because `hta_ammo_init` is
  what equipping runs. A dropped weapon should carry what was left in it.

---

## The shield has a voice (2026-09-21)

Asked for: "isn't there supposed to be a sound effect when your shield is
recharging?" There is, and the Trial ships all of it.

Halo hangs HUD sounds off the **`unhi`**, not off the biped. The reflexive is
`UnitHUDInterfaceHUDSound`, **56 bytes** an entry (the definition's own size),
at **+960** -- a definition walk lands on 928 and drifts by 32, so the offset
is probed. Each entry is a sound plus a `latched to` bitfield at +16 naming
the condition. `ui\hud\cyborg_mp` fills all five:

| bit | condition | tag | class |
| --- | --- | --- | --- |
| 0x01 | shield recharging | `sound\sfx\ui\shield_charge` | lsnd |
| 0x02 | shield damaged | `sound\sfx\ui\shield_hit` | snd! |
| 0x04 | shield low | `sound\sfx\ui\shield_low` | lsnd |
| 0x08 | shield empty | `sound\sfx\ui\shield_depleted` | lsnd |
| 0x10 | health low | `sound\sfx\ui\health_low_heart` | lsnd |

`hta_unit_hud_sound(c, latched_to, &looping)` in hud.c.

**Four of the five are `lsnd`, and nothing downstream can play one** -- a
mixer clip comes from a `snd!`. `hta_loop_sound_track` is the step between,
factored out of the object-attachment reader; it passes a `snd!` straight
through, because the caller has a tag id and does not always know which it
is. Without it the shield stays silent and `bank_get` logs a decode failure
that looks like a broken sound rather than a wrong class.

### What drives them

The recharge hum's condition is **the tag's own and nothing of ours**: the
shield is growing back exactly when `since_damage >= recharge_delay` and it
is not yet full, which is the same test `hta_vitals_update` uses to grow it.

The one-shots have to be read **before** `hta_vitals_update`, which clears
`took_damage` and `shield_broke`.

`HTA_VITALS_LOW` (0.25) **is invented.** Halo has no threshold for "low" in
any tag -- the HUD flashes and the heartbeat starts on a hardcoded fraction.

Each loop has its own id, so the heartbeat can run under the recharge hum,
which is right. All three stop on death.

---

## The rocket's poof, and watching your own body (2026-09-20)

Two reports: "rocket explosion seems non-existent, just a small poof of smoke
where it lands but a lot of damage", and "you would ragdoll in the original
with some sort of third person view of your ragdoll".

### The area budget was starving the explosion

The rocket's detonation is `weapons\rocket launcher\effects\rocket
explosion`: **40 to 60 smoke puffs at radius 0.85 to 1.75 wu** -- up to five
metres across -- plus a 2.0 wu lens flare. The area budget was giving that
type **2 slots**. Hence a poof.

The budget was sized at 12 sq wu while the frame rate was believed to be a
fill problem. It was not, it was the collision ray, and the radius fix in the
same build had already quartered real fill. So:

- **A complete burst is now the FLOOR.** `type[t].burst` is what one firing
  of the effect needs, and the budget may never cut below it. The budget's
  job is deciding how many bursts OVERLAP; it does not get to decide that an
  explosion is four puffs instead of fifty-one.
- `HTA_PART_AREA` is **120** sq wu. One rocket burst is about 80 of those.
- A type's burst is the **SUM** of the emits using it, not the largest. The
  plasma impact has six particle entries over four types and they all go off
  together; taking the largest left it two sprites short of the tag.

The rocket now puts **58** particles up per detonation where it managed about
four. `test_particle.c` pins it: every emit must have room for its own
`count_max`, and one burst must really spawn at least the tag's minimum.

Two of the rocket's four impact entries are `create in` = **water**. They are
correctly filtered; do not go looking for them.

### A body to look at

There has never been a character in the world -- the only body was the
player's and you never saw it. `src/engine/actor.c` is one:
`hta_actor_load/play/play_death/update/place`. Most of it was already
shared -- `hta_model_append_skinned` binds any `mod2` to any animation graph
by node name, and the skinning loop is the viewmodel's. What is new is that
the result lands somewhere in the WORLD, by position and facing, rather than
in view space.

The cyborg: `characters\cyborg\cyborg`, **19 nodes, 254 animations**, 2256
vertices, 4 submeshes, 3 textures.

**There is no animation called "die".** Halo names them by how hard the blow
was and where it came from: `s-kill front gut`, `h-kill front head`,
`s-kill back gut` and so on, plus `stand airborne-dead` and `stand
landing-dead`. `hta_actor_play_death` picks among them.

Measured: the body starts at **2.06 m** upright and collapses to **0.63 m**
over 44 frames (1.4 s), then holds its last frame. Holding matters -- these
clips do not loop back to standing, and letting them would stand the corpse
up again.

**The texture table has to exist before anything interns into it.**
`hta_model_append_skinned` fills `mesh.textures`, it does not create it, so
the cyborg first loaded with four submeshes and no art at all. The viewmodel
calloc's 256 entries for exactly this reason.

### The death camera

The camera leaves your head, pulls out along the way the body is facing over
1.2 s, and looks at its chest -- about 6 m back and 2.5 m up. A ray from the
body to where the camera wants to be pulls it in short of any wall, which
costs nothing now the ray walks the grid.

**The fade moved to the END.** Fading out as you die would swallow the body
half a second after showing it. It is clear for the whole five seconds and
black only for the last 0.8, which covers the respawn.

Not a ragdoll: it is the last frame of a kill animation, held. A real ragdoll
is a physics solver we do not have. The camera keeps a little more distance
than Halo's for that reason.

If there is no body -- the biped failed to load -- the old behaviour remains:
the view sinks and tips forward.

---

## Dying (2026-09-20)

`hta_vitals` has tracked health, shields, fall damage and blast damage for a
while, and at zero **nothing happened**. You could not lose. That is now a
death, five seconds of black, and a respawn somewhere else.

### What the tags supply, and the one number they do not

`udlg` (Dialogue) reconciles at **4112**. Blood Gulch carries exactly one,
`sound\dialog\chief\chief`, so finding it by class is unambiguous rather
than a guess -- a campaign map carries one per character and would want the
actor variant instead. Slots, all TagDependency so the `snd!` is at +12:

| | offset | Blood Gulch |
| --- | --- | --- |
| pain body minor / major | 112 / 128 | empty |
| pain shield / falling | 144 / 160 | empty |
| scream fear / pain | 176 / 192 | empty |
| **death quiet** | **240** | `deathquiet` |
| **death violent** | **256** | `deathviolent` |
| death falling | 272 | `deathviolent` |
| death agonizing | 288 | empty |
| death instant | 304 | `deathviolent` |

Four of eleven filled. The empty ones are correct -- the Chief does not grunt
in Halo CE -- and an empty slot must read as *nothing*, not as a tag id of
zero being valid.

**`HTA_RESPAWN_DELAY` (5 s) is invented.** Respawn time is a GAMETYPE value
and no gametype ships inside a map, so it is the only number here that is not
the Trial's. Halo CE's default is five seconds; say so if it should be
shorter, it is one constant.

### Choosing where to come back

`hta_scenario_spawn_pick` takes the 32 spawn points the scenario already
gives us and the place you died, and picks at random among the furthest
half. Two reasons, both needed:

- Furthest, because with nobody else in the map the only thing known to be
  dangerous is what just killed you -- and without it, falling into a hole
  respawns you next to the hole, forever.
- Random among them, because Blood Gulch is symmetrical and "the furthest"
  is otherwise the same rock every single time.

### Fading out, and a white flash I nearly shipped

The HUD shader already had a per-element tint and a mask mode, so the fade is
one white texel stretched to clip space, tinted black, added **last** so it
draws over everything. `hta_hud_set_fade(h, alpha)`.

The trap: the draw loop reads a tint alpha of **zero** as "this element has
no tint" and falls back to **opaque white**. A fade of nothing would have
flashed the screen white rather than shown nothing. `hta_hud_layout`
collapses the quad instead, which also costs no fill. `test_hud.c` checks
exactly that, plus that the element is last and covers the screen at any
aspect ratio.

`hta_hud_elem` gained `fullscreen`, and `add_elem` had to be taught to
initialise it -- it sets its fields one by one rather than memsetting, so a
new field is garbage on every other element until it is.

### The sequence

Die -> the dialogue line, the camera sinks `HTA_DEATH_EYE_DROP` and tips
forward over 1.5 s while the screen goes black, input is ignored and the
weapon leaves the screen. Buttons pressed while dead are **consumed, not
queued**, or every one of them fires at once on respawn. At 5 s: a new spawn,
full health and shield, a full magazine and reserve, grenades back, unzoomed,
idle pose, and 0.6 s fading back in.

The body still falls while dead -- `hta_player_update` runs with a blank
input -- so dying on a slope still slides you down it.

`vitals.died` is sticky until `hta_vitals_reset`, which is what makes the
platform's `!dead` guard a one-shot. Blast deaths land a frame late because
projectiles update after vitals; that is invisible.

### What else the Trial actually ships (audited, not guessed)

- **Announcer: all of it, in bloodgulch.map.** 39 lines under
  `sound\dialog\multiplayer1\` -- slayer, ctf, king of the hill, oddball,
  race, double kill, triple kill, killtacular, killing spree, running riot,
  play ball, game over, the flag and hill calls, the vehicle names. Nearly
  all of it needs a game mode or a second player to mean anything.
- **Music: none, and that is correct.** No `sound\music\*` in bloodgulch at
  all. The campaign map has it (`b30_01/02/03`, `halo_orig`, `iron_novox`,
  `spooky2`) but Halo CE multiplayer maps carry no score.
- **Menus: the entire shell is data.** ui.map is 811 `DeLa`
  (ui_widget_definition) tags, 168 `ustr` string lists, 6 fonts, 222 bitmaps,
  29 sounds, a `vcky` virtual keyboard and an `mply` map list. bloodgulch
  itself carries another 324 `DeLa` and 54 `ustr`. A Halo-authentic main menu
  is a widget-tree interpreter, not asset work.
- Also sitting unused: `itmc` 14 (item collections -- what spawns where),
  `eqip` 14 (overshield, camo, health, ammo), `snde` 1 (sound environment,
  i.e. reverb), `vehi` 6.

---

## The frame rate was never fill: it was raycasts (2026-09-20)

The previous build cut particle fill by roughly ten times and the phone still
read **14 fps** while shooting a wall. It was the wrong suspect. Idle is 120
fps; the cost is not on the GPU at all.

### hta_collision_ray tested every triangle in the map

`hta_collision_ray_material` brute-forced all 5,940 collision triangles, every
call. That is fine for the handful of rays the player casts a frame and
ruinous at sixty: **a colliding particle asks for a ray every frame it is
alive**, spent brass collides, and brass lives thirty seconds. A floor
littered with casings was running millions of triangle tests a frame.

The fix is not new machinery -- the XY uniform grid has been there all along,
built by `hta_collision_build` and used by the height query. The ray walks it
now, 2D DDA, stopping as soon as the nearest hit so far beats anything the
next cell could hold.

Measured on the real map, 64 colliding particles:

| | per frame |
| --- | --- |
| every triangle (what it did) | **1.867 ms** |
| grid walk | **0.057 ms** |

33x, and that is a desktop; the phone is the one that was drowning. A 60 wu
bullet ray is 0.0036 ms.

**`test_player.c` checks the grid walk against a brute-force reference** on
4000 random rays, short and long -- 861 of them hit, zero disagree. Nothing
else in the suite would have noticed a wrong answer here, and every bullet,
bounce and scorch mark depends on it.

### Brass kept asking after it had landed

Gravity adds 0.057 wu/s back every frame, so a casing bouncing at 0.35
elasticity never truly stops -- it shivers against the floor for the rest of
its thirty seconds, raycasting all the way. A bounce that leaves it under
**0.15 wu/s** (about half a metre a second, two frames' worth of falling) now
marks it `at_rest`: no physics, no ray, still drawn and still ageing out. It
goes quiet after about 2.2 s.

`PART_REST_SPEED` cannot be made arbitrarily small. Anything below one
frame's gravity is never reached.

### Three weapons went silent because a table was full

Unrelated, reported in the same message: the sniper, needler and flamethrower
lost their sound after swapping through the roster.

A clip is one **permutation**, and Halo's sounds carry several -- the rifle's
shot has 4, its impacts 5 to 7 each, the plasma rifle's shot 5. One weapon's
set is twenty-odd clips. `HTA_AUDIO_MAX_CLIPS` was **64**, so the table filled
after about two weapons, `hta_audio_add_clip` started returning
`HTA_AUDIO_NO_CLIP`, `bank_get` gave up, and everything later was silent. The
three that broke are simply the ones late in the roster.

The whole Trial weapon set is **21 tags, 55 clips, 2.8 MB** of PCM. Ceilings
are now 256 clips and 64 bank entries, and **both paths log loudly when they
fill** -- this looked like a decoder bug for a whole build. `test_weapons.c`
counts the roster's demand from the tags and fails if it stops fitting.

### If it is still slow

In order of what to suspect:

1. Something else calling `hta_collision_ray` per-entity per-frame.
2. `HTA_PART_AREA` (12 sq wu) -- the fill budget, genuinely a GPU knob.
3. The dynamic mesh upload, which is `slot_count` quads a frame whether or
   not they are alive.

---

## 18 fps, and brass that hung in the air (2026-09-20)

The owner reported lag and "bullet casings fall in slow motion and are kind
of weird". Both were real, and the frame rate was **entirely** particles:
standing still with nothing going off the phone reads **120 fps**, and firing
at a wall a few metres away took it to **18**.

### Brass floated because drag was read wrong

`air friction` is a FORCE. What slows a particle is force over **mass**, and
the casing's `stones` physics reads friction 900 against a mass of 284672 --
**0.003 a second**, which is free fall, which is what brass does. Reading the
friction alone and dividing it by an invented constant of 125 gave it 7.2 a
second: a terminal velocity of about 1.6 wu/s, and casings that drifted
across the view like ash.

Mass is at **pphy+4**. It is `cache_only` -- derived from `density` by the tag
compiler -- and it is sitting in the cache already. Smoke comes out at 1.5/s
this way, which is almost exactly the 1.6 the 125 divisor had been hand-tuned
to produce for smoke *alone*; the constant was right for one case and wrong
for the other, which is what a missing divisor by mass looks like.

A casing now falls 1.5 wu in 0.92 s against a free-fall 0.94.

### Every particle was drawn at twice its tagged size

Halo's `radius` is the sprite's **size**, and the quad spanned `-radius` to
`+radius`. That is double the width and **four times the fill**. The spent
brass settles it four ways over -- these are the tagged radii at the two
possible conventions against the real round:

| casing | tag | as half-extent | as size | real |
| --- | --- | --- | --- | --- |
| pistol | 0.010 wu | 6.1 cm | **3.0 cm** | 9 mm, 2.5 cm |
| assault rifle | 0.015 | 9.1 | **4.6** | 7.62x51, 5.1 cm |
| shotgun | 0.023 | 14.0 | **7.0** | 12-gauge, 7.0 cm |
| sniper | 0.037 | 22.6 | **11.3** | .50 BMG, 13 cm |

Corners are `+/-0.5` now, not `+/-1`.

### The pool is budgeted by AREA, and I had made it worse

`HTA_PART_PER_TYPE` had gone 16 -> 64 for the flamethrower's jet in the build
before this one. That quadrupled what every *other* effect could keep alive,
and the plasma rifle's impact is **57 quads of additive blending per hit** at
up to 10 hits a second. 768 live quads, each twice the size it should be, is
the 18 fps.

Count was never the thing that hurt. **Fill** was. Brass is 4 cm and free at
any depth; a rocket's smoke puff is metres across. So each type now declares
what the tags ask of it -- `count_max * lifespan / HTA_PART_RECUR`, or
`rate * lifespan` for an emitter -- and slots are spent **cheapest first**
until the live quads add up to `HTA_PART_AREA` (12) square world units.
Cheapest-first matters: scaling everyone by one factor charged the
flamethrower's 6 cm jet particles for the explosion cloud sharing its load.

`type[t].first_slot` / `type[t].slots` is where a type's particles live.
**There is no uniform stride any more** -- `HTA_PART_PER_TYPE` is only a
ceiling, and slot arithmetic that assumes otherwise is a bug.

Where it lands on the real map: 2.7 to 11.7 sq wu per weapon, except the
rocket launcher at 36 -- its fireball alone is 12 sq wu a quad, so that one
is the floor rather than a choice. **`HTA_PART_AREA` is the knob** if the
phone still struggles.

Two traps hit while writing this:

- The floor can overspend the budget, so `left` goes negative; casting that
  to unsigned handed the rocket launcher 446 slots instead of 24.
- A type whose single quad is over a quarter of the budget gets a floor of
  **one**, not two.

### A full pool has to recycle

Spent brass lives **thirty seconds**. A pool that refuses to spawn when it is
full would eject a few shells and then go quiet for half a minute. A burst now
takes the oldest slot when there is no free one -- it drops what was about to
expire anyway, and the gun keeps ejecting.

### New tests

- brass is barely slowed by air and lands in about free-fall time
- a quad is no wider than its tagged radius, printed in cm beside the round
- the pool stays inside the geometry ceiling and the area budget (or is
  floored trying, which only the rocket is)
- a full pool keeps throwing

---

## Every particle was white (2026-09-20)

The needler threw white sparks. So did the plasma pistol, and so did the
flamethrower. None of that art is coloured: **Halo ships white sprites and
puts the colour on the effect**, as `tint lower bound` / `tint upper bound`
on each `EffectParticle` (+176 and +192, ColorARGB so **alpha first**).
EffectParticle reconciles at 232, so these are definition-derived rather than
probed, and the values confirm it -- the needler's shards are 0.98, 0.18,
1.00, the plasma pistol's bolt is green, the flamethrower carries a blue
pilot light and an orange flame.

We take the midpoint of the two bounds. All zeros means "no tint", **not**
black: read literally it paints the sprite out of existence.

### Tinting without touching the shader

The push constants are full -- `mat4` plus four `vec4` is exactly the 128
bytes Vulkan guarantees -- and a per-vertex colour would have widened the
format for the BSP as well, which is by far the biggest mesh. So the tint is
**baked into the decoded pixels** and becomes part of the texture cache key:
`hta_mesh_intern_bitmap_tinted(mesh, c, bitmaps, id, index, 0xRRGGBB)`. The
plain call is that with 0xFFFFFF and shares its slot. Alpha is untouched: it
is the sprite's shape, and on the additive path its brightness.

The muzzle flash goes through the same call, which is what makes the
needler's flash magenta and the plasma pistol's green.

**`hta_tint_pack` quantises to EIGHT levels a channel, not 256.** That is not
sloppiness, it is the cache key doing its job: the plasma pistol tints its
six particles F5FF2B, F5FF32, F4FF21, F4FF20 and F4FF28 -- one green written
five ways -- and at full precision each took its own particle type and its
own copy of the sheet.

### The pool is now divided, not multiplied

`HTA_PART_MAX` used to be `TYPES * PER_TYPE`. Those were the same thing while
every recipe was a burst, and stopped being the same thing twice in one
session: tinting splits each colour of one sheet into its own type (the
rocket launcher went 8 -> 17 types against a budget of 12), and the
flamethrower's jet wants 60 particles of ONE type alive at once.

So the pool is a fixed **768 quads shared out at build time**:
`per_type = 768 / type_count`, clamped to 8..64. The flamethrower's two types
get 64 each; the rocket launcher's twelve (on Blood Gulch's four materials)
get 64 each as well; a 24-type load would get 32. `hta_particles.per_type` is
what the slot arithmetic uses -- `HTA_PART_PER_TYPE` is only the ceiling now,
so **do not go back to using it for slot maths**.

Worst case on the real map is the rocket launcher at 12 types / 6 recipes,
against budgets of 24 and 16.

### New tests

- some particles carry a colour and some are meant to stay white
- the needler is magenta and the plasma pistol is green **by channel**, which
  is what catches an ARGB/RGBA mix-up that a merely-non-white tint would not
- near-identical tints quantise to the same key; white packs to white;
  out-of-range clamps

---

## The flamethrower sprays a particle system (2026-09-20)

The flamethrower had a flame that was one muzzle-flash quad and a handful of
projectiles. The real jet is a **`pctl` (particle_system)** hung off the
weapon object's `spawn fire` marker -- the same attachment list that already
gave us its looping roar, which is on `primary trigger`.

### Reading a `pctl`

The definition walk drifts a couple of bytes in this family, so these were
found by probing the Trial's own tags and confirming the strings land where
they should:

| what | offset | stride |
| --- | --- | --- |
| ParticleSystem particle types | +92 | 128 |
| type radius | type+44 | |
| type states | type+104 | 192 |
| state duration (float bounds) | state+32 | |
| state **particles a second** | state+88 | |
| type particle states | type+116 | 376 |
| pstate bitmap | pstate+48 | |
| pstate radius multiplier | pstate+128 | |
| pstate blend | pstate+226 | |

`weapons\flamethrower\effects\fp_defoliant3` reads as:

```
type 'flames'  radius 0.300
  state 'life'  60.0/s  duration 1.00..1.00
  particle states:
    invisible      radmul 0.100  add
    initial flame  radmul 0.100  add   r1.00 g0.75 b0.39
    roaring fire   radmul 0.400  add   r1.00 g0.84 b0.62
    smoke death    radmul 0.400  alpha a0.00
  bitmap weapons\flamethrower\bitmaps\cloud fire
```

The particle states are a life *curve* -- an ember that starts small and
bright, swells, then dies to smoke. We do not run the curve; we take the
range it spans (radius 0.030..0.120 here, from 0.300 x 0.100..0.400) and the
first bitmap and blend, which is one additive sprite growing over a second.
That is what the jet looks like from behind the gun.

### Emitting is rate-based, and that is the point

`hta_particles_emit` accumulates `rate * dt` and spawns whole particles out of
it. Bursting per shot would have given the jet the flamethrower's 0.1 s round
interval as a visible stutter -- the weapon fires ten times a second and the
flame is continuous. The accumulator is clamped to the pool size, so a dropped
frame cannot dump a second of flame into one frame.

It emits from `vm.flash_pos`, which the viewmodel now stores when it poses the
muzzle flash. That is not a coincidence or a convenient stand-in: the
flamethrower's first-person flash **is** on `spawn fire` (33 cm down the
barrel), because `setup_flash` already falls back to any marker for exactly
this weapon. Every other weapon's flash is on `primary trigger`; probe with
the flash-marker dump if that is ever in doubt.

### The pool was too small for anything continuous

`HTA_PART_PER_TYPE` was **16**, sized for a burst: an explosion throws a dozen
and they are gone inside a second. A jet asking for 60 a second that live a
second each got a dotted line. It is **64** now -- 768 quads of dynamic
geometry across every type at once, which is nothing -- and the test asserts
the count after half a second matches the tag's rate rather than the pool's
ceiling. That assertion is the one that catches this regressing.

### New

- `hta_object_attachment(c, object, marker, class)` -- generalised out of
  `hta_object_loop_sound`, which was the same walk with `lsnd` hardcoded.
- `hta_particles_add_system(p, c, bitmaps, pctl, speed)` and
  `hta_particles_emit(p, recipe, origin, dir, dt)`; `hta_particle_recipe`
  gained `rate` and `accum`.
- `hta_viewmodel.flash_pos` -- where the flash marker sits this frame.
- `test_particle.c` covers the emitter end to end: it builds, it is additive,
  it spawns nothing in zero time, it sprays at the tagged rate, it travels
  down the barrel line, and a 10-second frame cannot flood the pool.

**Remember to `hta_particles_build` before emitting.** Emit and burst both do
nothing without geometry, which cost a confusing "0 alive" while the same
code in a probe worked.

### Still not done

**The motion tracker.** The art and the behaviour are both readable -- `unhi`
motion sensor background at **620**, foreground at **724** (found
empirically), and `hud_globals` gives range, velocity sensitivity and scale --
but the single `unhi` anchor is 1 (top right) with 0,0 offsets, so where it
goes is not in the tag, and there is nothing in the world to track yet.
Deferred on purpose, twice now.

---

## Shield and health, and the spread question (2026-09-19)

### Should the crosshair react to firing? No -- the shots should

The owner asked. The tag answers it: the crosshair overlay has fields for
offset, scale, colour, flash and sprite frame, and **nothing tied to firing
error**. Halo does not bloom the reticle. It widens the cone the round goes
down, and the trigger's error fields had been parsed and ignored since the
weapon tag landed.

The Trial's assault rifle spreads **2.00 to 6.50 degrees**, blooming over
0.60 s of fire and settling over 1.00 s -- nine rounds at its 15/s to reach
full. The shot direction is drawn inside the cone with the polar angle taken
as `angle * sqrt(u)`, so rounds land evenly across the disc rather than
bunching in the middle.

### The unit HUD

`unhi ui\hud\cyborg_mp`, anchored top right. Shield plate, shield meter and
health meter; the health *background* has no bitmap in the tag, which is
correct for multiplayer.

**Meters are not sprites drawn at a width.** The meter bitmap's ALPHA is a
fill ramp -- brightest where the bar empties last -- and Halo lights a pixel
once the meter passes it. The HUD fragment shader discards `a <= 0` (outside
the bar) and `a < 1 - fill` (past the fill), then draws flat in the tag's
colour, which is lerped from the tag's empty colour to its full one. That is
why a draining shield goes dark blue and low health goes red with no
threshold of ours: both colours are in the tag.

Two things that cost time and are easy to hit again:

1. **A HUD bitmap is addressed by sequence, and Halo uses two shapes for
   that.** The meters are sprite sheets, where a sequence holds a rectangle
   of one sheet; the backgrounds are multi-frame bitmaps, where the sequence
   IS the frame and covers all of it. A sprite-only lookup silently loses
   every background. `sprite_or_frame` handles both.
2. **Anchor offsets are measured INWARD**, not in screen direction. The
   health meter's +29 on a top-right anchor moves it 29 to the left; the
   shield plate's -7 lets it bleed slightly past the corner, which is what
   Halo's plate does.

Nothing damages the player yet, so the bars sit full. The meters themselves
are live and driven by `hta_hud_set_shield` / `hta_hud_set_health`, verified
by rendering at 1.0, 0.55 and 0.15: the shield drains from the left and
darkens, health goes pink then red.

### Making the meters look like Halo's rather than like slabs

Comparing against the owner's screenshots of the real game, three things were
wrong and all three were in the shader:

1. **A meter must MULTIPLY the art's RGB, not replace it.** The art carries
   the bar's gradient and its bright edge; drawing flat in the tag's colour
   turns Halo's shield into a plain blue slab and hides the health bar's
   eight chevrons completely.
2. **Alpha cannot also be the opacity.** It is already the fill ramp, and the
   two meters encode it differently -- the shield's sits in a narrow band
   around 0.45, the health bar steps once per chevron from 0.94 down to 0.12.
   Using it as opacity leaves the whole bar half transparent. Halo separates
   them with the meter's `alpha multiplier`, `alpha bias` and `min alpha`,
   which we do not read yet; `smoothstep(0.15, 0.40, a)` stands in.
3. **Forcing lit pixels opaque leaves black blobs.** The health bar's left cap
   is RGB 0 at alpha 30 -- Halo blends it to nothing.

### The one number Halo does not supply

`HTA_HUD_PHONE_SCALE` (1.75) in `hud.h`. Halo's canvas assumes a monitor at
desk distance; a 21:9 handset scaling straight off screen height gives the
HUD an even smaller share of the width than Halo's 4:3 ever did, and the
owner's verdict on the faithful version was "the tiniest saddest HUD". This
is a deliberate departure and the only invented number in the HUD.

### The ammo block (2026-09-19)

The assault rifle's magazine is a **grid of pips** -- 3 rows of 20, one per
round, which is why the sprite is 227x53 -- and it is a METER like the
shield, so the same machinery drives it.

The chain matters: a `wphi` has a **`child hud`** dependency, and the plate
and outline behind the pips come from it. `weapons\assault rifle` ->
`ui\hud\master rounds` -> `ui\hud\master`. Walk it first so the child's
plate is drawn under this weapon's pips.

Weapon HUD panels are laid out **differently from the unit HUD's** -- same
idea, different offsets. Reusing the unit HUD's numbers here reads colour out
of the flash fields.

An **all-zero colour means "use the HUD's own"**, not "draw it black". The
assault rifle's ammo plate carries exactly that.

The AR has **no number elements**: its rounds are the pips. The numeric
readouts (total ammo, grenades) live on the child `ui\hud\master rounds`,
and they have no bitmap of their own -- the glyphs come from a `hud#`
(hud_number) tag reached through `hudg`, which does not reconcile yet
(1098 vs 1104). That is the next piece.

### Two scales, one of them measured not derived

Worth knowing before trusting either:

- The shield bar draws at **1.0x** its sprite scaled to the 480p canvas.
  Measured across three screenshots of the real game: 0.99, 0.97, 0.86.
  So the faithful HUD scale was right all along.
- The ammo pip grid draws at **0.51x**. Same canvas, same anchor, both width
  and height scales 1.0 in the tag. Nothing found yet says why.

`HTA_HUD_WEAPON_SCALE` (0.5) is therefore an empirical correction, not a tag
value, and `HTA_HUD_PHONE_SCALE` (1.75) is a deliberate enlargement for a
handset. Those two are the only invented numbers in the HUD and both are
named and commented as such.

## Eleven weapons (2026-09-19)

The last item on the owner's list. **SWAP** on the HUD (gamepad L1) cycles
every weapon in the cache a player could fight with, and the payoff is that
all the earlier work turned out to be genuinely weapon-driven: each one
brings its own first-person model and animation graph, muzzle flash,
on-gun counter, magazine, rate of fire, error cone, firing and dry-fire
sounds, impact sounds, crosshair and HUD ammo display, with nothing edited.

The plasma rifle's HUD shows a red heat bar instead of pips; the rocket
launcher's shows two rockets for its two rounds; the sniper's reticle is
12 px against the pistol's 28 and the assault rifle's 66. All from tags.

**A weapon names its own HUD interface** at **1152** (Weapon inherits Item
inherits Object, so its own fields start at 776 and `hud interface` is 376
in). Finding the `wphi` by matching tag paths looks right until the rocket
launcher, whose HUD tag is `rocket_launcher`, and the flamethrower's
`flame thrower` -- both silently lost their crosshairs that way.

**Playable** means a first-person model, first-person animations AND a HUD
interface. That last one is what rules out the ball and the flag: they are
held in first person with their own animations, but carried rather than
fired.

`equip_weapon` rebuilds everything, freeing and re-uploading the GPU meshes.
`hta_gfx_mesh_free` waits for device idle first, so that is safe between
frames, but it must stay on the game thread. The impact-sound cache is
cleared on swap, since impacts belong to the projectile.

## Bullet impacts (2026-09-19)

Hitting sand and hitting a base wall are the weapon's own two sounds, and
the chain is fully referenced -- no tag path is hardcoded:

`weap` -> trigger's `projectile` -> `proj weapons\assault rifle\bullet` ->
**projectile material response[MaterialType]** -> its `default effect`
(`effe`) -> `hta_effect_first_sound`.

Projectile inherits Object (380), so the responses sit at **576** absolute,
160 bytes each, `default effect` at **+4**. On the Trial's AR that gives
dirthits for dirt and sand, granhit for stone, metalhit for all three
metals, glass_hits for glass, fleshhit for a grunt -- 33 responses in all.

`hta_collision_ray_material` reports what the ray struck, and the gun
remembers it on `hit_material`, so the caller plays the right one.

**Two other places the same material index leads, not yet used:**

- `matg globals` -> materials[type] (884 bytes each) -> **melee hit sound**
  at **+868**: melee_dirt, melee_concrete, melee_metal, melee_impact_fleshy.
  Melee swings connect with nothing yet, but when they do, that is the
  sound.
- `foot sound\sfx\impulse\material_effects\weapon` group 8 holds a
  parallel set of impact sounds. Nothing here references it, and the
  projectile's own responses are the referenced route, so it is unused.

## Footsteps (2026-09-19)

You hear what you are walking on, because Halo's biped says so. The chain is
short once found:

`bipd cyborg_mp` **+ BIPD_BODY + 156** (footsteps dependency) ->
`foot globals\cyborg` -> effect group 0 (walk) -> materials[MaterialType] ->
`snd!`. Biped inherits Unit inherits Object, so its own fields start at
BIPD_BODY (752), which is why the offset looks odd on its own.

Which material you are on comes from the collision BSP, not a guess:

- a collision **surface** carries a material INDEX at **+10** (the surface is
  12 bytes, and the loader already used that stride for `first edge`)
- that indexes the BSP's **collision materials** at **SBSP +0xA4**, 20 bytes
  each, whose `material` at **+18** is Halo's MaterialType outright -- no
  shader hop needed

Blood Gulch comes out 2433 sand, 1382 stone, 1556 metal thick, 222 plastic,
347 none, which is exactly the canyon floor, the cliffs and the bases.
`hta_bsp_mesh` now carries `tri_material` per collision triangle, and object
colliders index their own `coll` tag's materials rather than the BSP's, so
they come back HTA_MATERIAL_NONE rather than wrong.

**Footsteps are paced by ground covered, not by a timer** -- one every
`HTA_STEP_LENGTH` (0.80 wu), which is a little under three a second at the
cyborg's 2.25 wu/s run. That keeps cadence with your speed for free, stops
dead when you stop, and does not change with frame rate; `test_player`
checks the same walk at 60 and 15 fps takes the same number of steps.
Landing is its own footfall however far you travelled getting there.

Materials with no sound stay silent -- plenty have none, and that is the
right answer, so the lookup result is cached including the misses.

### Melee (2026-09-19)

The owner asked for it. The `antr` already had the clip: `first-person
melee`, 37 frames (1.23 s), with its own sound on frame 0 -- so playing it
fires `ar_melee` through the cue path that reload already uses. A MELEE
button on the HUD, gamepad R1, and the swing locks out firing and reloading
until it finishes.

It does no damage, because nothing can be damaged yet.

### The pip grid is approximate, and here is why

The meter art is white-on-black with the pip SHAPE antialiased in the RGB,
not the alpha -- a pip's edge pixels are dark while carrying the same alpha
as its bright middle -- and the alpha is a three-level ramp, one level per
ROW (4, 88, 168), not a smooth left-to-right gradient like the shield's.

Drawing it at full opacity outlines every pip in navy. Weighting opacity by
the art's own brightness (`smoothstep(0.15,0.40,a) * lit`) removes that and
is what white-on-black art expects, but the result reads as outlines rather
than the solid ticks Halo shows. How Halo actually composites this is not
pinned down. Do not assume the current look is right.

### Still missing from Halo's HUD

The **numeric readouts** (total ammo, grenade count) and the **motion
tracker bottom-left**. The numbers need the `hud#` glyph tag; the tracker is
in the `unhi`. The plain white "60 / 180" the Java HUD draws still stands in
for the reserve count, which the pip grid does not show.

## A 2D HUD pass, and Halo's own crosshair (2026-09-19)

The owner's verdict after the on-gun counter: *"It doesn't feel like a ready
game yet. We have one weapon, no HUD, no crosshair, no other sound effects."*
**Netcode is parked until they say otherwise.**

All of that needs one thing first: there was **no 2D overlay path at all** in
the native renderer. The Java layer draws the touch controls; the native side
drew only 3D. Crosshair, shield bars and HUD numbers all ride on the same
screen-space quad.

### The HUD is its own program

The mesh shader cannot draw a HUD: it forces `alpha = 1.0` and multiplies by a
lightmap ×2. So there is now `shaders/hud.vert` / `hud.frag` and a
`pipeline_hud` -- no depth at all, real alpha out of the texture, and a tint
from the push constants (reusing the `light_color` slot, so the pipeline
layout is unchanged).

Vertices arrive **already in clip space**, so the HUD needs no matrix and no
camera: `hta_hud_layout` converts pixels to clip space itself. Vulkan's Y and
pixel Y both point down, so there is no flip.

`hta_submesh` gained a `tint[4]`, used only by the overlay.

### The crosshair is entirely the tag's

`wphi weapons\assault rifle\assault rifle` -- a weapon and its HUD interface
share a tag path in Halo, which is how the right `wphi` is found. Its
crosshairs reflexive (+132) holds one entry of type **aim**, whose overlay
names sequence 0 of `ui\hud\bitmaps\combined\hud_reticles`: the top-left
66x66 of a 128x128 sheet.

**Colour byte order matters and is easy to get backwards.** `ColorARGBInt` is
stored **blue, green, red, alpha**. The AR's is `FF 96 28 00` -> (r 40, g 150,
b 255), Halo's cyan. Read the other way it is orange. And every HUD element in
the Trial carries **alpha 0, which means opaque**, not invisible.

Halo lays the HUD out on a fixed **640x480** canvas and scales by screen
HEIGHT, so the reticle keeps its apparent size on any aspect ratio.

Verified by rendering and measuring pixels: the reticle lands within 2 px of
frame centre, square, at the height-scaled size. `test_hud` is 25 checks.

Only the `aim` crosshair is drawn. The rest (zoom overlays, low-ammo flashes)
need weapon state we do not track yet.

## Grenades (2026-09-20)

`globals` +296 keeps the grenade table, 68 bytes an entry: how many you may
carry, how many you spawn with in multiplayer, and the projectile. Blood
Gulch gives you **two frags of a possible four**.

Grenades are not a weapon's ammunition, so
`hta_projectiles_equip_projectile` loads one straight from a projectile tag
rather than through a `weap`.

### What makes a grenade a grenade

Two fields, both already in the projectile tag:

- **`ProjectileMaterialResponse`** (160 bytes, response at **+2**): every
  material the Trial's grenades can hit answers **reflect**, not detonate.
  Read at +0 you get the FLAGS instead and every grenade looks like it
  vanishes on contact.
- **`detonation timer starts`** at **+384**: the frag says *after first
  bounce* (0.5 s), the plasma says *when at rest* (2.0 s).

**`detonation timer starts` only means anything for something that
bounces.** The needler's needles also say "when at rest" and never come to
rest -- they fly until they hit -- so for a projectile that does not
reflect the countdown runs from launch, which is the 0.75 s a needle lives.
Honouring the field blindly made needles immortal.

### The throw is invented

**The fourth invented number.** The frag's own initial velocity is
**0.00**, because in Halo the throw comes from the player and the player's
throw strength is an engine constant that is not in the data.
`HTA_GRENADE_THROW` is 9 world units a second, which lands one about
twenty-five out on a flat throw.

The bounce damping (`PROJ_BOUNCE`, 0.35) is chosen too: the `pphy` that
gives a spent casing its elasticity belongs to PARTICLES, and projectiles
carry none.

### A nesting bug worth knowing about

The world-space dynamic mesh uploads had drifted inside `if (s->gun.n)`,
which meant projectiles and their particles were only uploaded once you had
already shot a wall and made a scorch mark. Fixed; they are unconditional.

## Health, shield and consequences (2026-09-20)

The bars had been sitting full since they were drawn. They move now, and
every number is the Trial's.

### Where vitality lives

Not on the biped and not on the unit: on the biped's **collision model**.
`ModelCollisionGeometry` reconciles at 664 and carries

```
maximum body vitality   +8      maximum shield vitality +204
recharge time         +272      shield recharge rate    +448
```

The multiplayer cyborg is **75 body, 75 shield, back after 4 seconds at
30% a second** -- a little over three seconds to refill, which is Halo's.

Damage spends the shield first and reaches the body only once it is gone.
The body never heals itself; only the shield comes back.

### Falling

`globals` +392 (GlobalsFallingDamage, 152) gives a harmful velocity range
and a maximum, **per TICK like everything else in Halo that is a speed**.
The cyborg starts being hurt at 0.15 (4.4 world units a second) and a fall
at 0.34 (10.3 wu/s) is simply fatal. Read as per-second those would make
stepping off a kerb lethal.

At the player's own gravity of 3.4, that is a free drop of about three
world units and certain death from about fifteen.

`hta_player` reports `land_speed` alongside `landed`, captured BEFORE the
ground zeroes the vertical velocity -- which it does in three separate
places, so reading it afterwards always gives zero.

### The blast catches you

A detonation effect carries its area damage as a `jpt!` among its parts.
`DamageEffect` reconciles at 672: radius bounds at +0, area-of-effect core
radius at +460, damage at +464. A rocket is **80 at the centre, full inside
0.6 world units and gone by 2.0**, so firing one at your own feet costs you
most of a shield.

## Sound in the world, and spent brass (2026-09-20)

### Sounds have a place now

Impacts and detonations are things that happen somewhere, not in your
hands, and they had all been playing at full volume dead centre. They
attenuate with distance and pan toward where they are, on a
constant-power curve so a shot sweeping past does not dip as it crosses
the middle. `hta_audio_play_pan`.

**The range is INVENTED -- the third number in this project that is.**
Halo keeps a minimum and maximum distance on every `snd!` (at +8 and +12,
the struct reconciles at 164) and **in the Trial every single one reads
0.0 .. 0.0**. The real values live in per-CLASS defaults inside the engine:
the tag's `sound class` field IS set (weapon fire 4, projectile impact 0,
object impacts 13, particle impacts 14) but what those map to is not
shippable data we have. `HTA_SOUND_NEAR` 3 and `HTA_SOUND_FAR` 60 are the
two lines to replace if the class ranges ever turn up.

### Spent brass

A weapon's firing effect carries its muzzle flashes AND its ejected
casing, on a marker called `primary ejection` -- the same way the flash
hangs off `primary trigger`. `hta_particles_add_marker` takes the
particles on one marker alone, because the flash is already the
viewmodel's job and spawning the whole effect would spray a second set of
flashes into the world.

The viewmodel poses the ejection marker every frame in its own space
(`vm->eject_pos`), and the platform turns that into a world point with the
same basis the renderer builds the weapon with: `cam + fwd*x - right*y +
up*z`.

Five weapons eject and four do not, which is exactly the human/covenant
split -- the plasma rifle, plasma pistol, needler and flamethrower have no
brass and no ejection marker.

### Every particle has its own physics (the rough edge, closed)

Each `part` names a `pphy` (PointPhysics, 64, reconciles) at Particle+20,
and it settles both rough edges at once:

```
flags +0    bit 1 "collides with structures", bit 5 "no gravity"
air gravity scale +12      air friction +36      elasticity +48
```

**Gravity is SIGNED.** A spent casing is **-1.00** and falls at full
gravity; muzzle smoke is **-0.02** and barely falls; plasma residue is
**+0.05** and RISES. Across the roster's firing effects: 6 falling, 34
floating, 71 rising. Reading the sign as a magnitude would have smoke
dropping like brass.

Collision comes from the same flag. Brass bounces off the world with the
tag's own elasticity (0.35) and smoke drifts through it, which is why only
6 of 111 particles ever touch the collision grid.

Air friction is 200 for smoke and 900 for brass, in units that are not
ours. `PART_DRAG_SCALE` (125) divides them into a per-second damping,
chosen so smoke keeps the 1.6/s that had already been verified by eye --
it preserves what was known good and scales everything else against it.

## Impact particles, and the walk bob (2026-09-20)

### Bullets kick up dust now

The gap left when particles landed. Every projectile carries **33 material
responses**, and the assault rifle has ~30 DISTINCT impact effects across
them -- far too many to load.

What makes it affordable: **Blood Gulch is made of four materials.** Its
5940 collision triangles are sand (41%), stone (23%), thick metal (26%) and
a little plastic (4%). The platform scans `col.tri_material` once at load
and only ever adds the impact effects for the materials the map actually
contains. 33 effects per weapon becomes 4.

For that, `hta_particles` now separates **types** (a bitmap, a blend, its
sprites) from **recipes** (one effect's list of what to throw, how many, how
fast). Effects share art heavily: the rifle's four impacts come out as 4
recipes over 8 shared types. Loading is two-phase -- `hta_particles_add`
for each effect, then `hta_particles_build` once -- because the mesh cannot
be sized until every type is known, and re-interning a texture mid-game
would move it under the buffer the GPU is reading.

Sand throws a soft dust puff; stone throws visible sparks. That difference
is the tag's, not ours.

### The weapon sways as you walk

Every Trial weapon carries a `first-person moving` overlay -- 25 frames
that translate the rig's root and flex the support arm -- and nothing was
playing it. It is Halo's walk bob, and without it standing still and
running looked identical.

Composed as a delta from the clip's own first frame, weighted by how fast
the player is going, so **standing still is exactly the base pose** and
there is no seam when you stop. The cycle runs faster the faster you go and
eases back to neutral rather than freezing mid-stride.

The travel is the tag's own and is subtle: **3.4 mm** at a full run. Do not
"fix" that by scaling it up without evidence; the clip says what it says.
Note the clip LOOPS -- frames 0 and 25 are identical -- so comparing either
end alone shows nothing at all, which is the trap if you go looking.

## Particles (2026-09-20)

The thing the flamethrower and the rocket explosion had been waiting on.
`src/engine/particle.c`.

An `effe` event carries a list of particles beside its parts: each names a
`part` tag, how many to spawn (`count` bounds at EffectParticle+108), how
fast to throw them (`velocity` at +132) and inside what cone (`velocity cone
angle` at +140). `hta_effect_particle_count` / `_at` walk them all;
`hta_effect_fp_flash` still picks just one for the muzzle flash.

A rocket's detonation is a lens flare plus **40 to 60** smoke puffs at
0.85 to 1.75 world units, living four seconds. The needler's is spike
debris, a flash, a flare and smoke -- four types.

Geometry follows the projectiles: ONE mesh built when the effect loads,
holding a fixed slot per particle, re-posed on the CPU each frame, a dead
slot collapsed to a point. Slots are **partitioned by type** rather than
pooled, so each type's quads are contiguous and share one submesh -- one
draw call per type instead of one per particle. `HTA_PART_PER_TYPE` is 24,
so the rocket's 40-60 smoke puffs are capped at 24.

Everything is built on equip, never mid-game: interning a texture would
move the mesh under the buffer the GPU is reading.

### Two renderer bugs it exposed

**`mesh.frag` hardcoded its output alpha to 1.0.** The alpha pipeline
blends with SRC_ALPHA, so every alpha-blended surface in the world had been
drawing fully opaque -- a rocket's smoke came out as solid black squares,
because a smoke sprite is a soft shape in the alpha channel over a black
background. It outputs the base map's alpha now. The additive pipeline
blends ONE/ONE and never cared.

**Art with no alpha channel cannot be alpha-blended.** The rocket's lens
flare, `flares_generic`, is a format that carries none, so every texel
decodes to alpha 255 and it drew as a square of its own black background.
A particle whose decoded texture has no alpha is switched to ADDITIVE,
where black contributes nothing -- which is how a flare reads anyway. This
is measured from the decoded texels, not assumed from the format number.

### `dyn` is an array now

`hta_gfx_draw` takes `const hta_gfx_dynamic *dyn, uint32_t dyn_count`
(max 4). Projectiles and their particles are separate meshes with separate
textures, both rewritten every frame. The three passes run across ALL the
dynamic meshes in order -- opaque, then alpha, then additive -- so a
rocket's smoke never sorts in front of the rocket.

### What this does not do yet

- **Bullet impacts.** Every projectile carries 33 material responses, each
  an effect with its own sparks and smoke. Loading one per material would
  be 33 particle systems; loading on demand would move the mesh mid-frame.
  Hitscan impacts are still just a decal and a sound.
- **The flamethrower's jet.** Its detonation effect has no drawable
  particles, and the jet itself is a `pctl` particle SYSTEM -- a different
  tag class with emitters and curves, not an effect's particle list.
- No collision: particles pass through walls.

## The detail mask (2026-09-20)

`ShaderModelDetailMask` at ShaderModel+214 gates a model's detail map by
one channel of its **multipurpose map** (+188). Nine values: `none`, then
inverse/straight pairs for reflection, self-illumination, change colour and
auxiliary. Blood Gulch uses only the reflection pair -- 6 straight, 5
inverse, 23 `none` -- which is vehicles keeping detail off their shiny
panels.

**Which channel is which** was settled from the data rather than from
memory. The cyborg's multipurpose map has R 0, G 1, B 43 with 32% of the
blue in mid-tones and almost none of the green: blue carries the
change-colour structure and green is empty. That matches Halo's documented
layout, so:

```
R auxiliary   G self-illumination   B change colour   A reflection
```

Masked out means **neutral grey**, which the double-biased multiply turns
into "leave the base alone" -- not black, and not "skip the detail sample".

Reaches 32 of the 315 submeshes Blood Gulch's placed objects contribute.
Nothing a player HOLDS uses one: the cyborg's first-person hands say
`none`, so this changed nothing on the weapon in your hands, exactly as
predicted when it was deferred.

Descriptor bindings are now five: base, lightmap, detail, detail2,
multipurpose.

## Model detail maps, and a zeroed index that halved everything (2026-09-20)

### `hta_submesh_init`, and why memset is not enough

Filling in `shader_model` detail maps turned up a bug the detail work had
been carrying since it landed.

`model.c` cleared each new submesh with `memset` and then reset
`albedo_tex` and `lightmap_tex` to `~0u` -- but **not** `detail_tex`, which
was therefore left at **0. Zero is a valid texture index.** The renderer
duly bound each mesh's own first texture as every submesh's detail map and
multiplied it in at `det * 2`. With the assault rifle's first texture
averaging 58/255, that is a multiply by roughly 0.45: every weapon, every
piece of scenery and every vehicle had been running at HALF brightness.
That was most of "the weapons are a bit dark in general".

`hta_submesh_init` now clears a submesh to the right defaults -- `~0u` for
all four texture slots, `0xFFFF` for the lightmap index, `-1` for the meter
-- and `model.c`, `viewmodel.c`, `hud.c` and `gun.c` all use it. Do not go
back to `memset` for these: several slots mean "none" at `~0u`, and the
compiler will not tell you.

`tests/test_weapons.c` pins the invariant: a submesh may not claim a detail
map it does not have, in either direction.

### And the detail maps themselves

34 of Blood Gulch's 117 model shaders carry one, `ShaderModel` keeping it
at scale 216 / map 220 (the struct reconciles at 440). Every one of them
uses the same **double biased multiply** the environment shader does, so
`hta_shader_detail_bitmap` just learned a second pair of offsets and the
shader path is shared.

The ones that show: `characters\cyborg\fp\shaders\rubber hands` (6x) and
`armor hands` (10x) -- which is to say the hands on EVERY weapon -- plus
the warthog, ghost, banshee, gun turret, the flamethrower's body and the
fuel rod gun.

Still unread: the `detail mask` at 214, which names the channel of the
multipurpose map that gates the detail ("reflection mask", "self-illumination
mask", and their inverses). Half the model detail maps ask for one. The
Trial's first-person multipurpose maps are all zeros, so masking them would
currently change nothing on the weapon you are holding.

## The first-person weapon was never lit (2026-09-20)

Owner: "the rifle screen is supposed to be much brighter... I think the
weapons are a bit dark in general. Maybe we are missing something?" Yes,
and it was a whole term of the lighting equation.

`mesh.frag` declared `light_dir`, `light_color` and `ambient` and **used
none of them**. The world does not need them -- it carries a lightmap per
surface -- but the viewmodel has no lightmap, so it fell through to the
mid-grey fallback and rendered at RAW ALBEDO with no lighting at all.

The Trial's gun textures are dark: the assault rifle's body averages
**58/255**, its display panels 13 to 24. Unlit that is nearly black, while
the real game shows a lit mid-grey rifle. So:

```
ndl = dot(N, -L) * 0.5 + 0.5          // WRAPPED, not clamped
col = albedo * (ambient + light * ndl) * 2.0
```

Three things worth keeping:

- **Wrapped, not clamped.** A hard `max(dot,0)` splits the weapon into a
  blown highlight and a black underside. Halo's gun is evenly lit with soft
  modelling; wrapping keeps everything above the ambient floor.
- **The x2** is the same half-bright convention the lightmaps use, and is
  what brings a 58/255 texture up to the mid-grey of the reference.
- **The light arrives already rotated into viewmodel space.** `mesh.vert`
  passes normals through untransformed, so the viewmodel's are in its own
  frame (+X forward, +Y left, +Z up); the CPU rotates the scene light into
  that frame rather than the shader guessing.

`light_color.w` selects the path, and the **ADD pass is pushed with w = 0**:
the round counter's digits, the icons and the muzzle flash are emissive.
Lighting them would be wrong twice -- they glow by themselves, and a
billboard's normal need not face the sun.

### What the rifle's counter is NOT

Chased and ruled out, so nobody repeats it:

- Its digits are a `schi` with `framebuffer blend = add` and **one map whose
  colour function is "current"** -- no multiplier anywhere. We draw exactly
  what the tag asks for.
- The digit art is not dim: it peaks at **213/255**. It is the gun body
  around it that is dark.
- `shader_model` has a **multipurpose map** whose green channel is
  self-illumination, and the rifle's first-person one decodes to all zeros
  across R, G, B and A. That is not a decode failure -- it is format 14 like
  the base map beside it, which decodes fine. The assault rifle genuinely
  has no self-illumination, no specular and no change colour in first
  person.

What was left is exposure, and the lit viewmodel is what fixes it.

`shader_model` detail maps (scale at 216, map at 220) are still unread. The
rifle declares a detail scale of 8.0 with no map, so it would gain nothing;
other models may.

## The needler was invisible, not broken (2026-09-20)

### `sgla` was never handled, so the needles were dark

Three rounds of "the needler looks wrong" all had the same root cause, and
it was not the animation.

`weapons\needler\shaders\needler luminous` is a **`sgla`**
(shader_transparent_glass) -- the glowing crystal needles. Nothing handled
that class, so:

- `hta_shader_draw_mode` fell through to **OPAQUE**, drawing lit crystal as
  solid dark spikes; and
- `hta_shader_base_bitmap`'s generic "first `bitm` dependency" scan picked
  the **reflection cube map**, because in `ShaderTransparentGlass` (480,
  reconciles) that comes before the diffuse map at 344.

So the needles were dark spikes wearing a grey cubemap, against a dark gun.
They now draw ADDITIVE with their diffuse map. Additive is a stand-in: we
have no refraction and no cube-map reflection, and what the eye reads on
these is the glow.

**Every earlier visual judgement about the needler was made on needles that
were nearly invisible.** That is why the animation was "fixed" twice in the
wrong direction. Fix the shading before trusting the animation.

### And then the animation, correctly

With the needles visible it took one render to settle: **the idle pose
already holds the complete rack.** That is what a loaded needler looks
like. So the ammunition clip is a delta away from FULL, and its full end is
its **last** frame:

```
delta_i = overlay[frame] . overlay[LAST]^-1
local_i = delta_i . local_i
ammo_frame = fraction * last
```

At a full magazine this is the identity by construction, so the idle's rack
survives untouched -- `tests/test_weapons.c` measures that drift and
requires zero. Referencing the delta to frame 0 (the spent end) instead
splayed the needles outward as the magazine emptied, which is the artifact
to recognise. Needle vertices above the gun now fall 762 -> 250 across the
magazine, monotonically.

Three wrong readings on this one clip. The order that works: make it
visible, compare against a reference screenshot, and only then reason about
frames.

### Mipmaps are for surfaces that minify

The rifle's round counter went dim and smeared the moment mipmaps landed:
its digits are small additive quads, and a lower level averages them with
the black around them.

**Dynamic meshes never minify** -- the viewmodel and the HUD are drawn at a
fixed size a few centimetres from the eye -- so `upload_mesh` now mips only
static meshes (`vslots == 0`). The world keeps its chain; the LCD and the
HUD font are sharp again. This is also why a packed atlas and mipmaps do
not mix: with no padding between cells, lower levels bleed across them.

## Meters draw their empty half, and mipmaps (2026-09-20)

### The pip grid was not depleting at all

Owner: "a few bullets left in the magazine but it looks like a lot of
bullets in the top left". Three wrong ideas before the right one, so here
is the record.

**A Halo meter does not discard the part the fill has not reached -- it
paints it in the element's EMPTY colour** (`WeaponHUDInterfaceMeter` +100).
That is the whole mechanism, and the assault rifle proves it: its
`color at meter minimum` and `color at meter maximum` are the SAME blue
(BGRA 255,150,40,0). Lerping between them does nothing. The only thing
distinguishing a live pip from a spent one is that empty colour, a dark
navy.

Alpha is WHERE a pixel sits along the meter -- the rifle's three pip rows
carry rising alpha because that is their firing order -- so:

```
filled = t.a <= fill
colour = filled ? tint : empty
opacity = art brightness        (NOT the alpha)
```

Opacity must not come from alpha either: alpha is already the ordering
channel, so using it made early pips faint and late ones bright, which is
what made an almost-empty magazine look fuller than a full one. This art is
white-on-black and antialiases its shapes in RGB, so brightness is the
right channel -- and that is also what keeps the health bar's dim left cap
from rendering as a black blob.

`hta_submesh.empty` carries it; the HUD pushes it in the fourth push-constant
vec4, the same slot `mesh.frag` uses for detail scales.

### Mipmaps, and a trap worth 4 seconds

Textures now upload a full mip chain, generated with a box filter, and the
samplers are trilinear with anisotropy where the device has it (capped at
8x). That is what fixes "the textures look grainy at distances": a detail
map tiling a hundred times across a hillside samples one texel in a few
hundred without one.

The hand-rolled sub-pixel fade in `mesh.frag` is gone -- mipmapping does
that job properly.

**Never read back from a mapped staging allocation.** Filtering each level
straight into the staging buffer means every level reads the previous one
out of write-combined memory: Blood Gulch's texture upload went from 59 ms
to **4189 ms**. Build the chain in ordinary malloc'd memory and `memcpy` it
across once -- 108 ms, and the mips cost about 50 ms of that.

Device memory for Blood Gulch: 29.7 MiB -> 38.4 MiB, which is the expected
4/3.

## Detail maps, HUD numbers, and the needler a third time (2026-09-20)

### The world was flat because detail maps were never read

The single biggest visual gap. `shader_environment` layers TWO detail maps
over the base at high frequency, and we only ever sampled the base:

```
levels\test\bloodgulch\shaders\blood ground
   primary detail    levels\b30\bitmaps\detail sand   scale 100
   secondary detail  levels\a30\bitmaps\detail grass  scale  60
```

`ShaderEnvironment` reconciles at 836: primary scale 180, primary 184,
secondary scale 200, secondary 204. 20 of Blood Gulch's 79 submeshes have
them.

Halo combines them as a **double-biased multiply** -- `base * detail * 2`,
so mid-grey is a no-op -- and blends the two by the **base map's own
alpha**. That is how one shader is sand in places and grass in others.

Two things worth keeping:

- The fallback texture for a surface with no detail map is the existing
  mid-grey `tex_light`, which doubles to exactly 1.0. No branch, no second
  pipeline.
- **We upload no mipmaps.** A map tiling 100 times across a hillside
  aliases into static at any distance. `mesh.frag` measures how fast the
  detail UV moves per pixel (`dFdx`/`dFdy`) and eases back to neutral grey
  as it goes sub-pixel. That is what a mip chain would do, and without it
  the ground looks like television snow.

This took the descriptor layout from 2 bindings to 4 and `PUSH_SIZE` from
112 to 128 (the guaranteed Vulkan minimum), with the two detail scales
pushed per submesh.

### HUD numbers come from a FONT

There is no digit bitmap anywhere in a weapon's HUD tag. `WeaponHUDInterfaceNumber`
(160 bytes) says only where the number goes, how many digits it gets and
what colour it is; the glyphs come from `hud_globals`' **fullscreen font**,
which on the Trial is `ui\large_ui`.

A `font` (156) is a character table (`FontCharacter` 20) plus one flat blob
of 8-bit coverage. Each character carries its own size, origin and an offset
into that blob -- no sheet, no packing. `src/asset/font.c` builds the ten
digits into one small RGBA atlas, white with the coverage as alpha, which
the HUD tints like any other element.

Two traps, both cost time:

- The blob is addressed by the **pointer at TagDataOffset+12**, not the
  file offset at +8.
- The number elements are on the weapon HUD's **child** (`ui\hud\master
  rounds`), not on the weapon's own tag, and `hta_hud_load` has to load the
  font BEFORE walking the weapon HUD or they are silently skipped.

The counter shows the TOTAL carried -- magazine plus reserve -- which is
what Halo's 240 and 036 are. The pips are the magazine and the gun's own
readout is the magazine.

### The needler, a third time: it POSES, and the full end is the LAST frame

Two wrong readings in a row, so this is the record.

The ammunition clip **replaces** the needle bones; it is not an additive
delta. Composing a delta throws them into a splayed mess at an empty
magazine. And **frame 0 is EMPTY, the last frame is FULL** -- the opposite
of the obvious reading.

What misleads is that at the full end all sixteen needle bones share ONE
transform, which reads as "collapsed to a point" until you remember each
needle is skinned against its own bind pose. Identical node transforms
therefore mean every needle sits at rest, i.e. the complete rack; it is the
SPENT end that gives them separate, displaced transforms.

**Do not reason about this from node translations.** Measure the posed
bounding box, or count vertices above the gun. The test sweeps the magazine
and requires the count to fall monotonically.

### Impact marks were drawn opaque

A decal is a hole in a sheet of alpha. The fx pass bound the opaque pipeline
unconditionally, so every bullet hole painted its transparent border onto
the wall as a square. The pass honours each submesh's draw mode now, and
the marks are `HTA_DRAW_ALPHA`.

### The shotgun's reload had one animation for twelve shells

`hta_ammo` chains the shells by itself, but only the first press replayed
the clip -- every shell after it loaded silently. The platform replays on
`reload_began`, which the chain sets per shell. The shotgun's
`reload-empty` is 12 frames = 0.40 s, exactly its tagged per-shell time.

(Its `enter` / `exit-full` / `exit-empty` clips are the rest of the
choreography and are still unused.)

### Still open

- **The ammo pip grid tracks backwards**: more pips light as the magazine
  empties. The meter reaches the element (`ammo_meter` is set and the art
  responds), so it is the fill sense or the layer the meter is on, not the
  plumbing.
- **The flamethrower is one quad, not a jet**, and the rocket's explosion
  has no particles or light. Both need a particle system.
- No mipmaps anywhere; the detail fade is standing in for one.

## The scope, the needles again, explosions and real decals (2026-09-20)

### Overlays are DELTAS, and the needler proved it

The needler shipped inverted: needles gone at a full magazine, reappearing
as a clump at one spot as it emptied. `hta_viewmodel_apply_ammo` was
SUBSTITUTING the overlay's absolute node values, which throws the base clip
away -- the ammunition clip's own frame 0 is nowhere near where the idle
holds the needles.

An overlay is what the clip has moved **since its own first frame**:

```
delta_i = overlay[frame] . overlay[0]^-1
local_i = delta_i . local_i        (for nodes the overlay keyframes)
```

**The invariant to check if this ever looks wrong again: at frame 0 the
overlay must change nothing at all.** `tests/test_weapons.c` measures that
drift and requires it to be zero. Do not re-derive this by staring at node
positions; the absolute values at either end of the clip look plausible
both ways round.

`htaview` drove the graph itself and never called `hta_viewmodel_update`,
so `--ammo` previewed something the device never drew. Both now go through
`hta_viewmodel_apply_ammo`.

### The sniper is scoped, not just magnified

Everything in a weapon's `wphi` flagged **"show only when zoomed"** (overlay
flags at crosshair-overlay+72, bit 2) is scope furniture. The sniper has
six such elements per zoom level: reticle ticks from
`sniper_scope_crosshairs2` (2x) and `_sm` (8x), plus a magnification label.

Two things the tag does not say outright:

- **Which level a block belongs to.** There is no field. The Nth zoom-only
  block of a given crosshair type is taken as level N. That is the only
  assumption in `load_scope`.
- **Which sprite.** The label's sequence is not one sprite but TWO side by
  side in one 64x64 sheet -- "2x" at u 0..0.391 and "8x" at 0.391..0.797 --
  so the zoom LEVEL picks the sprite within the sequence, not the sequence.
  `hta_bitmap_sprite_at` only ever returned a sequence's first sprite;
  `hta_bitmap_sprite_in` and `hta_bitmap_sprite_count` are new for this.
  Drawing sprite 0 at both levels put "2x" on screen while scoped to eight.

An overlay flagged **"not a sprite"** (bit 1) addresses a whole bitmap FRAME
by its sequence index; the reticle ticks are all of those.

Halo also takes the weapon off the screen while scoped, which is most of
what makes it read as a scope rather than a zoom. The platform skips the
viewmodel when `zoom_level > 0`.

**Watch the HUD's texture table.** It was `calloc(16, ...)` while elements
were capped at 16; the scope's three extra bitmaps overran it and showed up
as a double free in `test_hud`, nowhere near the cause. It is
`HTA_HUD_MAX_ELEMENTS` now. Interning past the end of that table corrupts
the heap silently.

### Explosions

A rocket detonating did nothing audible or visible beyond a bullet-sized
mark, because the rocket's projectile has **no material-response sounds at
all** -- its bang is a PART of its detonation effect, beside the damage, the
light, the particle system and the decal:

```
weapons\rocket launcher\effects\rocket explosion
   snd! sound\sfx\weapons\frag grenade\expl
   pctl weapons\frag grenade\effects\explosion med
   deca effects\decals\bullet holes\grenade char
   ligh weapons\frag grenade\explosion
```

`hta_effect_detonation` reads the sound, the decal and its radius out of an
effect's parts. A rocket's `grenade char` is **1.25 world units**; impact
marks had a hardcoded 0.035 half-width, which is why an explosion pricked
the wall instead of charring it. Marks carry their own size now.

Still missing: the particle system and the light. Those need a particle
engine, which this does not have.

### Impact marks use Halo's decal art

They were a flat **1x1 grey pixel** -- literally `malloc(4)` in `gun.c`.
Every material response names a decal: `dirt pistol` 64x64 for bullets,
`plasma burn` 32x32, `flame thrower char`, `grenade char` for the rocket.
`hta_gun_set_decal` takes a copy and `hta_gun_build_mesh` uses it.

One decal per weapon, chosen on equip: the projectile's own detonation
decal if it has one, else the first material response that names one. Note
the rocket's material 0 response is a BLOOD SPLAT, so the detonation decal
has to win -- ordering matters here.

### Textures are not being downscaled

Asked and checked: Blood Gulch decodes **31 unique environment textures,
8 to 1024 px, 8.1 MiB of RGBA** -- the Trial's own art at full size, base
mip. `htaview` prints the range now. There is no higher-resolution art
being skipped; Trial textures are simply 2003 textures.

### The flamethrower has a flame again

Its first-person flame particle hangs off `spawn fire`, not `primary
trigger`, so `setup_flash` found nothing. It now falls back to any marker
when the trigger marker has none -- still first-person and still additive.
All nine weapons have a flash. A real flame JET is a particle system and
remains out of reach; this is one quad at the nozzle, restarted every shot,
shrinking over the particle's 1.5 s life.

## Needles that deplete, and rockets you can watch fly (2026-09-20)

The last two of the owner's six.

### The needler wears its magazine

Its first-person model has `frame needle01`..`needle16`, and the weapon's
animation graph has a clip nothing was playing: **`first-person
ammunition`**, 21 frames for a 20-round magazine, type 1 (OVERLAY). Frame 0
is full, frame 20 empty. It is the only Trial weapon with one -- every other
weapon has `first-person overlays` and nothing else.

Overlays compose, they do not replace. A sampled animation fills EVERY node,
using the animation's own defaults for the ones it does not keyframe, so
taking all of them would flatten the arms. `hta_anim_animates(g, anim,
node)` reads Halo's per-node keyframe bitmask, and the viewmodel takes only
those: sixteen needle bones, and `frame r pinky tip`, which is flagged but
never moves. 745 of the needler's 3861 vertices move between full and empty,
by up to 17.7 cm.

The overlay is composed in `hta_viewmodel_update` after the base clip, so
the needles stay retracted through firing, reloading and the melee swing.

### Projectiles

`hta_projectiles` in `src/engine/projectile.c`. Halo fires everything as a
projectile, bullets included, but only two carried weapons give theirs a
MODEL: the rocket launcher (117 verts) and the needler (10). The rest are
particles and stay hitscan -- a bullet at 324 wu/s crosses Blood Gulch in
under a third of a second.

**The units are per TICK.** `initial velocity` (proj+484) and `final
velocity` (+488) are world units per tick at 30 ticks a second; `maximum
range` (+456) is world units and `timer` (+444) is seconds. So the rocket's
0.4 is **12 wu/s** (about 36 m/s, which is why you can dodge one) and the
sniper's 33.3 is 1000 wu/s. Reading them as per-second gives a rocket you
can walk past, and is the mistake to avoid. Projectile reconciles at 588.

Geometry: ONE mesh holding `HTA_PROJ_MAX` copies of the model, built on
equip and re-posed on the CPU each frame -- the muzzle flash's trick. An
idle copy is collapsed to a point rather than removed, so the index buffer
never changes. **The mesh's `textures` table must be allocated before
anything is appended into it**, or every submesh comes out with
`albedo_tex == ~0u` and the renderer silently draws nothing. That cost an
hour; `hta_viewmodel_load` does the same `calloc(256, ...)` for the same
reason.

A weapon with a drawable round no longer hitscans: `hta_gun_launch` spends
the cooldown and hands back the shot direction, the projectile does its own
collision on the way, and its detonation calls `hta_gun_add_mark` and plays
the material's impact -- the same scorch and the same sound a bullet leaves.

### A renderer slot, and a push-constant bug it uncovered

`hta_gfx_draw` gained `const hta_gfx_dynamic *dyn`: world-space geometry
whose vertices change every frame, the same arrangement as
`hta_gfx_viewmodel` but drawn with the world camera.

Adding it exposed an existing bug. The draw order is world, viewmodel, fx,
HUD -- and the **viewmodel pass pushes a VIEW-space matrix while the fx pass
never pushed anything of its own**. Scorch marks were therefore drawn with
the viewmodel's matrices whenever a viewmodel was present, which is to say
always on device. The world constants are pushed back before both fx and the
dynamic pass now. Anything added after the viewmodel that lives in the world
must do the same.

## Zoom, the shotgun's flash, and the gun with two sets of arms (2026-09-19)

Owner's second pass on the roster. Four of the six were real bugs.

### Zoom did nothing, and stuttered doing it

`hta_player_update` writes `cam->fov_y` from the biped tag **every frame**.
Poking the zoom into the camera therefore lasted exactly one frame, which on
device read as "the screen stutters for a moment then nothing happens". The
magnification now lives on the player (`hta_player_set_zoom`), and the player
divides its own tagged field of view by it. Anything else that wants to own
the field of view has to go through there too.

The stutter itself was the zoom sound being decoded on the game thread at
first press; the zoom in/out sounds are decoded on equip now.
`tests/test_player.c` `[scope]` pins the regression: zoomed, then still
zoomed 120 updates later.

### The shotgun's muzzle flash was its sparks

Every candidate in the shotgun's firing effect is additive and first-person,
and we took the first one. That is `effects\particles\flash\sparks trail`:
six to nine sprites **1.6 cm** across thrown at 15 world units a second. The
actual flash, `flash h generic`, is 12.5 cm and sits two entries later. A
1.6 cm flash is invisible, hence "no muzzle flash".

`hta_effect_fp_flash` now scores candidates instead of taking the first:

- a particle whose **count** tops out at 0 never spawns -- the tags carry
  switched-off size variants that way, and they are usually the biggest;
- **tier 0 sits on the muzzle** (speed <= 1 wu/s), tier 1 is thrown;
- within a tier, the widest wins.

Tiers rather than a hard rejection because the **sniper** has no stationary
flash at all -- its muzzle brake sprays 15 to 20 sprites at 8 to 12 wu/s, so
a thrown one is the honest stand-in there. The quad's size is now the
MIDDLE of the tagged radius range, not the top: Halo randomises each sprite
inside that range, and taking the top put a 50 cm flare on the fuel rod gun.

### Two needlers

`needler` and `mp_needler` are separate tags sharing a first-person model,
animation graph, HUD interface, magazine and rate of fire -- the same gun
twice in the swap order. `hta_weapon_list_playable` now skips a weapon whose
(fp model, fp anim) pair it has already listed.

### "The weird gun": two sets of arms, and a corrupted bind pose

Worth reading before touching `hta_viewmodel_load`.

Almost every Trial weapon's first-person model is the **gun alone**, 3 to 7
nodes, and the arms come from the globals hands model. `plasma_cannon` --
the fuel rod gun -- ships a **self-contained 41-node model with its own
arms**, 37 nodes shared with the hands.

`hta_model_append_skinned` writes `rest_inv[node]` for every node the model
defines, and the gun is appended second. So the fuel rod gun's model
silently overwrote the hands' bind pose for all 37 shared nodes, `frame
bone24` (the ROOT) among them, which differs by 7.8 cm in z. Result: the
globals hands were lifted to eye level and drawn as a detached arm beside
the gun. The viewmodel now skips the globals hands when the weapon's own
model has `frame l wriste`.

**An earlier note in this file was wrong** and is corrected here: the fuel
rod gun is NOT posed 50 cm below the eye. That figure came from the bind
pose. Posed at its idle it sits where the others do. What made it look
wrong was the doubled arms above, plus its idle holding the gun against the
camera.

It is kept out of the roster anyway, on the same signal: Halo never gives it
to a player, its first-person animations were never finished, and in the
hands it fills half the screen. One line in `hta_weapon_list_playable`.

Nine carriable weapons remain. Note that Blood Gulch's netgame equipment
DOES list the fuel rod gun -- placement is not evidence a weapon was
finished for first person.

### Where the wrist-height idea went

Measuring how high the idle poses the wrists looked like the principled
filter and is **not**: every weapon including the fuel rod gun rests them 2
to 9 cm below the eye (it is at -0.061, between the flamethrower's -0.057
and the rocket launcher's -0.063). The hands *mesh* was displaced, not the
wrist node, because the displacement came from the bind pose. Do not
re-derive it.

## The whole roster: firing clips, shotgun shells, zoom, the flamethrower (2026-09-19)

Owner's list: *"all of the weapons need firing animations. Also the shotgun when
I click reload reloads one shell at a time awkwardly. Also the sniper I cant
zoom in... look at the screenshot, whatever this gun is looks weird. Need sounds
for some as well like the flamethrower."* Five items, all tag questions.

### Only the assault rifle calls its fire clip "firing"

Every other weapon names it `fire-1`. We looked up exactly one name per state,
so ten of eleven weapons silently had no firing animation. `CLIP_NAMES` in
`src/engine/viewmodel.c` is now a table of fallbacks per state, tried in order:

```
fire   : "fire-1", "firing", "fire-2"
reload : "reload-full", "reload-empty", "reload"
```

**Order matters.** A bare `"fire"` would substring-match the plasma weapons'
`misfire-1`, which is the overheat cough, not the shot. Keep the specific
names first and never add a bare `"fire"`.

10 of 11 weapons now have a fire clip. The flamethrower genuinely has none in
its `antr` -- Bungie animated it as a held pose plus a particle jet.

### The shotgun loads one shell at a time, and that is correct

`rounds reloaded 1`, `reload time 0.40 s`, straight from the tag. What was
missing is that one request should keep going. `hta_ammo` gained `chaining`:
set when `per_reload < mag_max`, re-armed at the end of each shell in
`hta_ammo_update`, and cleared when full or when the trigger is pulled.

Firing mid-chain does not merely cancel it -- it **fires**. `hta_ammo_shoot`
drops the weapon back to `READY` when a chained reload is running and there is
a round in it, because one shell in the gun is enough to shoot with. Without
that the phase check refuses the shot and the gun feels stuck. A magazine
weapon (`per_reload == mag_max`, so `chaining` is false) is still
uninterruptible, which keeps the AR behaving as it did.

### Zoom is three weapons, and only one of them twice

`zoom levels` at `weap+986`, `zoom magnification range` at `+988` (a bounds
field: two floats). In the Trial only the **pistol (2x)**, the **rocket
launcher (2x)** and the **sniper rifle (2x / 8x)** zoom at all; everything
else is 0 levels and must not divide the field of view by anything.
`zoom_magnification()` spreads the tagged first and last magnification evenly
across the levels, so the sniper's two come out 2x and 8x. ZOOM button and
gamepad THUMBR cycle 0 -> 1 -> ... -> 0.

### The weird-looking gun is the plasma cannon, and it is faithful

Its geometry is *smaller* than the sniper's (0.21 x 0.09 x 0.37 against
0.50 x 0.08 x 0.41). It looks wrong because of where its own animation puts
it: hanging `z -0.50..-0.13` at `x 0.05..0.26`, i.e. half a metre below the
eye and 5 cm forward, jammed against the camera. It is the detached turret
gun. Blood Gulch really does place it -- see below -- so it stays in the
rotation. Dropping it would be a roster filter, not a fix.

### What Blood Gulch actually places

`Scenario+900` is the netgame equipment reflexive, 144 bytes an entry, with an
`item collection` dependency at **+80**. Each `itmc` has permutations at +0,
84 bytes each, with the `item` dependency at **+36**. Walking that gives the
eight weapons the map spawns: shotgun, assault rifle, plasma rifle, pistol,
rocket launcher, sniper rifle, **flamethrower and plasma cannon**. Both of the
odd ones are legitimately there.

### The flamethrower's roar is an object attachment, not a shot

Its firing effect `weapons\flamethrower\effects\flame thrower jet` has one
event with **zero parts**, and `WeaponTriggerFiringEffect` has no sound field
at all -- so `hta_effect_first_sound` correctly returns nothing. Halo hangs
continuous sounds off the **object**:

- `Object+320` attachments, 72 bytes each: `type` dependency at +0 (which
  accepts `lsnd` among others), `marker` TagString at +16, `primary scale`
  function at +48.
- `SoundLooping+60` tracks, 160 bytes each: `start` at +48, `loop` at +64,
  `end` at +80, `gain` at +4. Both structs reconcile (84 and 160).

The flamethrower carries `sound\sfx\weapons\flamethrower\fire_ft` on
`primary trigger`, whose loop track is `flamethrower\fire`. `hta_object_loop_sound`
in `src/asset/effect.c` reads it.

**Only fall back to the attachment when the firing effect has no sound.** The
plasma pistol hangs `plasma rifle\charge` -- its *overcharge* whine -- on the
same marker, and looping that on every shot would be wrong. Across the whole
roster exactly one weapon has no effect sound and exactly one has a usable
loop, and they are the same weapon; `tests/test_weapons.c` pins that.

The mixer learned continuous voices for this: `hta_audio_loop(a, id, clip,
gain)` and `hta_audio_loop_stop(a, id)`, routed through the same SPSC request
ring (a request whose clip is `HTA_AUDIO_NO_CLIP` is a stop). Asking for a
running loop again leaves it alone rather than restarting it, so the caller
can just call it every frame while the trigger is held. Looping voices are
excluded from voice stealing -- cutting a continuous sound is far more
audible than clipping a gunshot's tail.

### New test

`tests/test_weapons.c` (needs `HTA_MAP`) walks the playable roster and checks
every weapon's animation slots, the three zoom weapons' tagged magnifications,
and the firing-sound split above.

## The counter on the gun, and a strip bug it uncovered (2026-09-19)

**Confirmed on the S24+:** magazine, reload and muzzle flash all work; the
on-gun round counter reads correctly and tracks the HUD.

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
- No world weapon pickups and no resupply: you spawn with 60 + 180 from the
  tag, and once that is gone the rifle clicks until the app restarts. The
  owner reached 60/0 on 2026-09-19 simply by testing.
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
| Screen HUD from `wphi` | `src/engine/hud.c`, `.h` |
| HUD shader + pipeline | `shaders/hud.vert`, `hud.frag`, `pipeline_hud` |
| AAudio stream | `src/platform/audio_android.c` |
| snd! + Xbox ADPCM + effe->snd! | `src/asset/sound.c`, `.h` |
| Sound inspector / WAV dump | `src/tools/htasound.c` |
| Fit probe at a coordinate | `src/tools/htaprobe.c` |
| Map / bitmap picker | `android/.../SetupActivity.java` |
| Android glue | `src/platform/platform_android.c` |
| Animation + skinning tests | `tests/test_anim.c` (needs `HTA_MAP`) |
| Roster: clips, zoom, flashes, sounds | `tests/test_weapons.c` (needs `HTA_MAP`) |
| Projectiles in flight | `src/engine/projectile.c`, `.h` |
| Projectile tests | `tests/test_projectile.c` |
| Overlay clips / keyframe mask | `src/asset/anim.c` `hta_anim_animates` |
| Rocket preview | `htaview --weapon "rocket launcher" --fly 0.05` |
| Model node lookup | `src/asset/model.c` `hta_model_has_node` |
| Scope magnification | `src/engine/player.c` `hta_player_set_zoom` |
| Scope furniture from `wphi` | `src/engine/hud.c` `load_scope` |
| Sprite within a sequence | `src/asset/bitmap.c` `hta_bitmap_sprite_in` |
| Detonation sound / decal | `src/asset/effect.c` `hta_effect_detonation` |
| Impact-mark art | `src/engine/gun.c` `hta_gun_set_decal` |
| Detail maps | `src/asset/bitmap.c` `hta_shader_detail_bitmap`, `shaders/mesh.frag` |
| HUD font digits | `src/asset/font.c` |
| Meter empty colour | `shaders/hud.frag`, `hta_submesh.empty` |
| Mipmap generation | `src/gfx/gfx_vulkan.c` `downsample` / `upload_rgba_mips` |
| Glass shaders (`sgla`) | `src/asset/bitmap.c` draw mode + diffuse map |
| Viewmodel lighting | `shaders/mesh.frag` lit path, `gfx_vulkan.c` viewmodel push |
| Submesh defaults | `src/asset/bsp.c` `hta_submesh_init` |
| Detail mask | `src/asset/bitmap.c` `hta_shader_multipurpose`, `shaders/mesh.frag` |
| Particles | `src/engine/particle.c`, `.h`, `tests/test_particle.c` |
| Walk bob | `src/engine/viewmodel.c` `apply_move` |
| Positional sound | `platform_android.c` `play_tag_at`, `hta_audio_play_pan` |
| Health / shield / falling | `src/engine/vitals.c`, `tests/test_vitals.c` |
| Grenades | `globals`+296, `hta_projectiles_equip_projectile` |
| Blast damage | `src/asset/effect.c` `hta_effect_damage` |
| Casing ejection | `viewmodel.c` `eject_pos`, `hta_particles_add_marker` |
| Map material scan | `platform_android.c` `map_material[]` |
| Effect particle walk | `src/asset/effect.c` `hta_effect_particle_at` |
| Rounds counter | `src/engine/hud.c` `load_numbers` |
| Object attachments / looping sounds | `src/asset/effect.c` `hta_object_loop_sound` |
| Continuous voices | `src/engine/audio.c` `hta_audio_loop` |
| Tag physics tests | `tests/test_biped.c` (needs `HTA_MAP`) |

**Reading Invader's JSON:** count a field with `"bounds": true` as **two** values. Missing
that is what made `Weapon` come out 4 bytes short and put every offset past
`zoom magnification range` (0x3DC) in the wrong place.
