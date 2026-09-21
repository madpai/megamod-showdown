# Progress

Last updated: **2026-09-21** (vehicle slice accepted; multiplayer next session)

This is the milestone log. For the current implementation and next-session
instructions, use `docs/HANDOFF.md`; older test counts below record their
original milestone rather than the latest full suite.

Legend: ✅ done & verified · 🟡 built but not verified on device · ⬜ not started · 🚫 blocked

---

## Phase 0 — Investigation

- ✅ Locate authoritative Demon repo (`Aerocatia/demon`; `demon-old`/`demon-rust` superseded)
- ✅ Inspect Demon source (9,780 LOC; no renderer/input/netcode)
- ✅ Survey broader Halo CE RE projects (`halo-re/halo` + 6 forks, TiaraCE, OpenH2, halocea)
- ✅ Quantify how much of Halo Trial is reimplemented → **none of the game; support systems only**
- ✅ Determine multiplayer status → **0% reimplemented anywhere**
- ✅ Identify required original files → §7 of investigation
- ✅ Identify legitimate source → **no live official source exists**; IA preservation copy documented honestly
- ✅ Build Demon on CachyOS → **succeeds** (PE32 i386 DLL, 44/44 targets)
- ✅ Determine minimum path to ARM64 Android → **new engine; existing code is unportable**
- ✅ Set up Android NDK/SDK toolchain
- ✅ `docs/ANDROID_PORT_INVESTIGATION.md`
- ✅ `docs/BUILD_ENVIRONMENT.md`

## Phase 1 — Android proof of concept

- ✅ Clean platform abstraction (`engine` / `gfx` / `platform`; engine has zero platform includes)
- ✅ Portable engine core + 12 host unit tests (all passing)
- ✅ Vulkan backend: instance → surface → device → swapchain → render pass → pipeline → present
- ✅ SPIR-V shaders compiled at build time via NDK `glslc`, embedded in the binary
- ✅ Android NativeActivity layer (no Java/Kotlin at all; `hasCode="false"`)
- ✅ Touch + gamepad + BACK routed through the platform boundary into the engine
- ✅ Gradle build producing arm64-v8a-only APK (61 KB)
- ✅ `scripts/verify.sh` — 10 automated build/artifact checks, all passing
- ✅ **APK installs on device** — sideloaded on S24+ over Tailscale, 2026-09-18
- ✅ **APK launches / Vulkan initializes on S24+** — magenta frame presented; Adreno swapchain/present works
- ✅ **Touch changes render state on device** — walked Blood Gulch on S24+
- 🟡 **Gamepad input on device** — *not yet confirmed*
- 🟡 **Clean exit via BACK** — *not yet confirmed*
- 🟡 **Stage-2 `mmap(0x40440000)` probe result** — *pending: logcat from our own process only*

Run `scripts/device_test.sh` with the S24+ connected to convert remaining 🟡.

## Phase 2 — Asset pipeline

- ✅ Host-side Trial cache parser + BSP extractor (`htainfo`) against real `bloodgulch.map`
- ✅ Host renderer draws Blood Gulch geometry (`htaview`)
- ✅ On-device import: `SetupActivity` document picker copies the user's map into app-private storage (no All-files access required)
- ✅ Parse tag table on device and render Blood Gulch — S24+ walk-around 2026-09-18

## Phase 3 — Renderer

- ✅ Blood Gulch BSP geometry on device (untextured slice, then textured)
- ✅ Textures + lightmaps — `bitm` decode from `bitmaps.map`, `senv` base map, 2× lightmap multiply. Host `htaview` and S24+ (2026-09-18) both show orange canyon, sand, red-base markings.
- ✅ Shader/material translation (minimal) — skip sky-portal `light black`, additive chicago lights/teleporters, alpha for other transparent; sky model + scenery/vehicles instanced from scenario

## Phase 4 — Gameplay

- ✅ Pawn physics from Trial tags (`matg` player info + `cyborg_mp`): run 2.25, accel, jump 0.07/tick, camera 0.62, radius 0.2, 45° slope
- ✅ Structure **collision BSP** for walking/hitscan (~5940 tris vs render mesh)
- ✅ Pill depenetration vs steep BSP faces (radius 0.2 cylinder + inbound-velocity cancel). S24+: pylons stop you; **walk-off the red-base pad works** (ledge lips skipped; no-ground falls instead of sliding back).
- ✅ Scenery/vehicle `coll` tags instanced onto the collision mesh (19+28 on Blood Gulch). S24+ walk-around after this drop. Shrubs without a coll tag still ghost.
- ✅ **Animated first-person weapon from Trial tags** — hands (`matg` → FP interface) +
  gun (`weap+0x45C`) skinned on the weapon's 42-node `antr` (`weap+0x46C`). Right hand,
  lower-right, barrel forward; idle / ready / firing / reload / melee all play. Verified
  in the host offscreen renderer (`htaview --fp`); the fabricated `fp_offset` is gone —
  the tag's own value is (0,0,0) and the hold comes from `frame gun` hanging off
  `frame r wriste`.
- ✅ Eleven playable weapons with tagged viewmodels, animation and original
  sounds; hitscan and object projectiles, impacts, grenades, damage, death and
  respawn. See `docs/HANDOFF.md` for remaining gaps.
- ✅ Touch HUD: stick, fire, jump, crouch, weapon controls and driving controls.
- ✅ Drivable human Warthogs with tag-based handling, wheel animation, crest
  airtime and planar collision response. Owner accepted this slice for now;
  exact behavior of the last collision release remains without a separate
  phone report. Full verification: 57/57 at `8f6ecca`.

## Phase 5 — Multiplayer (the product)

