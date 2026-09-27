# Halo reconstruction research (2026-09-26)

This is a read-only comparison for **Open Halo compatibility** and **MegaMod Engine design**. No reconstructed implementation, formulas, or assets were imported. The source is evidence about a particular Halo build, not a specification for MegaMod or proof of Halo Trial behavior.

## Repositories and scope

| Repository (examined revision) | Target and evidence status | Port/toolchain boundary |
| --- | --- | --- |
| [punpckhdq/halo](https://github.com/punpckhdq/halo/tree/01e8615) | Work-in-progress reconstruction of Xbox Halo CE `cachebeta.exe`, PAL debug build **2342**. | Matching build needs the separately supplied PAL debug executable, August 2001 Xbox SDK, Python and Ninja. README notes type help from later Halo CEA beta symbols, which can differ from Xbox. |
| [bnunu/halo-1](https://github.com/bnunu/halo-1/tree/52659b6) | Fork of the same 2342 target; its README **claims** approximately 99.5% byte matching. Much more reconstruction documentation and source is present. Exact and honest fuzzy bodies coexist; the claim is not independently verified here. | Same matching prerequisites. Later PC/CEA names, supplied source maps and reconstructed C have different evidentiary strength. |
| [cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal/tree/13d1ae2) | Fork of bnunu, still targeting 2342, with **new native port code**. The Linux/Windows/Android builds and interpolation are not byte-matching Xbox behavior. | Linux: 32-bit clang, glibc development files, SDL3, OpenGL. Windows: 32-bit clang, SDL3, Visual Studio x86 libraries and Windows SDK. Android: NDK, arm64_32-capable clang, Gradle/JDK, SDL3 and GLES. PAL 2342 data is separately required to run. |
| [bnunu/halo-ce-universal](https://github.com/bnunu/halo-ce-universal/tree/f2fa457) | Fork of universal. Adds a **port-only** 128-player/128-machine system-link experiment, while the matching Xbox build remains at 16 players/4 machines. | Enlarged pools, game-state memory, messages, UI handling and protocol version; its [system-link notes](https://github.com/bnunu/halo-ce-universal/blob/f2fa457/port/linux/README.md#system-link) give measured traffic and limitations. |

The upstream checkout contains 876 `source/` files at the examined revision; the bnunu reconstruction and both port checkouts contain 990 each. The shared top-level families (AI, objects, units, items, physics, game, networking, HS, render, sound, memory) remain recognizable, while the fork adds more bodies and evidence notes. The detailed subsystem and behavior analysis therefore cites the fuller bnunu tree and uses upstream to establish lineage and the earlier organization. File count is **not** a byte-match or quality measure.

The reconstruction is **Xbox PAL debug 2342**, with data identified by the port as 01.01.14.2342. MegaMod currently reads the owner's **Halo CE Trial PC** cache. We did not establish a function-by-function Trial-versus-2342 crosswalk. PC/Trial changes to timing, tags, game rules, networking, graphics, input and map data may invalidate a transfer. Even within 2342, a reconstructed function can be approximate. Treat every behavioral comparison below as a candidate for an independent Trial test. The universal port's `HALO_LINUX`/`HALO_ANDROID` branches, SDL/GL adapters, and interpolation are **new implementation**, not original source evidence. [Build/lineage notes](https://github.com/cybersecurity/halo-ce-universal/blob/13d1ae2/README.md) explain the split.

## Provenance boundary

The repositories label their work CC0, but reconstructed proprietary implementation may raise third-party provenance questions. This research records concepts, interfaces, ordering and validation hypotheses only. Do not transplant bodies, mechanically translate them, or place reconstructed Halo code in generic MegaMod Engine. Any Halo compatibility change needs separately authorized work and an independent test against the owner's Trial data. No game data, executable or private package belongs in these public repos.

The bnunu tree also discusses separately supplied, source-like network reference files in its [reconstruction map](https://github.com/bnunu/halo-1/blob/52659b6/docs/user_source_reconstruction_map_20260906.md). Its own notes say complete provenance and build context were not established. This study did not inspect those attachments and does not treat them as cleared original source. Its citations point to the public reconstruction or port documentation, with their evidence labels.

## Reading index

- [Subsystem map](HALO_SUBSYSTEM_MAP.md): architecture ownership, types, updates, dependencies and MegaMod equivalents.
- [Behavior findings](OPEN_HALO_BEHAVIOR_FINDINGS.md): classified findings, tick order, handles, AI, physics and scripting.
- [Engine assumption audit](MEGAMOD_HALO_ASSUMPTION_AUDIT.md): where Halo ancestry still leaks, and what **not** to carry forward.
- [Portability, networking and agent DX](PORTABILITY_NETWORK_AGENT_LESSONS.md): separate evidence about native ports and the 128-player fork.
- [Independent follow-up tests](FOLLOW_UP_TESTS.md): fixtures and observable pass/fail conditions; no implementation changes.

MegaMod baseline used for the initial comparison: [vision](../../MEGAMOD_VISION.md), [handoff](../../HANDOFF.md), [engine map](../../ENGINE_ARCHITECTURE.md), [desktop plan](../../DESKTOP_AGENT.md), [dense-index audit](../DENSE_INDEX_HAZARDS.md), [runtime/content review](../CURRENT_RUNTIME_CONTENT_BOUNDARY_REVIEW.md), and source at `52e9156`. Before publication, the findings were reconciled with main at `5b24c02`: [X1/X2 world entities](../../WORLD_ENTITIES.md), generation-checked world-entity handles, reusable mover definitions and the expanded world key are now implemented. Other specialized pools have not all adopted common handles. Open Asset Lab baseline: [vision](https://github.com/madpai/open-asset-lab/blob/main/docs/ASSET_LAB_VISION.md) and [generalization audit](https://github.com/madpai/open-asset-lab/blob/main/docs/GENERALIZATION_AUDIT.md). Some older MegaMod architecture and network documents are historical; the handoff and current source take precedence.

**Decision:** this research does not justify revisiting landed X1/X2, changing Lua or original-content priorities, or altering the current roadmap. It narrows compatibility questions and adds test candidates.

## Ten most useful takeaways

1. Xbox 2342 and PC Trial require a version crosswalk before behavior transfer.
2. The reconstructed tick gives a concrete phase-order hypothesis for independent testing.
3. Slot-plus-identifier datum handles demonstrate the stale-reference problem MegaMod's implemented X1 world-entity handles address in one domain.
4. Object creation, deletion, parent/child update and BSP activation have explicit lifecycle phases worth testing.
5. AI separates perception, decisions, movement and firing, with dormant/budgeted work; simple bots can benefit from telemetry without inheriting the full system.
6. Physics, collision, damage and rules are distinct responsibilities, even though Halo ties them through object types and tags.
7. Halo script threads are tick-owned and use typed engine references; host Lua should have a similarly explicit and safe API boundary, not HS semantics.
8. Halo system link's lockstep input flow differs from MegaMod v10 host authority; the latter remains a valid product choice.
9. The 128-player fork shows player limits span protocol, field widths, pools, UI, memory and bandwidth.
10. The universal port exposes hidden ABI, filesystem, timing and Android memory-layout assumptions; its agent docs show the value of clear evidence labels and owner maps.

## Next-session handoff

- **State:** research is complete at the pinned revisions above. The six documents here and the parent research index are the entire deliverable. No runtime, package, network, Android or Open Asset Lab implementation was changed. Publication verification on MegaMod main `5b24c02` passed **83/83** checks with the owner's Trial data, including X1/X2, host rendering and the Android APK build. Local/pinned-source links and Markdown whitespace passed a separate check.
- **Best next evidence, when compatibility work is scheduled:** privately establish a Trial-versus-2342 build/content crosswalk, then run the independent tick/lifecycle, projectile/melee/grenade, seat/damage and CTF probes in [FOLLOW_UP_TESTS.md](FOLLOW_UP_TESTS.md). Record observed Trial results separately from reconstructed-source hypotheses. Public fixtures must be original and synthetic.
- **Best engine follow-up, when a new pool needs cross-system references:** use the landed [X1/X2 world-entity contract](../../WORLD_ENTITIES.md) as MegaMod's own tested baseline. Reuse its authored-ID and checked-handle principles only when the new domain requires them. The Halo datum model is evidence for stale-slot hazards, not an implementation recipe.
- **Decision boundary:** no finding reopens X1/X2 or changes Lua or original-content priorities. Revisit only if an independently reproduced Trial incompatibility or a concrete generic-engine requirement appears.
