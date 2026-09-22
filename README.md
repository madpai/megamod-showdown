# Halo: MP (halo-trial-android)

A native ARM64 Android engine for the free **Halo: Combat Evolved Trial**,
playing **Blood Gulch**. C, Vulkan, AAudio, no engine dependencies. It reads
the player's own copy of the Trial and takes every value it can from the
original tags.

**Working on this? Start here: [`docs/HANDOFF.md`](docs/HANDOFF.md).**
That file is the whole briefing — the build and test loop, what works, what is
next, how to read Halo's tags, and the traps that have already cost a session.

## Where it got to

The app opens on the Trial's own main menu (from `ui.map`): the ring, the
space sky, the HALO logo and the title theme. MULTIPLAYER plays Slayer on
Blood Gulch against 0-7 bots that walk a nav grid rebuilt from the map's
collision, pick up weapons and powerups, and fight with the tags' own
damage; the kill feed and announcer use the Trial's own words and voice.
Blood Gulch renders with lightmaps, detail maps and (new) its real sky. You
can run, crouch, jump, carry two of the eleven weapons with real models,
animations, sounds and HUD, throw grenades, drive Warthogs, die and respawn.
BACK pauses.

Not yet: the Scorpion, Ghost, Banshee and turret; Warthog gunner seats;
vehicle damage; more than one remote player in LAN and server-authoritative
combat; CTF/Oddball/KOTH; the campaign. See [`docs/HANDOFF.md`](docs/HANDOFF.md).

## Legal

- **No Halo assets, executables, or DLLs are ever committed here.** The
  shareable APK carries none; the player supplies their own Trial copy. The
  owner's personal build (`publish_apk.sh --with-assets`) embeds their own
  maps for their own device only and is never distributed.
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
| [NETWORK_ARCHITECTURE.md](docs/NETWORK_ARCHITECTURE.md) | Current engine audit and native multiplayer design |
| [NETWORK_PROGRESS.md](docs/NETWORK_PROGRESS.md) | Verified LAN slice, commands and remaining tests |
