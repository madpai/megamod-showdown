# Night Shift -- performance

Measured 2026-09-27. Desktop: Ryzen 7 5700X, the host build
(`build-host`, -O2). Emulator: AVD `megamod` (Android 14 x86_64, 2400x1080,
host GPU RTX 3060 Ti), the emulator APK carrying only Night Shift. No
benchmarking infrastructure was added: the numbers come from the engine's
own logs, `megamod-match`, and a throwaway bench (below) linked against
`libhta_engine.a`.

| What | Desktop | Emulator (Android host) |
|---|---|---|
| OAL `project build` (3 packages, 1.99 MB) | 1.5 s (Python; `budget` alone 1.5 s) | - |
| package set + world load incl. prefab expansion (19 instances -> 41 entities), 17 textures, 17 models, 18 sounds decoded | **7.0 ms** (bench, mean of 50) | - |
| X7 runtime load (dispatch index, handles, 19 movers' collision) | 1.2 ms | - |
| world geometry (BSP 2,724 tris, 35 submeshes) | 8.8 ms | 17.1 ms |
| **bot nav grid** (33,458 nodes, built though no bot plays) | **518 ms** | **608 ms** |
| whole world load in `megamod-match` / the app | 566-578 ms + start 29 ms | ~0.99 s from the menu's "match on night_shift" to "scripts loaded" |
| X7 step, dark annex (nothing running) | 0.43 us | - |
| X7 step, plant running (heartbeat + pump) | 0.49 us | - |
| X7 step, lockdown (klaxons, vents, pump, heartbeat) | 0.49 us | - |
| worst single step of the escalation (core -> lockdown cascade) | 1.8 us | - |
| host session tick, idle world, no players | 0.02 ms/frame | - |
| frame rate | not measurable on this desktop (NVIDIA Vulkan fails; lavapipe is a CPU rasterizer) | **60 fps**, steady (vsync cap; the desktop GPU's number, not a phone's) |
| audio | - | 1,666 sounds started, **0 dropped** over the session |

Under ASan+UBSan (the same bench): load 20.3 ms, steps 1.8 us -- the
relative costs hold.

## Findings

- **Nothing in Night Shift's content is slow.** 59 bindings, three
  oscillating movers and every cascade cost well under 2 us a step;
  X7 is not where a phone's frame goes.
- **The one obvious cost is the bot nav grid**: ~90% of the desktop world
  load and ~60% of the Android load, for a world hosted with 0 bots (E17).
- Renderer and audio showed no bottleneck at this scale (2,724 world
  triangles, 11 props, 19 movers, ~2 sounds a second at lockdown).
- Not measured: a real phone's frame time and thermals, a 4-player
  session's host tick on a phone. The owner's S24+ is the place for both.

## The bench (not committed)

`bench.c` loads `maps/night_shift.oalmap` through
`hta_external_map_load_with` with a directory package source, runs
`hta_went_load`, then steps the world 60,000 times dark, uses the aux
breaker, the console and the valve (`hta_went_use`), steps 60,000 times
running, uses the core socket, times each step until `lockdown` is
active, and steps 60,000 times in lockdown; everything is freed (clean
under LSan). It uses only public engine headers; rebuild with
`gcc -O2 -Isrc bench.c build-host/libhta_engine.a build-host/libhta_lua.a -lm`.
