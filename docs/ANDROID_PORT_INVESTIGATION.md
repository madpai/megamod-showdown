# Halo Trial → Native Android (ARM64): Feasibility Investigation

**Date:** 2026-09-18
**Investigator:** lead RE/porting engineer (automated investigation on CachyOS x86_64)
**Status:** Investigation complete. **The originally proposed foundation (Demon) does not support the goal.** A revised path is recommended.

---

## 0. Executive summary

| Question | Finding |
|---|---|
| Is there an existing Halo Trial reimplementation we can port? | **No.** Every project found is an *in-process binary patcher* for a 32-bit x86 executable, not a portable engine. |
| Does any of it run without the original executable? | **No.** All require the original EXE/XBE at **link time and run time**. |
| Is Blood Gulch multiplayer implemented anywhere in open source? | **No.** Zero lines of Halo netcode exist in the Demon lineage. |
| Is native ARM64 Android feasible? | **Yes, but not as a "port."** Only as a **new engine** that consumes user-supplied Trial assets. |
| Is the user's actual goal (Blood Gulch MP on an S24+) achievable? | **Yes** — scope is unusually favourable (see §3). But it is a *from-scratch engine build*, ~12–24 months of work, not a port. |

**The single most important finding:** the phrase "Halo Trial reimplementation" in these projects means *"recompile individual functions and patch them back into the original Windows/Xbox binary, byte-behaviour-matched."* The deliverable of every such project is a **patched copy of the original 32-bit executable**. That artifact category cannot be made to run on ARM64 Android by any amount of porting, because the thing being produced *is the original x86 binary*.

---

## 1. Existing projects discovered

### 1.1 The Demon lineage (Aerocatia) — the project named in the brief

Three successive iterations, all by the same author. **All are DLL-injection patchers for 32-bit Windows.**

