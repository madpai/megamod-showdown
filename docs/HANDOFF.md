# Session handoff — Halo Trial Android

**Date:** 2026-09-18  
**Repo:** `/home/commander/projects/halo-trial-android`  
**Map data (not in git):** `/home/commander/halo-trial-data/extract/maps/` (`bloodgulch.map`, `bitmaps.map`)  
**Device:** Galaxy S24+, Tailscale node `100.68.201.52` (`node`)  
**Build host Tailscale:** `100.89.1.14`

Do **not** commit Trial `.map` files. The APK never bundles Halo assets.

---

## How to resume

```
cd /home/commander/projects/halo-trial-android
HTA_MAP=/home/commander/halo-trial-data/extract/maps/bloodgulch.map scripts/verify.sh
```

Sideload page (keep the Python server bound to Tailscale only):

**http://100.89.1.14:8731**

Server: `scripts/serve_poc.py --bind 100.89.1.14 --port 8731` serving  
`/tmp/claude-1000/-home-commander/dacf88c7-0fa5-4691-a530-83bafded3436/scratchpad/serve/`  
Screenshots land in that dir’s `uploads/` and are mirrored to `scratch/uploads/`.

If the server died:

```
python3 scripts/serve_poc.py \
  --root /tmp/claude-1000/-home-commander/dacf88c7-0fa5-4691-a530-83bafded3436/scratchpad/serve \
  --bind 100.89.1.14 --port 8731 \
  --mirror /home/commander/projects/halo-trial-android/scratch/uploads
```

Then copy a new APK over `halo-trial-poc.apk` and refresh `SHA256SUMS`.

---

## What this session finished

- Vulkan on S24+, Blood Gulch textured + lightmaps
- Sky portals, additive glass/lights, scenery + vehicles (triangle strips + regions)
- Spawn no longer falls through (collision rebind after realloc)
- COD-style HUD: stick, FIRE, JUMP, CROUCH (WindowManager overlay on NativeActivity)
- Aim while holding fire
- No walking up walls (upward faces + slide)
- **Pawn physics from Trial tags:** `matg` player info + `cyborg_mp`  
  run 2.25 wu/s, accel, jump 0.07/tick, cam 0.62, radius 0.2, 45° slope
- **Previous drop:** FP assault rifle (`weapons\assault rifle\fp\fp`), weap ROF 15/s, structure collision BSP (~2830 verts / 5940 tris)
- **This drop (pill vs BSP walls):**
  - Standing cylinder (radius 0.2, tag height) depenetrates steep collision faces
  - Deepest-hit per pass so a triangle in several grid cells cannot over-push
  - Inbound XY velocity is cancelled after a push — that was the “slight clip” (point query + sinking back in next frame)
  - Scenery/vehicles still have no colliders (walk through rocks and warthogs)

---

## What is still not Halo

- No first-person **animations** (static FP mesh)
- No real **projectiles** / tracers / ammo UI (hitscan + cooldown only)
- No weapon pickup; you spawn with the AR
- Vehicle `shader_model` textures still weak; some attachments may sit wrong
- Rocks/vehicles are **not** solid (only structure collision BSP)
- Multiplayer not started (by request)

---

## Suggested next session (after you test)

1. Walk into a **base wall / pylon** — you should stop at ~0.2 wu, not sink. Rocks and warthogs still ghost.
2. FP AR sits a bit low-left and untextured — tweak `weap.fp_offset` / viewmodel basis if needed
3. Then: scenery/vehicle colliders, projectile tags, ammo, or netcode (Phase 5)

---

## Key files

| Area | Path |
|---|---|
| Pawn physics parse | `src/asset/biped.c` |
| Movement | `src/engine/player.c` |
| Weapon + ROF | `src/asset/weapon.c`, `src/engine/gun.c` |
| Collision BSP | `src/asset/bsp.c` `hta_bsp_load_collision` |
| FP draw | `src/gfx/gfx_vulkan.c` viewmodel pass |
| HUD | `android/.../GameActivity.java` |
| Android glue | `src/platform/platform_android.c` |
