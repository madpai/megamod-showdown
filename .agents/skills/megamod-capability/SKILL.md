---
name: megamod-capability
description: Research and implement a reusable Megamod Engine capability from a donor mechanic or a Showdown feature request, with an original content slice and measured evidence. Use for new mechanics, importer/runtime boundaries, or cross-game behavior comparisons.
---

# Develop a Megamod capability

Locate the Showdown and Open Asset Lab checkouts. Read their `AGENTS.md`,
the Engine's `CLAUDE.md`, current `docs/HANDOFF.md`,
`docs/MEGAMOD_VISION.md`, `docs/ENGINE_ARCHITECTURE.md`, and, for gameplay,
`docs/GAMEPLAY_CAPABILITY_MODULES.md`. Read Asset Lab's vision when content
or its contract changes. Current code and production evidence supersede
dated research snapshots.

## Establish the behavior and its owner

- Describe the observable player behavior, the present limitation and the
  smallest playable result. Define its test before changing architecture.
- Search `docs/research/`, `docs/night_shift/` and `docs/RACING.md` for an
  existing answer. Study only the unresolved donor behavior. Record
  **Fact / Inference / Unknown**, upstream revision, exact source locations,
  license boundaries and any authored tuning. A showcase is evidence of
  an appearance, not proof of its architecture or performance.
- Prefer authoritative source/data and reproducible traces. For private
  binary research, keep extracts and decompiles outside public checkouts.
  Validate offsets against several files; prove a format hypothesis with
  decode/encode/decode or a compiler/native-loader comparison.
- Assign source acquisition and foreign interpretation to Asset Lab;
  frequent generic simulation to native Engine code; mode rules and roster
  choices to Showdown. Existing host Lua and declarative bindings compose
  behavior where their actual APIs suffice. Do not invent a plugin, ECS,
  package format or generic mode API to express one feature.

## Carry one slice through

Start with original placeholder content in Asset Lab, through the existing
compiler and the native runtime. Define authority, reset behavior, late
join, package identity and resource bounds for the actual feature. Learn
from `engine/arcade_racer.*`, `game/race.*`, `app/racing.*` as one concrete
boundary, not a required template for every mechanic.

Use `assetlab project verify` for project/package agreement (see the
assetlab-content skill), then the applicable Showdown playtest and existing
verification procedure. Loading successfully does not prove playability.
Compare imported content when the change touches a shared path, and Blood
Gulch/Android when it touches runtime behavior. Seek a second concrete
consumer before broadening an abstraction.

After repeated identical failures, inspect the evidence and change the
hypothesis. Preserve the last successful slice. Write confirmed findings,
remaining coupling and the next decisive check into the existing research
note/handoff; use private scratch for raw evidence. Report measured scope
and counts, with phone-only claims reserved for physical-device results.

The methodological source and its limitations are documented in
[Universal Modder study](../../../docs/research/UNIVERSAL_MODDER.md).
