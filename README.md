# halo-trial-android

A native ARM64 Android engine for the free **Halo: Combat Evolved Trial**,
playing **Blood Gulch**. C, Vulkan, AAudio, no engine dependencies. It reads
the player's own copy of the Trial and takes every value it can from the
original tags.

**Working on this? Start here: [`docs/HANDOFF.md`](docs/HANDOFF.md).**
That file is the whole briefing — the build and test loop, what works, what is
next, how to read Halo's tags, and the traps that have already cost a session.

## Where it got to

Blood Gulch renders with its lightmaps and detail maps. You can run, crouch,
jump and fall on the Trial's own biped physics; carry two of its eleven
weapons with their real models, animations, sounds and HUD; throw its
grenades; pick up what the map actually places, on the map's own respawn
timers; die and watch your body go down; and shoot a target that bleeds
shields before health, at the damage the tags say each weapon does.
Human Warthogs can be driven across Blood Gulch with a chase camera, steering,
braking, wheel motion, and terrain contact.

Not yet: other drivable vehicles, bots that think, menus, netcode.

## Legal

- **No Halo assets, executables, or DLLs are ever committed here or bundled in
  an APK.** The player supplies their own Trial copy; assets are imported
  on-device at runtime.
- This project does **not** use the December 2024 Halo "Digsite" leak material
  that the current upstream Demon depends on.
- No DRM is involved and none is circumvented.
- Our code: GPLv3 (compatible with Invader and Demon).

## Docs

| Doc | Contents |
|---|---|
| [HANDOFF.md](docs/HANDOFF.md) | **Start here.** Current state, the loop, tag discipline, traps, invented constants |
| [JOURNAL.md](docs/JOURNAL.md) | Every session, newest first. The *why*. Search it by symptom |
| [BUILD_ENVIRONMENT.md](docs/BUILD_ENVIRONMENT.md) | Exact toolchain versions |
| [BLOOD_GULCH_ASSETS.md](docs/BLOOD_GULCH_ASSETS.md) | What is in the map |
| [INVADER_ASSET_PIPELINE.md](docs/INVADER_ASSET_PIPELINE.md) | How Invader's tag definitions are used |
| [ANDROID_PORT_INVESTIGATION.md](docs/ANDROID_PORT_INVESTIGATION.md) | The original feasibility study |
| [PROGRESS.md](docs/PROGRESS.md) | Early milestone log |
