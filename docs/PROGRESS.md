# Progress

Last updated: **2026-09-18**

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
- 🟡 **APK installs on device** — *not yet run: no device attached to this host*
- 🟡 **APK launches / Vulkan initializes on S24+** — *pending device*
- 🟡 **Touch changes render state on device** — *pending device*
- 🟡 **Gamepad input on device** — *pending device*
- 🟡 **Clean exit via BACK** — *pending device*
- 🟡 **Stage-2 `mmap(0x40440000)` probe result** — *pending device; code is in the APK and logs its answer*

Run `scripts/device_test.sh` with the S24+ connected to convert all 🟡 above.

## Phase 2 — Asset pipeline

- ⬜ Build Invader on CachyOS
- ⬜ Verify Invader round-trips a user-supplied Trial `bloodgulch.map` (`CACHE_FILE_DEMO`)
- ⬜ On-device asset import flow (user supplies their own Trial copy)
- ⬜ Parse tag table on device; log tag count; compare against desktop Invader

## Phase 3 — Renderer

- ⬜ Blood Gulch BSP geometry, untextured, fly-cam
- ⬜ Textures + lightmaps
- ⬜ Shader/material translation

## Phase 4 — Gameplay

- ⬜ Collision BSP + player movement
- ⬜ Weapons, projectiles, damage, HUD

## Phase 5 — Multiplayer (the product)

- ✅ **Scope decided (2026-09-18): our own protocol.** Transport stays behind an
  interface so wire-compatibility with real Halo servers can be attempted later
  without rearchitecting. See Decisions.
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

### 5. No device attached to the build host
All on-device milestones are unverifiable here. Not a real blocker — the owner
has an S24+; `scripts/device_test.sh` is ready.

### 6. No Trial assets present (deliberately)
Phase 2 onward needs the owner's own Trial copy. Nothing proprietary will be
committed or bundled.

---

## Decisions

| Date | Decision | Rationale |
|---|---|---|
| 2026-09-18 | **Netcode: our own protocol**, transport behind an interface | Avoids bit-exact RE of a partially-documented encrypted protocol (Xbox-only docs). Keeps option B reachable. Investigation §5.2. |
| 2026-09-18 | **No new engine built on Demon or halo-re** | Both unportable (measured) and halo-re is unlicensed. Investigation §4, §7. |
| 2026-09-18 | **Vulkan only, no GLES3 path** | Target is the S24+; a second backend doubles renderer work for no gain. Investigation §6. |
| 2026-09-18 | **Host left un-upgraded** (~425 packages behind) | Avoid a 436-package kernel upgrade on a daily driver. Fix conflicts surgically instead. |

## Test inventory

| Test | How | Status |
|---|---|---|
| Host engine builds | `cmake --build build-host` | ✅ |
| Engine unit tests (12) | `./build-host/test_engine` | ✅ 12/12 |
| Android Gradle build | `gradle :app:assembleDebug` | ✅ |
| APK arm64-only, correct exports, links Vulkan | `scripts/verify.sh` | ✅ 10/10 |
| Demon cross-build (reference) | mingw32 toolchain | ✅ 44/44 |
| APK installs / launches / renders / input / exits | `scripts/device_test.sh` | 🟡 pending device |
