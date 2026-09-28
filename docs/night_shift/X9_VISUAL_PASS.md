# X9 visual production checkpoint

## Baseline, before edits (2026-09-28)

Heads: MegaMod `2073961` (remote had added X8 docs after the reported
`5ca7dbc`); OAL `963d564`. Both worktrees were clean. The X8 world is a
separate `night_shift_x8` package; the older world is a pinned fixture.

The S24+ playtest reports mechanics working and about 120 FPS in simple
scenes, but flat illumination, prototype wall treatment, little depth,
and an overbearing diagnostic HUD. The earlier X7 desktop load benchmark
measured a 2,724 triangle / 35 submesh world, 17 decoded textures and
17 models, a 566–578 ms load dominated by a 518 ms bot nav build.
The Android emulator ran at its 60 FPS vsync ceiling; those numbers are
historical measurements, not a new X9 run.

## Renderer architecture audit

- `gfx_vulkan.c` owns Vulkan on desktop and Android. The same forward
  pipelines draw opaque, alpha and additive world geometry, sky, rigid
  instances, dynamic meshes, the viewmodel and HUD. An optional HDR/post
  path supports tonemapping, bloom and FXAA; the direct path draws to the
  presentation target. Depth testing is enabled for world geometry.
- Vertices are 40 bytes: position, normal, base UV and lightmap UV. Models
  use the same mesh format. The vertex shader passes world position, but
  rigid instances bake their model transform into the pushed matrix, so
  this varying is model space for instances.
- A per-submesh descriptor set has five texture samplers: base, lightmap,
  two detail maps and a multipurpose map. The scene camera and broad light
  fit in push constants. A 48-byte per-frame uniform holds fog settings.
  A frame fence protects each persistently mapped uniform slot; swapchain
  acquire/present semaphores guard presentation. The viewmodel has its own
  camera/push setup, then the world camera is restored.
- Static imported BSPs use lightmaps, with a neutral fallback for original
  worlds. Scene-lit models use broad directional lighting and ambient.
  Night Shift authors no local illumination. Fog already exists as a cheap
  exponential-squared depth effect but is player-configured and may be off.
- Textures have CPU-generated mip chains, linear/trilinear filtering and
  optional anisotropy (feature checked). Each GPU mesh uploads its own
  copies of textures; X5 borrows pixel memory but has no GPU texture cache.
  This is a measured opportunity, not a reason to rewrite resource storage.
- Shader `.inl` files are compiled from GLSL sources. Both platforms use
  the same SPIR-V and explicit std140-compatible vec4 uniform layout. The
  swapchain prefers an UNORM target and offscreen capture uses UNORM; the
  optional composed path tonemaps, while the direct path has no authored
  exposure control. A wholesale color-space change would alter historical
  worlds, so X9 keeps their existing response.

The smallest visual path is an additive authored environment plus bounded
local forward lights, reusing the frame uniform and the existing fog path.
Night Shift's gameplay relays can own switchable light state: X8 already
replicates their active flags and restores them on late join.

## X9 implementation

- World entities schema 7 adds an authored environment and at most 32 placed
  point or spot lights. The renderer chooses the nearest eight active lights
  within range per frame. Uniform packing is explicit and shared between
  host and Android Vulkan. This remains a forward renderer; it has no extra
  render pass for lights. The spot cone and distance falloff are evaluated in
  the mesh shader.
- The environment sets ambient RGB, clear RGB, fog RGB, exponential-squared
  density and a clear-air start distance. The authored fog overrides the
  player's legacy fog preference for that world. Existing worlds retain their
  original rendering path.
- Assets schema 2 adds material emissive strength and roughness, both scalars.
  Old schema 1 materials retain the same semantics and are emitted unchanged
  by OAL. Emission changes the surface itself; an associated placed light is
  required to illuminate nearby geometry. Roughness changes local-light
  highlight width and strength. No normal map, shadow map or bloom pass was
  added. The existing mip and filtering pipeline remains in use.
- A light may name an existing relay. The host changes that relay through X7
  actions; X8's logical bitset sends its current value to joiners, including
  late joiners. Static light definitions come from matching packages; no new
  high-frequency protocol field exists.