- ✅ **Scope decided (2026-09-18): our own protocol.** Transport stays behind an
  interface so wire-compatibility with real Halo servers can be attempted later
  without rearchitecting. See Decisions.
- ⬜ Implementation begins when the owner prompts next session. Start with a
  two-instance connection and remote-player visibility slice; see `HANDOFF.md`.
- ⬜ Two clients connect
- ⬜ Players see each other
- ⬜ Shooting / hit registration over network
- ⬜ Death / respawn
- ⬜ Teams / score
- ⬜ Vehicles

---

## Blockers

### 1. ~~Netcode scope decision~~ — **RESOLVED 2026-09-18**
Decided: **our own protocol** (investigation §5.2 option A), with the transport
behind an interface so option B remains reachable later. This removes the
project's largest scope risk: no reproduction of the encrypted Halo wire format
(3DES/DES-CBC, Diffie-Hellman, HMAC-SHA1) is required.

### 2. No portable foundation exists — *accepted, design decided*
Every RE project is a 32-bit x86 binary patcher. Mitigation: write a new engine,
reuse Invader (GPLv3) for assets and Demon's struct definitions for layouts.
Investigation §4.

### 3. halo-re's 327k lines are unlicensed — *accepted, do not use*
No licence file = all rights reserved. Usable as documentation only, never as code.

### 4. Current Demon depends on leaked material — *accepted, do not use*
`halo_cache_symbols.exe` comes from the Dec 2024 unauthorised Digsite leak.
Use `demon-old`'s legitimate Trial targeting and Demon's public struct definitions instead.

### 5. ~~First on-device map load~~ — **RESOLVED 2026-09-18**
`SetupActivity` uses the system document picker to copy the owner's map into
app-private storage. Blood Gulch has since rendered and played on the S24+.

### 6. ~~No Trial assets present~~ — **RESOLVED 2026-09-18**
The owner supplied their own `HaloTrialSetup.exe`. Extracted to
`~/halo-trial-data/` — **outside the repository**. `.gitignore` prevents any
game data from being committed; the APK bundles nothing.

### 7. ~~Textures are not sampled yet~~ — **RESOLVED 2026-09-18 (host + S24+)**
Pixel bytes live in `bitmaps.map` (BitmapData `external` flag), not in
`bloodgulch.map`. Decoder covers DXT1/3/5 and the 16/32-bit formats the Trial
uses. S24+ screenshots: red base, canyon rock, sand paths, lightmaps. Remaining
holes in the base interior are untranslated sky/transparent shaders, not missing
geometry.

---

## Decisions

| Date | Decision | Rationale |
|---|---|---|
| 2026-09-18 | **Netcode: our own protocol**, transport behind an interface | Avoids bit-exact RE of a partially-documented encrypted protocol (Xbox-only docs). Keeps option B reachable. Investigation §5.2. |
| 2026-09-18 | **No new engine built on Demon or halo-re** | Both unportable (measured) and halo-re is unlicensed. Investigation §4, §7. |
| 2026-09-18 | **Vulkan only, no GLES3 path** | Target is the S24+; a second backend doubles renderer work for no gain. Investigation §6. |
| 2026-09-18 | **Host left un-upgraded** (~425 packages behind) | Avoid a 436-package kernel upgrade on a daily driver. Fix conflicts surgically instead. |
| 2026-09-18 | **Guns from Trial tags, right-handed + animated + original sounds** | Owner: hold like CE (right hand), play FP `antr`, play `snd!`/`effe` from `sounds.map`. No synthesized gunshots, no mirrored viewmodel. Netcode waits until a local shot looks and sounds like a gun. |
| 2026-09-18 | **Skin the viewmodel on the CPU, not the GPU** | 3200 verts × 2 influences per frame is negligible, and it keeps the Vulkan side to one vertex-buffer write instead of a new pipeline, descriptor layout and bone UBO. Revisit only if a device profile says so. |
| 2026-09-18 | **Animation quaternions are conjugated on read** | Halo stores node rotations in the opposite sense to the child→parent convention the transform algebra composes in. Fixed at the two read sites rather than by inverting the composition, so `hta_xf_*` stays standard quaternion algebra. |

## Test inventory

| Test | How | Status |
|---|---|---|
| Host engine builds | `cmake --build build-host` | ✅ |
| Engine unit tests (12) | `./build-host/test_engine` | ✅ 12/12 |
| Android Gradle build | `gradle :app:assembleDebug` | ✅ |
| APK arm64-only, correct exports, links Vulkan | `scripts/verify.sh` | ✅ 10/10 |
| Demon cross-build (reference) | mingw32 toolchain | ✅ 44/44 |
| Camera/projection math | `./build-host/test_camera` | ✅ 23/23 |
| Player + collision | `./build-host/test_player` | ✅ 45/45 (wall pill + walk-off ledge) |
| Real Trial data parse + extract | `HTA_MAP=... scripts/verify.sh` | ✅ 5/5 |
| Offscreen render draws geometry | `scripts/verify.sh` | ✅ |
| FP animation graph + skinned viewmodel | `./build-host/test_anim $HTA_MAP` | ✅ 46/46 |
| FP viewmodel renders | `htaview --fp idle` | ✅ |
| **Full suite** | `HTA_MAP=... scripts/verify.sh` | ✅ **31/31** |
| APK installs / launches / Vulkan presents | S24+ sideload 2026-09-18 | ✅ |
| APK loads map / walks Blood Gulch | S24+ 2026-09-18 | ✅ untextured, then landscape-fixed |
| Host textured Blood Gulch (`htaview` + bitmaps.map) | 2026-09-18 | ✅ 31 unique textures |
| Device textured Blood Gulch | S24+ screenshots 2026-09-18 | ✅ |
| Sky + transparent + scenery/vehicles | host spawn view 2026-09-18; device pending | 🟡 |
