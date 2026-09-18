# halo-trial-android

Investigating and (if feasible) building a native ARM64 Android multiplayer client
for the free **Halo: Combat Evolved Trial** — target: **Blood Gulch**.

**Start here: [`docs/ANDROID_PORT_INVESTIGATION.md`](docs/ANDROID_PORT_INVESTIGATION.md)**

## Headline finding

No portable Halo engine exists. Every reverse-engineering project found
(Demon, halo-re/halo and forks) is an **in-process patcher for a 32-bit x86
executable** — the artifact they produce *is* the original binary. That class
of project cannot be ported to ARM64 Android.

Native Android is feasible only as a **new engine** consuming user-supplied
Trial assets. Favourable scope note: the Trial's *only* MP map is Blood Gulch,
so the stated goal is 100% of the Trial's MP content, not a subset.

## Legal

- **No Halo assets, executables, or DLLs are ever committed here or bundled in an APK.**
- The user supplies their own Trial copy; assets are imported on-device at runtime.
- This project does **not** use the December 2024 Halo "Digsite" leak material
  that the current upstream Demon depends on.
- No DRM is involved and none is circumvented.
- Our code: GPLv3 (compatible with Invader and Demon).

## Docs

| Doc | Contents |
|---|---|
| [ANDROID_PORT_INVESTIGATION.md](docs/ANDROID_PORT_INVESTIGATION.md) | Full findings, measurements, feasibility, staged plan |
| [BUILD_ENVIRONMENT.md](docs/BUILD_ENVIRONMENT.md) | Exact toolchain versions |
| [PROGRESS.md](docs/PROGRESS.md) | Milestone checklist + blockers |
