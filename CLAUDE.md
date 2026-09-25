# MEGAMOD SHOWDOWN (branch `halo-sandbox` of halo-trial-android)

**This branch is MEGAMOD SHOWDOWN**, the owner's fork of Open Halo that plays
imported content from other games (Source maps, characters, weapons via
Open Asset Lab). Named 2026-09-24. Its GitHub home is the PUBLIC repo
https://github.com/madpai/megamod-showdown (remote `megamod`; this branch
tracks `megamod/main`). Push with `git push megamod halo-sandbox:main`.

- **Never push this branch to `origin`** (Open Halo, public, strictly
  Halo) and never cut Open Halo releases from it. The Open Halo rules
  below about `origin`, `fp-animated-guns` and guest-APK releases apply to
  Open Halo work only.
- Imported packages (`.oalmap`, `.oalasset`) and Trial data are never
  committed here either; they live in `~/assetlab-private/bundle` and go
  only into the owner's personal APK.
- Halo engine fixes that are not about imported content can be offered
  to Open Halo separately (cherry-pick onto `fp-animated-guns`), with the
  owner's say-so.

The rest of this file is the shared engine's briefing.

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
- **Keep GitHub current (owner's standing instruction, 2026-09-23):** push
  after every commit, and when a build is published, release its guest APK
  (`gh release create vX.Y.Z scratch/serve/halo-trial-guest.apk --target main`,
  never the personal one). The remote is `origin`,
  https://github.com/madpai/open-halo-project (PUBLIC). Local
  `fp-animated-guns` goes to its `main`: `git push origin fp-animated-guns:main`.
  Before any push, confirm no Trial data is in history:
  `git rev-list --all --objects | grep -iE '\.(map|wav|ogg)$'` must print nothing.

## The loop

```
# 1. verify — must be green before publishing
HTA_MAP=/home/commander/halo-trial-data/extract/maps/bloodgulch.map scripts/verify.sh

# 2. publish to the sideload server the owner installs from
scripts/publish_apk.sh --title "what changed" --notes-text "a sentence or two"

#    ...which also backs everything up to /mnt/media/backups/halo-trial-android
#    (project, git bundle, Trial data, both APKs per build; apks/latest).
#    The owner wants that copy current at all times: run
#    scripts/backup_local.sh by hand after any work that is not published.

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