| Repo | Lang | LOC | Commits | Last push | Stars | License | State |
|---|---|---|---|---|---|---|---|
| [`Aerocatia/demon-old`](https://github.com/Aerocatia/demon-old) | C | 4,020 | 101 | 2025-03-06 | 18 | GPL-3.0 | **Superseded** (README points to successor) |
| [`Aerocatia/demon-rust`](https://github.com/Aerocatia/demon-rust) | Rust | 13,182 | 180 | 2025-06-28 | 8 | GPL-3.0 | **Superseded** |
| [`Aerocatia/demon`](https://github.com/Aerocatia/demon) | C | **9,780** | 155 | 2026-08-03 | 3 | GPL-3.0 | **Current/authoritative** ("3rd time unlucky") |

**Which is authoritative:** `Aerocatia/demon`. Confirmed by `demon-old`'s README notice and by push dates.

**Critical: the current `demon` no longer targets the Halo Trial at all.** Its README states it provides "C function replacements for `halo_cache_symbols.exe` (2020 digsite build)". Only the *superseded* `demon-old` targeted the retail Halo Trial. See §7 for why this matters legally.

### 1.2 The Halo CE Xbox decompilation project — the broader effort

| Repo | LOC (src) | Last push | Stars | License |
|---|---|---|---|---|
| [`halo-re/halo`](https://github.com/halo-re/halo) (nominal upstream) | 1,993 | **2023-10-09 (stale)** | 394 | **NONE** |
| [`stianeklund/halo`](https://github.com/stianeklund/halo) | **326,913** | 2026-09-18 | 21 | **NONE** |
| [`pastudan/halo`](https://github.com/pastudan/halo) | large | 2026-09-18 | 0 | **NONE** |
| `nickarcade/halo`, `scudo005/halo`, `USAvery/halo`, `bnunu/halo` | large | Sept 2026 | 0–6 | **NONE** |

Homepage: <https://blam.info/>. **The nominal upstream is abandoned (2023)**; real activity lives in personal forks, which are LLM-assisted lifting efforts (each carries `AGENTS.md`/`CLAUDE.md`).

`stianeklund/halo` advertises **85.65% decompiled, 5,831/6,808 functions**. Verified as genuine in volume — but see §4 for what that number actually means.

### 1.3 Others evaluated and rejected

- **[`wulfboy-95/TiaraCE`](https://github.com/wulfboy-95/TiaraCE)** — "cross-platform reimplementation of the Halo Custom Edition engine." **This is the architecture we want and it is dead.** 26 lines of code, last push **2018-03-06**, 0 stars, one-line README. An empty CMake skeleton. *Not a foundation.*
- **[`ronbrogan/OpenH2`](https://github.com/ronbrogan/OpenH2)** — Halo **2**, C#. Wrong game, wrong runtime.
- **[`surreptitiousresearch/halocea`](https://github.com/surreptitiousresearch/halocea)** — Halo CE *Anniversary* (Xbox 360, PowerPC). Wrong target.

### 1.4 Genuinely reusable assets (the valuable finds)

| Project | Why it matters | License |
|---|---|---|
| [`SnowyMouse/invader`](https://github.com/SnowyMouse/invader) | Cross-platform **C++** Halo CE map toolkit. **Confirmed `CACHE_FILE_DEMO` support** — it reads Trial maps. Actively maintained (2026-08-20). Builds on Linux. | **GPLv3** ✅ |
| [`Aerocatia/ringhopper-old`](https://github.com/Aerocatia/ringhopper-old) | Rust tag-structure library | check |
| [`keowu/gamespy`](https://github.com/keowu/gamespy) | **From-scratch RE of the GameSpy SDK 2000–2005, explicitly including Halo CE**, with papers + the GOA encryption algorithm. Directly relevant to a server browser. | **GPL-3.0** ✅ |
| [hllmn.net H1 System Link RE](https://hllmn.net/blog/2023-09-18_h1x-net/) | Documents Halo 1 LAN protocol: **3DES-CBC** (discovery), **DES-CBC** (in-game), **Diffie-Hellman** key exchange, **HMAC-SHA1** derivation. Partial. | writeup |
| `Aerocatia/demon*` structure/tag definitions | Struct layouts + function names, which the author explicitly declares **public information, no copyright claimed** | GPLv3 code ✅ |

---

## 2. What Demon actually implements (verified by reading code, not the README)

Measured LOC per subsystem in the current `Aerocatia/demon` (9,780 total):

```
1298 math        1203 ai         1152 cseries    1140 memory
 798 structures   705 sound       683 cache       610 scenario
 304 text         295 bitmaps     228 objects     193 saved_games
 183 main         179 game        163 tag_files   156 physics
 140 demon        111 interface    84 rasterizer   73 units
  36 input         36 render       10 items
```

**What this is:** tag/cache-file parsing, math, memory allocators, and struct definitions.

**What this is not:**
- `source/rasterizer/` (84 lines) and `source/render/` (36 lines) contain **header files only** — struct and enum declarations, **zero rendering implementation**.
- `source/input/` is 36 lines — a header.
- **There is no `network/` directory. Multiplayer networking is 0% implemented.**
- In the superseded `demon-old`, the file literally named `multiplayer/netcode.c` is **12 lines** and contains one unnamed arithmetic helper (`halo_unknown_00478d40`). The rest of `multiplayer/` is weighted random weapon-spawn selection.

### How it works (the architecture)

`source/demon/exe_functions.c` is a table of **hardcoded absolute addresses** into one specific EXE image:

```c
[_exe_function_cache_file_open]  = (void *)0x005173C0,
[_exe_function_hud_load]         = (void *)0x006220E0,
[_exe_function_system_milliseconds] = (void *)0x0054F1E0,
```

`source/demon/replacements.json` lists **217 functions** to overwrite at load time, each by absolute address. Graphics is `LoadLibraryA("d3d9.dll")` plus writes to absolute addresses:

```c
*(float *)(0x7123A8) = -0.000055; // DecalZBiasValue
```

**Answers to the brief's numbered questions:**

| # | Question | Answer |
|---|---|---|
| 1 | What does Demon implement? | Tag/cache parsing, math, memory. 217 functions replaced. No renderer, no input, no netcode. |
| 2 | How does it build? | CMake + **32-bit** mingw-w64 → `demon.dll`. **Verified building on this machine** (§8). |
| 3 | What is reverse engineered? | ~9.8k lines of support systems. Not the game loop, renderer, or netcode. |
| 4 | Requires original EXE/DLL? | **Yes — mandatory.** It *is* a DLL loaded by the original EXE. |
| 5 | Injection or standalone? | **DLL injection / binary patching.** Explicitly not standalone. |
| 6 | Windows-specific portions? | `cseries_windows.c`, `cache_files_windows.c`, `game_state_pc.c`, `stack_walk_windows.c`, `physical_memory_map.c` (Win32 `VirtualAlloc`/`VirtualFree`), plus D3D9/DirectSound/DirectInput loading. Also PE/DLL format itself. |
| 7 | Rendering API? | **Direct3D 9** — and only by *delegating to the original EXE's* D3D9 code. |
| 8 | Audio/input/net libs? | **DirectSound**, **DirectInput**, **Winsock** — all via the original EXE. None reimplemented. |
| 9 | How does multiplayer work? | Entirely inside the original unmodified EXE. Demon does not touch it. |
| 10 | Is Blood Gulch MP functional? | Only because *the original game* runs it. **Nothing about it is reimplemented.** |
| 11 | Required original files? | See §7. |
| 12 | Redistributable? | **No.** See §7. |
| 13 | Data extraction tools? | **Yes — Invader**, confirmed Trial (`CACHE_FILE_DEMO`) support, GPLv3, cross-platform. |
| 14 | Adaptable to ARM64? | **No** (§4, measured). |
| 15 | Android/NDK realistic? | Not for this code. Yes for a new engine (§5). |
| 16 | Minimum work to Android PoC? | §9. |

---

## 3. One piece of unexpectedly good news

**The Halo Trial's only multiplayer map is Blood Gulch.** The Trial shipped with exactly:

- **Blood Gulch**, up to **16 players**
- **Two gametypes only:** CTF Classic (first to 3 captures) and FFA Slayer (first to 25 kills)
- Campaign: *The Silent Cartographer* only

The stated product goal — "multiplayer-focused, especially Blood Gulch" — is therefore **100% of the Trial's multiplayer content, not a subset.** There is no MP scope to cut. This is the single most favourable fact in this investigation: it means a complete, shippable MP product requires *one* BSP, *one* vehicle set, and *two* gametypes.

---

## 4. ARM64 feasibility — measured, not estimated

### 4.1 Demon on ARM64

Every `.c` file compiled with `clang --target=aarch64-linux-gnu -std=c2y`:

```
ARM64 compile: OK=2  FAIL=41   (of 43 files)
```

Error breakdown:

```
187  static assertion failed (sizeof/offsetof struct layout)
 32  __float128 not supported on this target   (incidental, header artifact)
 17  TARGET_STRING not defined                 (incidental, build flag)
  7  file not found (Windows headers)
  2  incompatible pointer types
```

**The 187 layout assertions are the real blocker, and they are not bugs — they are the point of the project.** Concretely, from `source/memory/data.h`:

```c
struct data_array {
    char name[32];
    int16_t maximum_count, size;
    bool valid, identifier_zero_invalid;
    tag signature;
    int16_t first_free_absolute_index, count, actual_count, next_identifier;
    void *data;                       // 4 bytes on x86-32, 8 on ARM64
};
static_assert(sizeof(struct data_array) == 56);   // 64 on ARM64 → FAILS
```

Every core engine structure asserts its exact 32-bit byte layout, because it must be **memory-compatible with the original binary's structures**.

### 4.2 The halo-re lifted code on ARM64

Compiled with the real Android NDK r28 `aarch64-linux-android24-clang`:

```
halo-re ARM64 (60-file sample): OK=6  FAIL=54
```

Additional blockers beyond Demon's:

- **66 inline x86 assembly sites across 17 files** (`invalid output constraint in asm`), plus **211** x86 register/x87 FPU instruction references (`fld`, `fstp`, `fchs`, `rdtsc`, `"eax"`…). The project deliberately targets **MSVC 7.1 x87 FPU codegen** for bit-exact float matching — x87 has no ARM64 equivalent (ARM64 has no 80-bit extended precision).
- **921 hardcoded 32-bit absolute address literals** in `.c` files.
- Pervasive **pointer-stored-in-`int`**: lifted signatures read `int model_ref` where the value is a pointer. This is *silently correct* on 32-bit and *silently catastrophic* on 64-bit.

### 4.3 The build architecture makes it structural, not incidental

From `stianeklund/halo`'s `CMakeLists.txt`:

```cmake
set(CMAKE_C_STANDARD 90)
... /def:${gen_dir}/halo.xbe.def   /machine:x86  /out:halo.xbe.lib
... /def:src/xboxkrnl.exe.def      /machine:x86  /out:xboxkrnl.exe.lib
```

The build **links against the original `cachebeta.xbe` as an import library** (so all ~977 un-lifted functions resolve *by address into the original executable*) **and against the Xbox kernel** `xboxkrnl.exe`.

**Therefore "85.65% decompiled" means:** 85.65% of `.text` bytes have a compilable C replacement that gets **patched back into the original Xbox XBE**. The output artifact is a modified XBE that runs on Xbox hardware or in **xemu**. It also means the remaining **14.35% exists only as original machine code** — and **2,517 of ~6,105 functions are still named `FUN_<address>`** (41% semantically unidentified). Sample header comment from `network_client_manager.c`:

> *"0x124730 — model marker lookup … it is kept here because that is the object mapping kb.json currently records."*

The file organisation is approximate, and a models.c function lives in the networking file. This is **binary-behaviour research output, not engine source.**

### 4.4 Verdict on porting

| Path | Verdict |
|---|---|
| Port Demon to ARM64 Android | **Not possible.** It is a Windows DLL whose purpose is patching a 32-bit PE. There is no renderer, input, or netcode to port. |
| Port halo-re lifted C to ARM64 Android | **Not possible as-is.** Needs original XBE + Xbox kernel at link time, 66 x86 asm sites, x87 float-matching intent, 41% unnamed functions, 32-bit ABI throughout. |
| **Legal blocker on halo-re regardless** | **`halo-re/halo` and every fork have NO LICENSE FILE** → all rights reserved by default. **We cannot legally reuse that code.** This alone disqualifies the 327k-line codebase, independent of the technical findings. |

---

## 5. What *is* feasible on Android

Android/ARM64 is entirely realistic **for a new engine** that treats the Trial as a **data dependency**, not a code dependency. The Trial's assets are in `.map` cache files that Invader can already parse cross-platform.

**Verified working on this machine already:** NDK r28 produces ARM64 Android ELF binaries; Vulkan and GLES3 headers are present; `native_app_glue` is available.

### 5.1 One real technical question to resolve on device

Halo memory-maps tag data at a **fixed base address**, verified in `demon/source/cache/physical_memory_map.h`:

```c
static_assert(TAG_CACHE_BASE_ADDRESS == 0x40440000);
```

Tag data contains **absolute 32-bit pointers** assuming the map loads at `0x40440000`. Two options:

1. `mmap(MAP_FIXED)` at `0x40440000` in the 64-bit process and keep tag pointers valid as-is. `0x40440000` ≈ 1.07 GB, comfortably inside ARM64 Android's user VA range and normally unoccupied.
2. Translate pointers to offsets at load time (robust, slightly slower, no VA assumptions).

**Status: UNKNOWN until tested on the S24+.** Determine it with a ~20-line NDK test calling `mmap(0x40440000, len, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS)` and checking the return. Recommend implementing **(2)** regardless, as it removes an entire class of risk.

### 5.2 Networking: a scoping decision that changes the project size

The Halo protocol is **encrypted**: 3DES-CBC for discovery, DES-CBC in-game, Diffie-Hellman key exchange, HMAC-SHA1 derivation — documented for **Xbox System Link only, and only partially**. The **PC/Trial protocol is GameSpy-based and differs**; GameSpy's master servers were shut down **2014-05-31**.

| Goal | Cost |
|---|---|
| **A. Our own clients play each other** (define our own protocol) | **Far cheaper.** Standard UDP + snapshot/delta replication. No RE of encryption. Recommended for the PoC and probably for the product. |
| **B. Join existing Halo Trial/CE servers** | **Much more expensive.** Requires bit-exact reproduction of the encrypted wire format *and* server-side state replication semantics. Partially documented at best. |

**DECIDED 2026-09-18: option A — our own protocol**, with the transport kept behind
an interface so option B stays reachable later without rearchitecting. This removes
the project's single largest scope risk.

---

## 6. Recommended architecture

Matches the platform-abstraction requirement in the brief:

```
                 GAME / ENGINE  (portable C/C++17, no platform calls)
                       │
        ┌──────────────┼──────────────┬──────────────┐
     Renderer        Input          Audio         Network
    (RHI iface)   (action map)   (mixer iface)   (socket iface)
        │              │              │              │
        └──────────────┴──────┬───────┴──────────────┘
                              │
                       PLATFORM LAYER
                      /                \
            Desktop (Linux)          Android (NDK)
         SDL3 · Vulkan · ALSA    GameActivity · Vulkan · AAudio
                              │
                    ASSET LAYER (Invader-derived)
              reads USER-SUPPLIED Halo Trial .map files
```

Rationale: desktop-first keeps iteration fast (seconds, not an APK cycle) and gives a debuggable reference. Android becomes a second backend rather than a rewrite. **Recommend Vulkan-only** — the S24+ has excellent Vulkan support, and maintaining a GLES3 path doubles renderer work for no benefit on the stated target device.

---

## 7. Legal / data handling — read this carefully

### 7.1 The current Demon requires leaked material. We must not use it.

`Aerocatia/demon`'s README requires `halo_cache_symbols.exe` from `haloce_2020_debug.7z`. This is a **2020 internal Halo CE debug build** originating from the **December 2024 unauthorised ~90 GB "Digsite" leak**, posted to 4chan and reportedly sourced from a Halo Studios employee's access. It contains internal dev builds, tools, and documents.

**This is unambiguously unauthorised proprietary material.** It is not a free demo. It must not be downloaded, used, or depended on by this project. **This disqualifies the current `Aerocatia/demon` as a foundation on legal grounds, separately from all technical findings.** (The superseded `demon-old` targeted the legitimate retail Trial instead.)

### 7.2 The Halo Trial itself

- Officially distributed **free of charge** by Bungie/Microsoft/Gearbox (2003). "Free to download" ≠ "free to redistribute."
- **No live official Microsoft/Bungie download exists in 2026.** The original distribution pages are long dead. *(Corrected expectation: the brief asked for an official source; there isn't one any more.)*
- The best-documented preservation copy is the Internet Archive item **[`halo-trial-setup`](https://archive.org/details/halo-trial-setup)** — `HaloTrialSetup.exe`, 131 MB, described as "Official Bungie trial for Halo: Combat Evolved PC. Original .exe installer." Uploaded 2021 by a community member. **This is a preservation archive, not an authorised Microsoft mirror, and carries no rights statement.** It is categorically different from a warez/torrent mirror, but it is not an official source. Flagging that honestly rather than presenting it as authorised.
- **No DRM is involved**, so nothing here requires or implies circumvention.

### 7.2b What the Trial's own EULA says (read from the user's copy, 2026-09-18)

Extracted from `Eula.rtf` inside `HaloTrialSetup.exe`:

- *"Microsoft does not grant you the right to sell or otherwise distribute files
  from the SOFTWARE PRODUCT in exchange for value."* — consistent with our rule
  of never redistributing anything.
- *"You may not reverse engineer, decompile, or disassemble the SOFTWARE
  PRODUCT, except and only to the extent that such activity is expressly
  permitted by applicable law notwithstanding this limitation."*

That second clause deserves a straight answer rather than a shrug. Our project
**reads the game's data files**; it does not decompile, disassemble, or patch
`halo.exe`. That distinction is meaningful, and it is another reason the
architecture chosen in §13 is the right one: **Demon and halo-re both operate by
disassembling and patching the executable — squarely the activity the clause
names. Our approach does not.** The clause also carries an explicit carve-out
for what applicable law permits, which in many jurisdictions covers
interoperability analysis.

This is a licence term, not legal advice, and I am not your lawyer. If this
project were ever to be distributed publicly, that clause is the one worth
having a professional look at. For private use with your own copy, building a
renderer that reads your own data files is a materially different act from
patching Microsoft's binary.

### 7.3 Binding rules for this project

1. **Never commit or bundle Halo assets, executables, or DLLs** into this repository or any APK. Enforced by `.gitignore`.
2. The user supplies their own Trial copy; the app imports assets from device storage at first run.
3. Do not use, reference, or build against the 2024 leak / Digsite material.
4. Our own code: **GPLv3**, to stay compatible with Invader and Demon (both GPLv3) if we reuse them.
5. **Do not copy code from `halo-re/halo` or its forks** — unlicensed, all rights reserved. Their *documentation and research notes* may inform understanding, but their source must not be copied.
6. Struct layouts, field names, and function names are treated as public information (multiple independent sources: official modding tools, community wikis) — consistent with Demon's own stated position.

---

## 8. Verified on this machine

- **Demon builds successfully on CachyOS.** `cmake` + `i686-w64-mingw32-gcc 16.2.0`, Ninja, 44/44 targets, cross-compiled to a 32-bit Windows DLL:
  ```
  demon.dll: PE32 executable for MS Windows 4.00 (DLL), Intel i386, 18 sections
  text 109062  data 3972  bss 712     (291,364 bytes)
  ```
  So the project is healthy and the toolchain is correct — **it just produces a Windows DLL, which is the entire problem.**
- **It cannot be launched or tested here** without a Halo Trial install (not present, and intentionally not obtained — see §7).
- **Android NDK r28 verified** producing `ELF 64-bit ARM aarch64 … for Android 24`.
- Full versions in [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md).

---

## 9. Staged plan

Each stage has a concrete pass/fail test. **Stages 0–3 are cheap and de-risk the expensive parts — do them before committing to the rest.**

| Stage | Deliverable | Test |
|---|---|---|
| **0** | Workspace, toolchains, this document | ✅ done |
| **1** | Android PoC: APK boots, Vulkan clears screen, touch+gamepad logged, clean exit | APK installs & runs on S24+ |
| **2** | `mmap(MAP_FIXED, 0x40440000)` probe on device | Decides §5.1 |
| **3** | Asset layer: parse user-supplied `bloodgulch.map`, dump tag list on device | Tag count matches Invader on desktop |
| **4** | Render Blood Gulch BSP geometry, no lighting, fly-cam | Recognisable geometry on device |
| **5** | Lightmaps, textures, shaders | Visually close to original |
| **6** | Collision BSP + player movement + camera | Walk Blood Gulch |
| **7** | Weapons, projectiles, damage, HUD | Shoot; hit registration |
| **8** | **Netcode (scope per §5.2)** — 2 clients, spawn, see each other, shoot, respawn, score | 2 devices in one session |
| **9** | Vehicles (Warthog, Scorpion, Banshee) | Drivable |
| **10** | Mobile controls polish, gamepad, perf | 60fps on S24+ |

**Honest estimate:** Stage 1–3 in days. Stage 4–7 is a substantial engine effort. Stage 8 is the product. **Total to "full playable MP session": 12–24 months of sustained solo work.** This is a from-scratch engine, and the investigation found no shortcut. Anyone claiming otherwise has mistaken "85% decompiled" for "85% portable."

### The pragmatic alternative, stated for completeness

**Winlator** (Wine + Box86/Box64 on Android) already runs 32-bit Windows games on ARM64 Android, and Halo CE is a known target. This is **not** a native port and not what was asked for — but it is worth knowing it exists because it can serve as (a) an immediate way to play Trial MP on the S24+ today, and (b) a **behavioural reference oracle** to diff our engine against on the same device.

---

## 10. What can be reused vs. rewritten

**Reusable:**
- **Invader** (GPLv3) — Trial map/tag parsing, cross-platform C++, confirmed `CACHE_FILE_DEMO`. Biggest single time-saver; skips the entire asset-format problem.
- **Demon's struct definitions and function names** (GPLv3 + explicitly-public info) — authoritative tag/engine layouts. Reuse the *definitions*; ignore the *hooking*.
- **`keowu/gamespy`** (GPL-3.0) — GameSpy/GOA RE, only if pursuing §5.2 option B.
- **hllmn.net protocol writeup** — understanding, not code.
- Halo community wikis, HEK docs, and the halo-re projects' *notes* (not their code).

**Must be written from scratch:**
- **Everything that makes it a game.** Renderer (no implementation exists anywhere), input, audio, physics/collision response, game loop, object/unit simulation, weapons, vehicles, HUD, menus, **and all netcode**.
- Entire Android platform layer.

---

## 11. Major blockers, ranked

1. **No portable engine exists.** Every project is an x86-binary patcher. This is the whole finding. *No workaround: it must be written.*
2. **halo-re's 327k lines are unlicensed** (all rights reserved) → largest body of RE work is legally unusable as code. *Workaround: use it as documentation only; write our own.*
3. **Current Demon depends on leaked material** → unusable. *Workaround: use `demon-old`'s legitimate Trial targeting and Demon's public struct definitions.*
4. **Netcode is encrypted and only partially documented (Xbox-only)** → §5.2 option B is high-risk. *Workaround: option A — our own protocol.*
5. **32-bit ABI is baked into all existing RE code** (187 + layout assertions, 66 asm sites, 921 absolute addresses, pointers in `int`). *Workaround: none; do not port this code.*
6. **Original files must be user-supplied**, complicating onboarding and testing. *Workaround: in-app import flow; never bundle.*
7. **Scale.** One person, 12–24 months. *Workaround: ruthless MP-only scope — §3 shows the Trial helps here.*

---

## 12. Explicitly unknown, and how to resolve

| Unknown | How to determine |
|---|---|
| Does `MAP_FIXED` at `0x40440000` work on the S24+? | Stage 2 probe, ~20 lines. Or sidestep with offset-based pointers. |
| Exact **PC/Trial** wire protocol (vs. documented Xbox System Link) | **No longer on the critical path** — §5.2 resolved to option A. Would only matter if wire-compatibility is revisited; method would be to capture loopback traffic between two local Trial instances and diff against the hllmn Xbox findings. |
| Whether any community master server still serves Trial clients | Probe with a real Trial client; ask the Open Carnage / Halomaps communities. |
| Halo Trial EULA's exact redistribution terms | Extract the EULA from the user's own `HaloTrialSetup.exe` and read it. **We are designing to never redistribute, so this is not blocking.** |
| ~~Whether our parser handles real Trial data~~ | **RESOLVED 2026-09-18.** The user supplied their own Trial copy. Our parser reads `bloodgulch.map` correctly: 2410 tags, 72 spawn points, 5503 triangles, 0 materials skipped. See `BLOOD_GULCH_ASSETS.md`. |
| ~~Whether Blood Gulch needs `bitmaps.map`~~ | **RESOLVED: no.** 0 of 2410 tags are flagged `indexed`. (Proven for tag data; texture pixel data not yet exercised.) |
| Real S24+ perf headroom for a Vulkan Blood Gulch | Only measurable after Stage 4. |
| Whether Demon actually runs (we only proved it *builds*) | Requires a Trial install; deliberately not obtained. |
| Xbox↔PC gameplay/netcode divergence | Cross-reference halo-re notes (as docs) with PC captures. |

---

## 13. Recommendation

**Do not port Demon. Do not port halo-re.** Both are architecturally and legally unsuitable, for reasons measured above rather than assumed.

**Build a new, MP-only, Vulkan, portable-C++ engine that consumes user-supplied Halo Trial assets,** desktop-first with Android as a first-class second backend, reusing Invader for assets and Demon's struct definitions for layouts.

Proceed with **Stages 1–3** now: they are days of work, they prove out the toolchain, the device, and the asset pipeline, and they answer the two open technical questions (§5.1, and Invader's Trial round-trip) before any large commitment.

**§5.2 is resolved** (our own protocol), so Phase 2 is unblocked and the largest
scope risk is retired.
