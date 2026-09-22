# halo-trial-android

A native ARM64 Android engine for the **Halo: Combat Evolved Trial**, playing
Blood Gulch. C, Vulkan, AAudio.

## Read this first

**`docs/HANDOFF.md`** is the full briefing: the build/test/publish loop, what
works, what is next, how to read Halo's tags, the traps that have already cost
a session, and every invented constant. Read it before starting work.

`docs/JOURNAL.md` is the session history — the *why*. Search it by symptom
(`grep -in "upside down"`), do not read it front to back.

## Hard constraints

- **No Halo assets, executables or DLLs are ever committed.** The owner
  supplies their own Trial copy. No DRM is involved or circumvented.
- **The shareable APK carries no Trial data** (`verify.sh` checks it). Since
  2026-09-22 the owner publishes a PERSONAL build with
  `scripts/publish_apk.sh --with-assets`, which puts their own maps
  (`bloodgulch`, `bitmaps`, `sounds`, `ui`) into the APK uncompressed; native
  code maps them straight out of it. That APK is for the owner's own device
  only and must never be given to anyone else -- a LAN friend installs the
  plain build and picks their own maps.
- **Never commit Trial `.map` files.** They live at
  `/home/commander/halo-trial-data/extract/maps/`.
- Project code is **GPLv3**.
- Git author: **`Phase2 <schultz0@proton.me>`**.
- **Do not push unless asked.**

## The loop

```
# 1. verify — must be green before publishing
HTA_MAP=/home/commander/halo-trial-data/extract/maps/bloodgulch.map scripts/verify.sh

# 2. publish to the sideload server the owner installs from
scripts/publish_apk.sh --title "what changed" --notes-text "a sentence or two"

# 3. the owner tests on device; screenshots arrive here, newest first
ls -lt scratch/uploads/ | head
```

`src/platform/platform_android.c` is **only** built by the Android target —
`cmake --build build-host` will not catch mistakes in it; `scripts/ndkcheck.sh`
does in a second, `verify.sh` for real. Publish the owner's build with
`--with-assets`. If host Vulkan is broken, see "Tools" in HANDOFF.md
(lavapipe in `scratch/lvp`).

End every session by updating the **CURRENT TESTING OBJECTIVE** section of
`docs/HANDOFF.md` with what to try on device next.

## Working style that has been productive here

- Small shippable slices. The owner tests every build.
- Take values from the tags. When you invent a number, say so in the release
  notes and add it to the ledger in `HANDOFF.md`.
- Struct sizes must reconcile against Invader's JSON before you trust an
  offset; probe when the walk drifts; sanity-check values against the real
  world.
- Measure before optimising. Suspect the data path before the art.
- Write the test that would have caught it.
