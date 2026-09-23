# Open Halo Project (halo-trial-android)

A native ARM64 Android engine for the free **Halo: Combat Evolved Trial**,
playing **Blood Gulch**. C, Vulkan and AAudio, with no engine dependencies. It
reads **your own copy** of the Trial and takes every value it can from the
original tags: weapons, vehicles, sounds, the HUD, the menu, the announcer.

**This repository contains no Halo files, and never will.** You need your own
copy of the Trial installer (`HaloTrialSetup.exe` / `lotrialsetup1.exe`).

## What it plays

- The Trial's own main menu from `ui.map`: the ring, the space sky, the HALO
  logo and the title theme.
- Blood Gulch with lightmaps, detail maps and its real sky; eleven weapons
  with their models, animations, sounds and HUD; grenades; powerups.
- Every vehicle: Warthog (driver, gunner, passenger), Scorpion, Ghost and
  Banshee.
- Singleplayer against 0-7 bots: **Slayer, Team Slayer and Capture the Flag**,
  with team colours and the Trial's own announcer.
- LAN and direct-IP multiplayer (Slayer), host-authoritative over UDP.

Not yet: team modes over LAN, Oddball, King of the Hill, Race, the campaign.
See [`docs/HANDOFF.md`](docs/HANDOFF.md) for the full state.

## Play it

You need an ARM64 Android phone with Vulkan (Android 8.0+) and a PC to
unpack the Trial installer.

1. **Get the Trial's maps.** The installer (`HaloTrialSetup.exe`, sometimes
   named `lotrialsetup1.exe`) is a cabinet archive, so you don't need to install
   anything. Extract the four maps with [7-Zip](https://www.7-zip.org/):

   ```sh
   7z e lotrialsetup1.exe maps/bloodgulch.map maps/bitmaps.map maps/sounds.map maps/ui.map
   ```

   On Windows, open the installer in 7-Zip and drag those four files out of
   its `maps` folder. (Installing the Trial works too: the same files are in
   the installed game's `maps` folder.)
2. **Copy those four files to your phone**, for example into `Download`.
3. **Install the APK.** Download `halo-trial-guest.apk` from this
   repository's Releases page, or build it yourself (below). It contains no
   Halo data.
4. **Open the app and pick each file** in the setup screen: `bloodgulch.map`,
   then `bitmaps.map` (textures), `sounds.map` (audio) and `ui.map` (the main
   menu). The app copies them into its own storage, and you only do this once.

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
- Halo is a trademark of Microsoft. This is an unaffiliated fan project.
- Our code is GPLv3 (compatible with Invader and Demon).

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
