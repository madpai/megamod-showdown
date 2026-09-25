# Engine architecture

The owner wants this engine to outlive Megamod: cross-platform, networked,
with real physics, and a starting point for other games. This is how it is
laid out today, what is already reusable, and the staged plan for the one
big piece that is not.

## The layers

```
 platform  ─ platform_android.c (NativeActivity, JNI, AAudio, touch)     desktop_sdl.c (SDL2 window, input, pacing)
    │
 app/game  ─ game.c (rules, units, events) brain.c nav.c view.c menu.c   world_fx.c (effects director)
    │
 engine    ─ player  vehicle  projectile  particle  contrail  rigid  props  gore  fx  weather  audio  vitals  ...
    │
 gfx       ─ gfx_vulkan.c (one renderer: Android swapchain, desktop swapchain, offscreen) + gfx_settings.c
 asset     ─ Halo tag cache, bitmaps, models, sounds; OALMAP/OALASSET (imported maps, characters, weapons)
 net       ─ protocol, replication, UDP session (host-authoritative)
```

Everything below the platform line is **C11 with no platform headers**. The
renderer includes Vulkan and nothing else; windowing arrives through
`hta_gfx_create_desktop`'s surface callback or an `ANativeWindow`.

## Reusable today, for any game

| Module | What a new game gets |
|---|---|
| `gfx/gfx_settings.*` | Presets Potato..Ultra, device suggestion, dynamic resolution, key=value persistence |
| `gfx/gfx_vulkan.c` | Forward renderer: lightmapped + lit meshes, skinned/dynamic meshes, instancing, sky, HUD; MSAA, render scale, bloom, ACES, FXAA, sharpen, fog; Android/desktop/offscreen |
| `engine/player.*` | Collision grid (static + moving `extra` + placed instances), rays, ground, depenetration, biped movement |
| `engine/rigid.*` | Rigid bodies (box, sphere): contacts, friction, torque, speculative contacts, sleeping, blasts |
| `engine/props.*` | Destructible props: health, materials, shattering, explosive chains, respawn, solid while whole |
| `engine/gore.*`, `engine/fx.*` | Gibs, blood and splats, sprite bursts, procedural effect atlas (no assets) |
| `engine/weather.*` | Rain, storm (lightning, thunder), snow, ash, sandstorm, with a roof map |
| `game/world_fx.*` | One object that turns game events into all of the above |
| `net/*` | UDP client/server, snapshots, interpolation, events |
| `platform/desktop_sdl.*` | Window, Vulkan surface, input (`hta_input`), frame pacing |

`src/tools/sandbox.c` is the template: about 500 lines that stand up a
window, a generated level, the player, destructibles, gibs, weather and
live video presets, with no game data. A new game starts by copying it.

## The platform contract

A platform provides, and the game consumes nothing else:

| Need | Desktop (today) | Android (today, inside platform_android.c) |
|---|---|---|
| Window + renderer | `hta_desktop_open` → `hta_gfx_create_desktop` | `start_gfx` → `hta_gfx_create_window_ex` |
| Input for a frame | `hta_desktop_poll` → `hta_input` | touch/JNI → `hta_player_input` + flags |
| Time and pacing | `hta_desktop_time`, `hta_desktop_pace` | `hta_time_seconds`, vsync |
| Settings file | caller's path | `video.cfg` beside the maps (SETTINGS writes it) |
| Audio out | *not yet* (SDL audio backend is stage 3) | `audio_android.c` (AAudio) feeding `engine/audio.c` |
| Files, maps | `fopen` | APK assets mapped, or app storage |
| Menus | *not yet* | Java overlay (`SetupActivity`, `GameActivity`) |

## The one big piece: the game loop

The match -- session state, the per-frame order of simulation, networking
and drawing -- lives in `platform_android.c` (7,600 lines), in
`hta_android` and `android_main`. Until it moves, a PC build of *Megamod*
means `htaplay` (a thin LAN client), not the same game.

Moving it blind would be the riskiest change this project could make: it
touches every feature, and only a phone can say whether it still plays.
So it moves in stages, each small enough to publish and test on the phone
before the next, each leaving Android working.

1. **State out.** Split `hta_android` into `hta_session` (everything about
   the match: game, vehicles, player, camera, effects, net) and what is
   Android's (app, window, JNI, touch). New `src/app/session.h`. The loop
   still lives in `platform_android.c`, now reaching `s->session.*`.
   *Device check:* a normal match; nothing should differ.
2. **Input in.** `hta_session_frame(session, dt, const hta_input *in)`
   takes the device-neutral input `desktop_sdl.h` already defines; Android
   fills it from touch and the Java HUD. *Device check:* every control,
   driving, the menus.
3. **Audio behind an interface.** `engine/audio.c` already mixes; add an
   `hta_audio_out` with AAudio and SDL backends. *Device check:* sounds,
   the flamethrower loop, music on the menu.
4. **Files behind an interface.** Map, bitmaps, sounds and packages opened
   through `hta_fs` (APK asset, app storage, desktop path). *Device check:*
   the personal APK's built-in maps and a picked `.oalmap`.
5. **The loop out.** `src/app/session.c` owns simulation and drawing;
   `platform_android.c` shrinks to lifecycle, JNI and input. *Device check:*
   a full LAN match, both phones.
6. **Megamod on PC.** `src/app/main_desktop.c`: the same session on
   `desktop_sdl`, with a small native menu (map, class, host/join) in place
   of the Java overlay. LAN play between a phone and a PC falls out of the
   shared protocol.

Stages 1 and 2 are mechanical and the right next step. Each is one
publish and one phone test. Do not merge stages to save a publish.

## What stays out of the engine

- Halo tag knowledge stays in `asset/` and in `game/` where it names tags.
  The generic modules above never reference a tag.
- Anything Megamod-specific (heroes, abilities, classes) stays in `game/`.
- Content never enters the repositories: not Halo's, not the Workshop's.
