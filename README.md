<div align="center">

# MEGAMOD SHOWDOWN

*A fork of the Open Halo Project: the same native Android engine, playing
maps, characters and weapons imported from other games through
[Open Asset Lab](https://github.com/madpai/open-asset-lab). Open Halo
itself stays strictly Halo. No game content is included in this
repository.*

## Built on: Open Halo Project

**Halo: Combat Evolved on Android, running natively.** A from-scratch engine that plays the
free Halo Trial's **Blood Gulch** on your phone, built from your own copy of the game.

[![Release](https://img.shields.io/github/v/release/madpai/open-halo-project?label=download&color=2b6cf6)](https://github.com/madpai/open-halo-project/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Android%20arm64-3ddc84)
![Renderer](https://img.shields.io/badge/renderer-Vulkan-ac162c)
![Language](https://img.shields.io/badge/written%20in-C-555555)

<img src="https://github.com/madpai/open-halo-project/releases/download/media/main-menu.jpg" alt="The Trial's own main menu: the ring, the HALO logo and the menu, rendered by this engine" width="100%">

<sub>The main menu, drawn from the Trial's own <code>ui.map</code>: the ring, the space sky, the logo and the title theme.</sub>

</div>

---

This is **not** an emulator, a wrapper, or a port of the original executable. It's a new engine
in C with Vulkan and AAudio that reads the Trial's map files directly and takes every number it
can from the original tags: weapon damage, rate of fire, vehicle seats, sounds, HUD layout, even
the announcer's lines and the kill-feed wording. When a value isn't in the data, the project
says so in a public ledger of invented constants.

**The repository contains no Halo files, and never will.** You bring your own Trial installer.

## Screenshots

<table>
  <tr>
    <td width="50%"><img src="https://github.com/madpai/open-halo-project/releases/download/media/first-person.jpg" alt="First person with the assault rifle in Blood Gulch"></td>
    <td width="50%"><img src="https://github.com/madpai/open-halo-project/releases/download/media/team-slayer.jpg" alt="Team Slayer: a red player firing at three blue players on a ridge"></td>
  </tr>
  <tr>
    <td><sub><b>Blood Gulch in first person.</b> The assault rifle's model, animations, ammo counter, shield and motion tracker all come from the Trial's tags.</sub></td>
    <td><sub><b>Team Slayer.</b> Bots in red and blue armour, coloured through the cyborg shader's own change-colour mask.</sub></td>
  </tr>
  <tr>
    <td><img src="https://github.com/madpai/open-halo-project/releases/download/media/ctf-flag-run.jpg" alt="Capture the Flag: a red player running blue's flag home"></td>
    <td><img src="https://github.com/madpai/open-halo-project/releases/download/media/ctf-blue-base.jpg" alt="Blue's flag on its stand inside the blue base"></td>
  </tr>
  <tr>
    <td><sub><b>Capture the Flag.</b> A red bot runs blue's flag home, with a Warthog and a Ghost behind.</sub></td>
    <td><sub><b>The flag stand.</b> The pole comes from the flag's model and the cloth from its widget tag, on the stand the map defines.</sub></td>
  </tr>
  <tr>
    <td><img src="https://github.com/madpai/open-halo-project/releases/download/media/warthog.jpg" alt="Driving a Warthog down a dirt track"></td>
    <td><img src="https://github.com/madpai/open-halo-project/releases/download/media/ghost.jpg" alt="A Ghost beside the blue base"></td>
  </tr>
  <tr>
    <td><sub><b>The Warthog.</b> Driver, gunner and passenger seats, with the chaingun spinning up.</sub></td>
    <td><sub><b>The Ghost.</b> All 28 of Blood Gulch's vehicles: Warthogs, Ghosts, Scorpions and Banshees.</sub></td>
  </tr>
</table>

<sub>All screenshots are rendered by this project's own offscreen tools (<code>htamenu</code>, <code>htaview</code>, <code>htamatch</code>) from the owner's Trial data.</sub>

## Features

| | |
|---|---|
| 🎮 **Game types** | Slayer, Team Slayer and Capture the Flag against 0–7 bots, with the Trial's announcer ("Red team has the flag!") |
| 🚙 **Vehicles** | Warthog (driver, gunner, passenger), Scorpion, Ghost, Banshee: every seat and every gun; destructible hulls, momentum, blasts that throw them; bots drive the Warthog, Ghost and Scorpion on paths wide enough for each, steer round parked cars, and crew each other's guns |
| 🔫 **Weapons** | Nine weapons (assault rifle, pistol, shotgun, sniper, rocket launcher, flamethrower, plasma rifle, plasma pistol, needler) with their first-person models, animations, sounds, HUD and tracers; grenades; powerups; dropped guns |
| 🤖 **Bots** | They walk a navigation grid built from the map's collision, pick up weapons, fight, attack and defend flags, and chase the carrier |
| 🌐 **Multiplayer** | LAN discovery and direct-IP play (Slayer, Team Slayer, CTF), host-authoritative over UDP |
| 🖥️ **Menus** | The Trial's own main menu and its words, with a touch overlay for match settings |
| 🎨 **Rendering** | Lightmaps, detail maps, the real multi-layer sky, team colours, flag cloth, contrails, decals, particles |

**Still to come:** Oddball, King of the Hill, Race, bots that fly the Banshee, and the
campaign. See [`docs/HANDOFF.md`](docs/HANDOFF.md) for the full picture.

## Play it

You need an **ARM64 Android phone with Vulkan** (Android 8.0 or later) and a PC to unpack the
Trial installer.

1. **Get the Trial's maps.** The installer (`HaloTrialSetup.exe`, sometimes named
   `lotrialsetup1.exe`) is a cabinet archive, so you don't need to install anything. Extract four
   files with [7-Zip](https://www.7-zip.org/):

   ```sh
   7z e lotrialsetup1.exe maps/bloodgulch.map maps/bitmaps.map maps/sounds.map maps/ui.map
   ```

   On Windows, open the installer in 7-Zip and drag those four files out of its `maps` folder.
2. **Copy the four maps to your phone**, for example into `Download`.
3. **Install the APK:** [`halo-trial-guest.apk` from the latest release](https://github.com/madpai/open-halo-project/releases/latest).
   It contains no Halo data.
4. **Open the app and pick each file** in the setup screen: `bloodgulch.map`, `bitmaps.map`
   (textures), `sounds.map` (audio) and `ui.map` (the main menu). You only do this once.

## Build it

On Linux (the project is developed on Arch); macOS should work the same way.

```sh
# host tools
sudo pacman -S --needed cmake ninja clang git python jdk17-openjdk unzip   # or your distro's equivalents

# Android SDK pieces (sdkmanager from Android's command-line tools)
sdkmanager --sdk_root=$HOME/android/sdk \
  "platform-tools" "platforms;android-35" "build-tools;35.0.0" \
  "ndk;28.0.13004108" "cmake;3.31.5"
export ANDROID_HOME=$HOME/android/sdk

# the APK -- the Gradle wrapper downloads Gradle 8.9 itself
cd android && ./gradlew assembleDebug
# -> android/app/build/outputs/apk/debug/app-debug.apk
```

The desktop build runs the tests and the offscreen tools (`htaview`,
`htamatch`, `htamenu`). The tests that need real data take your own map:

```sh
cmake -S . -B build-host -G Ninja && cmake --build build-host
./build-host/test_game /path/to/your/maps/bloodgulch.map
HTA_MAP=/path/to/your/maps/bloodgulch.map scripts/verify.sh   # the full gate
```

Exact toolchain versions are in
[`docs/BUILD_ENVIRONMENT.md`](docs/BUILD_ENVIRONMENT.md).

## Working on it

**Start with [`docs/HANDOFF.md`](docs/HANDOFF.md).** It covers the build and test
loop, what works, what's next, how to read Halo's tags, and the traps that
have already cost a session. [`docs/JOURNAL.md`](docs/JOURNAL.md) is the
session history. Search it by symptom.

## Legal

- **No Halo assets, executables or DLLs are ever committed here**, and no
  APK built from this repository carries any. Every player supplies their
  own Trial copy. `verify.sh` checks that the shareable APK contains no maps.
- This project does **not** use the December 2024 Halo "Digsite" leak
  material that the current upstream Demon depends on.
- No DRM is involved and none is circumvented.
- The screenshots above are hosted on a release, not in git, so the repository's
  history carries no Halo imagery either.
- Our code is [GPLv3](LICENSE) (compatible with Invader and Demon).

## Docs

| Doc | Contents |
|---|---|
| [HANDOFF.md](docs/HANDOFF.md) | **Start here.** Current state, the loop, tag discipline, traps, invented constants |
| [JOURNAL.md](docs/JOURNAL.md) | Every session, newest first. The *why*. Search it by symptom |
| [BUILD_ENVIRONMENT.md](docs/BUILD_ENVIRONMENT.md) | Exact toolchain versions |
| [BLOOD_GULCH_ASSETS.md](docs/BLOOD_GULCH_ASSETS.md) | What is in the map |
| [INVADER_ASSET_PIPELINE.md](docs/INVADER_ASSET_PIPELINE.md) | How Invader's tag definitions are used |
| [ANDROID_PORT_INVESTIGATION.md](docs/ANDROID_PORT_INVESTIGATION.md) | The original feasibility study |
| [NETWORK_ARCHITECTURE.md](docs/NETWORK_ARCHITECTURE.md) | Engine audit and native multiplayer design |
| [NETWORK_PROGRESS.md](docs/NETWORK_PROGRESS.md) | Verified LAN slice, commands and remaining tests |
| [PROGRESS.md](docs/PROGRESS.md) | Early milestone log |

---

<div align="center"><sub>Halo is a trademark of Microsoft. This is an unaffiliated, non-commercial fan project
that runs only on data the player already owns.</sub></div>
