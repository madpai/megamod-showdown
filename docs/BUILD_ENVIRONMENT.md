# Build Environment

Recorded **2026-09-18** on the development host. All versions captured from the live system.

## Host

| Item | Value |
|---|---|
| OS | CachyOS (Arch-based, rolling) — `BUILD_ID=rolling` |
| Kernel | `7.2.0-1-cachyos` |
| Arch | x86_64 |
| CPU | AMD Ryzen 7 5700X (8C/16T) |
| RAM | 62 GiB |
| Free space (/home) | ~1.8 TB |
| Shell | /bin/fish (build commands written POSIX-portable) |

## Toolchain (host / desktop builds)

| Tool | Version | Source |
|---|---|---|
| git | 2.55.0 | pacman |
| cmake | 4.4.3 | pacman |
| ninja | 1.13.2 | pacman |
| clang/LLVM | 22.1.8 | pacman |
| gcc | 20260810 | pacman |
| python | 3.14.7 | pacman |
| 7z | present | pacman |

## Cross-compile: 32-bit Windows (to build Demon for inspection)

| Tool | Version |
|---|---|
| i686-w64-mingw32-gcc | 16.2.0 |
| mingw-w64-binutils | 2.47-1 |
| mingw-w64-crt / headers | 14.0.0-1 |

Toolchain file used: `scratch/mingw32.cmake` (also copied to `toolchains/mingw32.cmake`).

## Android

| Item | Value |
|---|---|
| SDK root | `~/android/sdk` (2.7 GB) |
| cmdline-tools | 13114758 |
| NDK | **28.0.13004108** (r28) |
| NDK clang | 19.0.0 (Android build 12896553), target `aarch64-unknown-linux-android24` |
| Platform | android-35 |
| Build-tools | 35.0.0 |
| platform-tools (adb) | r37.0.1 |
| NDK-bundled CMake | 3.31.5 |
| JDK | 17.0.19
 2026-04-21 (jdk17-openjdk) |
| Gradle | **not installed** — using the Gradle **wrapper** per project (preferred: pins version in-repo) |

**Verified capabilities:**
- NDK produces `ELF 64-bit LSB pie executable, ARM aarch64 … for Android 24` ✅
- `vulkan/vulkan.h` present in NDK sysroot ✅
- `GLES3/gl32.h` present in NDK sysroot ✅
- `sources/android/native_app_glue` present ✅

## Environment variables

Add to shell profile (fish syntax, since the host shell is fish):

```fish
set -gx ANDROID_HOME   $HOME/android/sdk
set -gx ANDROID_NDK_HOME $HOME/android/sdk/ndk/28.0.13004108
set -gx JAVA_HOME      /usr/lib/jvm/java-17-openjdk
fish_add_path $ANDROID_HOME/platform-tools
fish_add_path $JAVA_HOME/bin
```

POSIX equivalent is in `scripts/env.sh`.

## Target device

| Item | Value |
|---|---|
| Device | Samsung Galaxy S24+ (primary test target) |
| ABI | `arm64-v8a` only |
| Graphics | Vulkan (primary). GLES3 not planned — see investigation §6 |
| minSdk | 24 |
| targetSdk | 35 |
| Root required | **No** |

## Packages installed during setup (and why)

| Package | Reason |
|---|---|
| `cmake`, `ninja` | build system for every component |
| `jsoncpp` | **repair**: the initial `pacman -Sy` created a partial-upgrade state and the new `cmake` needed `libjsoncpp.so.27`. Targeted single-package fix. |
| `mingw-w64-{gcc,binutils,crt,headers}` | required to build Demon (32-bit Windows DLL) for inspection |
| `jdk17-openjdk` | required by Android SDK tooling / Gradle |
| `unzip` | extracting SDK archives |

Nothing else was installed. No unrelated project on this machine was modified.

## Known environment caveat

The host is **~425 packages behind** on updates. A full `pacman -Syu` was **deliberately not run** — it includes kernel updates on a daily-driver desktop and is out of scope for this task without explicit approval. The one partial-upgrade breakage it caused (`cmake` ↔ `jsoncpp`) was fixed surgically. **Decision 2026-09-18: leave the host un-upgraded.** Further partial-upgrade
conflicts are to be fixed surgically, one package at a time, rather than by a full `-Syu`.

## Reproducing

```sh
# host tools
sudo pacman -S --needed cmake ninja clang git python jdk17-openjdk unzip \
                        mingw-w64-gcc mingw-w64-binutils mingw-w64-crt mingw-w64-headers

# android sdk/ndk
mkdir -p ~/android/sdk && cd ~/android
curl -LO https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip
# unzip into ~/android/sdk/cmdline-tools/latest, then:
sdkmanager --sdk_root=$HOME/android/sdk \
  "platform-tools" "platforms;android-35" "build-tools;35.0.0" \
  "ndk;28.0.13004108" "cmake;3.31.5"
```