- Night Shift X9 is `night_shift_x9`, depending on the new
  `nightshift.assets_x9` and `nightshift.facility_x9` libraries. X8's 74
  runtime objects, 23 spatial states, 11 logical flags, 40 host-only objects
  and 71 bindings are retained. Static rails, beams and trims add no runtime
  identity. The dock, AUX passage, security corridor, research wing, core and
  tunnel have authored light colors and pools. Auxiliary power switches on
  the dock and corridor fixtures; lockdown relays activate red light in the
  hall, core and tunnel. Procedural painted/concrete/metal surfaces replace
  the broad checker wall treatment in the X9 variant.
- Android hides coordinates, raw networking and FPS by default. Pause offers
  **SHOW DIAGNOSTICS** to opt in. Authored visual worlds use lower-opacity
  secondary touch controls and a smaller ammo readout. Fire and ability stay
  prominent. The release capture uses player presentation.

## Reproducible visual evidence

Local private comparison artifacts, deliberately outside the public Git
repository: `/home/commander/assetlab-private/x9_evidence/`.

Four identical offscreen viewpoints use `megamod-join --shot --shot-view`:

| View | `x,y,z,yaw,pitch` | Baseline | X9 |
|---|---|---|---|
| Dock | `-1,-10,1.05,1.57,0` | `before_dock.png` | `after_dock.png` |
| AUX passage | `-8,-10.4,1.05,3.14,0` | `before_aux.png` | `after_aux.png` |
| Security corridor | `0,-2.6,1.05,1.57,0` | `before_security.png` | `after_security.png` |
| Narrow corridor | `0,5.2,1.05,1.57,0` | `before_corridor.png` | `after_corridor.png` |

`before_montage.png` and `after_montage.png` place the four in the same order.
The before views show uniform checker walls and ambient readability; the X9
views show localized warm/red light, dark corners, layered wall/ceiling trim,
surface texture and deeper vanishing points. The powered dock capture
`after_dock_powered.png` shows the auxiliary power transformation. Phone
screenshots remain the final human visual review; offscreen screenshots do
not include the touch HUD.

The same fixed core camera was captured before lockdown as
`core_pre_lockdown.png` and after the host's core extraction as
`after_lockdown.png`. The latter shows the relay-powered emergency red pool
across the pedestal and floor; the former is cool and dim. Both are private
comparison artifacts in the evidence directory above.

## Limits and production choices

The eight-light selection is camera based, so a large object near the edge
of the selected set may change lighting as the camera moves. Night Shift's
small rooms fit this budget. Existing models still upload duplicate texture
copies; X9's texture count did not justify a cache rewrite. Static visual
trim is deliberately non-colliding and should not be used as navigation
geometry. Lights are world placements that may reference prefab-child
relays; X9 does not add a second prefab light-expansion path. There is no
dynamic shadow casting; darkness, local falloff, fog,
material response and architectural layers provide depth within the phone
budget. Normal maps require tangents or derivative-space reconstruction and
were deferred after the screenshot comparison already improved materially.

## Measurements and final verification

The X9 world builds to 3,312 triangles and 39 world submeshes, versus X8's
2,724 triangles and 35 submeshes. It has 17 art textures, 17 materials,
17 models, 18 sounds and 26 authored lights. The package key for this build
is `cd8e683f`; it differs from the historical X8 key `cc52fc69`. The X8
world and asset package are built independently and remain unchanged.

The desktop offscreen dock comparison uses the same 240-frame, low-preset,
1,600×900 joiner viewpoint and NVIDIA RTX 3060 Ti. The loop is paced to
60 FPS to keep its network session on real time. `submit/fence` is wall time
around recording and submitting a frame, including a frame-slot fence; it
is **not** a GPU timer query. After a 30-frame warmup:

| World | Submit/fence mean | Max | Last-frame draws | GPU allocation |
|---|---:|---:|---:|---:|
| X8 baseline | 0.947 ms | 2.898 ms | 74 | 25.11 MiB |
| X9 | 0.985 ms | 2.954 ms | 78 | 25.82 MiB |

The observed low-preset difference is +0.038 ms, four draws and 0.71 MiB.
These are full-world differences, so local-light, fog and material costs are
not individually isolated. The camera selects at most eight lights and fog
is a single fragment expression. Shadow and bloom cost zero because neither
effect was added. At high preset the X9 joiner allocated 70.51 MiB; that is
not comparable to the low-preset X8 figure because high enables additional
scene/post targets.
The gameplay-route captures selected two lights at the AUX panel, five at
security, six after coolant, four in the lockdown core and five on the late
joiner's dock frame; they remained below the eight-light shader budget.

| Visual feature | Observed cost / limit | Interpretation |
|---|---|---|
| Base material response + emissive | included in the +0.038 ms full-world delta | one forward shader variant; no new texture or pass |
| Local lights | 26 authored, at most 8 evaluated per fragment | included in that same delta; no separate GPU query |
| Authored fog | one exp-squared fragment expression | included in that same delta; no separate pass |
| Shadows | 0 ms, 0 MiB | not implemented in X9 |
| New bloom | 0 ms, 0 MiB | not implemented; the old optional post path remains |

The headless Android emulator uses SwiftShader at 2,400×1,080. With the same
Master Chief spawn and no bots, X8 reported about 5.0–5.1 FPS and X9 about
4.5–4.9 FPS. This software GPU is unsuitable as a phone performance proxy;
both worlds render far below the emulator's display ceiling. X9 did load,
draw local light/fog/emissive surfaces, play audio and render the clean HUD.
The local capture is `emulator_x9_player_final.png`. The physical S24+ is
not attached to ADB in this session, so its 60-FPS target requires the
private APK playtest. The user-reported pre-X9 phone baseline is ~120 FPS.

The powered auxiliary route activated the host's `aux_power` relay. A fresh
joiner received that logical state and rendered the lit dock without replaying
the event. The powered capture is `after_dock_powered.png`; the joiner logs
are `scratch/x9_power_{host,join,late}.log` (local, ignored by Git). The X8
binary at commit `2073961` was also built separately and refused X9's assets
schema with `unknown or repeated field 'emissive'`; its binary is retained
privately at `x9_evidence/megamod-resources-x8`.

The X9 full route additionally uses D3 and D5 after security and coolant,
extracts the data core, and activates `lockdown` on the host. Its late joiner
receives `lockdown active` and the resulting closed D2, D3 and D5 doors in
the current state snapshot. The fixed core pre/post captures above show the
visible state change. No light-specific network messages or event replay
were added.

Verification outside the screenshot review: OAL's 202 unit tests passed;
MegaMod's 56 CTest tests passed; SPIR-V compilation and `spirv-val` passed;
`scripts/ndkcheck.sh` and the Android `assembleRelease` build passed. The
ASan/UBSan/LSan build passed the asset and world-entity tests, including the
new schema checks; existing asset fuzz cases exercised 4,000 libraries and
3,000 worlds, and the new schema-7 test mutates 1,000 inputs. The desktop
Vulkan render test passed on the NVIDIA GPU. The full Trial-data verification
gate passed **96/96**, including X8, X9, desktop Vulkan captures,
multiplayer and Android APK-content checks. OAL CI passed at `925cf51`;
MegaMod CI is checked after the engine push.

Physical S24+ results remain a user playtest item; no phone is attached to
ADB in this session. The private personal and asset-free guest APKs are
served from `scratch/serve-megamod/` at the established Tailscale sideload
page (`http://100.89.1.14:8733/`), together with SHA256SUMS. The personal
APK bundles the X9 map and its two libraries; the guest requires the same
packages from the player and contains no Trial data.

## Production assessment

The audit disproved two initial assumptions: Night Shift's distance fog path
already existed, but authored worlds could not set it; texture mipmaps and
linear filtering were already available. The largest visual gains therefore
came from authored ambient/local illumination, a more useful material
response, and content composition. The principal remaining weakness in the
captures is limited contact and structural detail in large rooms. If the
physical screenshots confirm that reading, the next presentation investment
should be more efficient OAL spatial authoring and reusable structural
detail, with a tightly scoped static occlusion/shadow technique considered
after its actual phone cost is measured. This is a recommendation, not X9
implementation work.
