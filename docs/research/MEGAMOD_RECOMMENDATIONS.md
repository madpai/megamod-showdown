# MegaMod and Open Asset Lab recommendations

**Status:** this is the 2026-09-26 research roadmap, annotated after X1–X9
landed. Its original proposal text is retained as research history, not a
fresh implementation order. Follow [MegaMod's staged refactor rule](../MEGAMOD_VISION.md#21-do-not-over-refactor)
and the [current handoff](../HANDOFF.md#current-testing-objective): a real
limitation, minimal slice, tests and Android/Trial/imported-content
regression. See [synthesis](CROSS_ENGINE_SYNTHESIS.md),
[content compatibility](../CONTENT_COMPATIBILITY.md),
[Night Shift evidence](../night_shift/README.md) and
[Asset Lab consequences](https://github.com/madpai/open-asset-lab/blob/main/docs/RESEARCH_CONNECTIONS.md).

## Landed after this research pass

| Milestone | Current evidence |
| --- | --- |
| X1 | Generic world interactions and late-join state ([WORLD_ENTITIES](../WORLD_ENTITIES.md)) |
| X2 | Shared mover definition and world key ([WORLD_ENTITIES](../WORLD_ENTITIES.md)); not a complete registry for every content kind |
| X3 | Bounded host Lua ([SCRIPTING](../SCRIPTING.md)); not complete Lua/data game modes |
| X4 | Typed resource IDs, package requirements and world-key closure ([RESOURCES](../RESOURCES.md)); the older lockfile proposal is broader |
| X5–X7 | Package-backed assets, prefabs and declarative bindings ([RESOURCES](../RESOURCES.md), [PREFABS](../PREFABS.md), [EVENT_BINDINGS](../EVENT_BINDINGS.md)) |
| X8 | Compact replicated world state and the X8 S24+ solo test ([WORLD_STATE](../WORLD_STATE.md), [playtest](../night_shift/PLAYTEST_NOTES.md)) |
| X9 | Authored lighting and presentation ([visual pass](../night_shift/X9_VISUAL_PASS.md)); subsequent S24+ solo review recorded 120.2 mean FPS and a positive visual assessment |

The original Night Shift slice exercised X3–X7, and its X8/X9 variants
build on it. It exposed real scenario-rules and authoring needs without
requiring a creature. A later [scenario-rules slice](../night_shift/SCENARIO_RULES.md)
addresses the inherited Slayer HUD/loadout and adds a local flashlight;
physical validation of that new light remains pending. Current measured
needs choose the next milestone; Skate, Racing and Party remain future
validation targets.

## N1–N3 (the original NOW research phase)

### N1. Runtime/content boundary inventory — done

The independent [source review](CURRENT_RUNTIME_CONTENT_BOUNDARY_REVIEW.md) identified path/ID conflation, manifest gameplay fields, ordinal network dependencies and Halo-tag/display-name lookups. The landed [v10 contract](../CONTENT_COMPATIBILITY.md) resolves the network inventory against code.

- **Problem:** OALMAP/OALASSET and current gameplay data have different schemas and ownership assumptions.
- **Observed solution:** Factorio publishes stage/API boundaries; Space Engineers distinguishes builders and runtime entities.
- **Proposed MegaMod adaptation:** document actual package fields, loader limits, definition lookup points, world IDs, and authority for each existing type before changing formats.
- **Benefits:** prevents accidental foreign-format leakage and version drift. **Costs:** short audit/doc work. **Risks:** stale docs; tie to code links.
- **Dependencies:** existing package docs and loader source. **Prototype needed?** No.
- **Open Asset Lab impact:** source-to-runtime field map and provenance matrix. **Networking impact:** identify fields affecting match compatibility. **Android impact:** no runtime cost.

### N2. Read-only OAL stable-ID audit — done

OAL's `assetlab ids` command now audits the [single proposed grammar](CONTENT_ID_GRAMMAR_RECOMMENDATION.md), current-name collisions and legacy references without changing package or runtime formats. The current bundle has one duplicate world identity and no declared namespaces; see [OAL's results](https://github.com/madpai/open-asset-lab/blob/main/docs/CONTENT_IDS.md).

- **Problem:** content references and package dependencies become opaque as new asset kinds arrive.
- **Observed solution:** Factorio prototype errors, Arma required addons, xEdit reference/conflict views.
- **Proposed MegaMod adaptation:** agree on a small namespaced ID grammar and referential checks for *current* map/asset manifests; do not redesign binary files yet.
- **Benefits:** earlier errors, future registry foundation. **Costs:** ID migration from existing ad hoc names. **Risks:** choosing a grammar too early; keep aliases for current content.
- **Dependencies:** OAL manifest read/write and MegaMod loader inventory. **Prototype needed?** Yes, synthetic duplicate/missing-reference packages.
- **Open Asset Lab impact:** implement offline checks and source-position diagnostics. **Networking impact:** canonical IDs enable package matching. **Android impact:** one startup lookup table at most.

### N3. Protocol v10 imported-content compatibility — done

- **Problem:** matching APKs and private roster packages are operational requirements, while future user packages need an explicit handshake.
- **Observed solution:** NS2 file consistency, Factorio save/mod configuration alignment, GNS connection state.
- **Landed MegaMod adaptation:** protocol v10 compares map and ordered imported character/weapon fingerprints before allocating a match slot. [Independent review](V10_FINGERPRINT_ARCHITECTURE_REVIEW.md) records its actual scope and remaining float/world-key limits. A future package lockfile does not replace this immediate gate.
- **Benefits:** prevents invisible mismatches. **Costs:** instrumentation and test fixtures. **Risks:** hash of nondeterministic manifest fields; canonicalize in OAL.
- **Dependencies:** current net codec and package manifests. **Prototype needed?** Yes, two synthetic peers with mismatched content.
- **Open Asset Lab impact:** deterministic closure hash/lockfile design. **Networking impact:** direct; fail before spawn. **Android impact:** small startup hash cost, no per-frame work.

## Post-N3 dependency decision

The 2026-09-26 [order memo](POST_N3_ORDER_DECISION.md) recommended X1 with the then-existing emulator/desktop joiner/headless paths; X1 has since landed. The [X1 authoring comparison](X1_ORIGINAL_AUTHORING_PATH.md) recommended a programmatic normalized-world integration fixture through OAL's normal validator/compiler, plus a C runtime unit fixture. N2's grammar was [audited](CONTENT_ID_GRAMMAR_RECOMMENDATION.md), not a runtime migration.

| Phase | Prerequisite | Concrete problem solved | Acceptance condition | Why no earlier |
| --- | --- | --- | --- | --- |
| N1 inventory — done | Current OAL/runtime source | Reveals actual gameplay fields and local ordinals | [V10 contract](../CONTENT_COMPATIBILITY.md) reconciles field/loader/network inventory | Must precede changes that assume the boundary |
| N3 v10 — done | N1's current-index evidence | Refuses mismatched imported rosters and missing joiner values before spawn | Cross-platform mismatch/zero tests reject before slot allocation | Correctness bug existed without stable-ID migration |
| N2 read-only audit — done | N1 names and OAL manifests | Exposes collisions and legacy mapping cost for `namespace:type/name` | OAL audit and synthetic tests; no runtime format change | Migration before v10 would enlarge the bug fix |
| X1 world-event slice — **implemented 2026-09-26** ([WORLD_ENTITIES](../WORLD_ENTITIES.md)) | V10 gate, original OAL fixture, current emulator/joiner paths | Generic Button → Relay → Door and Trigger → Teleport destination with host state/collision | Real compiled package loads; two clients and late join agree; invalid-link, reorder and stale-handle tests pass | Needs real fixture and a bounded interact input, not full desktop gameplay |
| Full Step 5 desktop | X1 behavior and shared player/action contract | Desktop parity and visual debugging | Desktop hosts/joins same match and action behavior as Android | X1 can reveal needed state/action UI without blocking engine proof |
| X2 mover definition — landed in a bounded form | X1 fields plus N2 evidence | Shared mover identity beyond ad hoc lists | One definition reused by multiple doors; see WORLD_ENTITIES | Broader registry remains a separate need |
| X3 host Lua — landed in a bounded form | X1/X2 verbs and authority | Creator behavior beyond fixed C links | Host script and late join proven; see SCRIPTING | General game modes remain future work |
| X4 package/resource graph — landed in a bounded form | X2/X3 package dependency needs | Typed references, requirements and compatible world identity | Missing/unsupported requirements reject; see RESOURCES | A general lockfile remains broader than X4 |

## Landed slices (the original NEXT proposals)

### X1. Build one generic world-event slice — landed

The [X1 evidence note](WORLD_EVENT_SLICE_RECOMMENDATION.md) narrows this to typed verbs, placed IDs, bounded queued dispatch, moving collision, host authority and late-join state. Implemented 2026-09-26 ([WORLD_ENTITIES](../WORLD_ENTITIES.md)): the acceptance checks pass on host tests, `scripts/test_x1.sh` and the emulator; the note records what was confirmed and refined.

- **Problem:** imported maps currently carry geometry and limited breakables; doors/buttons/triggers need generic behavior.
- **Observed solution:** Source entity I/O, Bethesda placement references, Arma mission/editor entities.
- **Proposed MegaMod adaptation:** original synthetic map with stable placed IDs, `Trigger`, `Interactable`, `EventEmitter/Receiver`, moving collider, and generation-checked runtime handles; reuse current prop/collision systems.
- **Benefits:** unlocks interactive worlds without Source class leakage. **Costs:** event queue, handle lifecycle, save/replication schema. **Risks:** event cycles, stale targets, collision regressions; cap/validate.
- **Dependencies:** OAL world-entity normalization/target-link validation. **Prototype needed?** Yes: button → relay → door and teleport, two-client run.
- **Open Asset Lab impact:** compile generic graph and diagnostics. **Networking impact:** host owns transitions and replicates state/events. **Android impact:** bounded active set, no global per-frame scan.

### X2. Compile immutable definitions into a small registry — mover slice landed

- **Problem:** characters, weapons, hero abilities, and world entities need consistent content identity without C special cases.
- **Observed solution:** Factorio prototypes, tModLoader owned registration, Space Engineers definitions/builders, Arma configs.
- **Proposed MegaMod adaptation:** OAL flattens `CharacterDefinition`, `WeaponDefinition`, `ProjectileDefinition`, `AbilityDefinition`, `VehicleDefinition`, `MaterialDefinition`, `GameRule`, and `WorldEntityDefinition` as each gets a second use; runtime resolves namespaced IDs to dense indices at match load.
- **Benefits:** source-independent content and simple hot paths. **Costs:** schema/version work in two repos. **Risks:** overgeneralization; start with weapon plus one world entity.
- **Dependencies:** N1/N2 and an original-content fixture. **Prototype needed?** Yes, two package definitions and one reference.
- **Open Asset Lab impact:** owns schema normalization/flattening and provenance. **Networking impact:** definition hash and declared replicated fields. **Android impact:** compact tables and no runtime inheritance parsing.

### X3. Prototype host Lua for one ability and one trigger — bounded host Lua landed

- **Problem:** special-cased hero behavior in C makes new gameplay require an engine build.
- **Observed solution:** Factorio control events, NS2 Lua game layer, PZ Events, Reconstructor's small-API lesson.
- **Proposed MegaMod adaptation:** host-only Lua with versioned verbs, typed handles, fixed-tick event dispatch, bounded state and instruction/memory budget; no raw pointers/files/sockets.
- **Benefits:** new behavior independent of C releases. **Costs:** runtime dependency, API docs, debugging, state/schema tests. **Risks:** latency, infinite loops, nondeterministic effects; measure and limit.
- **Dependencies:** X1/X2 subset and authority contract. **Prototype needed?** Yes, one current ability migrated as proof, compared against current behavior.
- **Open Asset Lab impact:** static script/API/reference checks and package capability manifest. **Networking impact:** host sends declared state/effects only. **Android impact:** budget and memory profile required on device.

### X4. Add match-boundary package closure and compatibility checks — package graph landed in part

- **Problem:** separately supplied packages can refer to missing or incompatible definitions.
- **Observed solution:** Factorio mod dependencies, Arma CfgPatches, Bethesda masters, NS2 consistency.
- **Proposed MegaMod adaptation:** OAL emits resolved DAG and canonical lockfile; runtime rejects missing/unsupported closures; unload only between matches.
- **Benefits:** reproducible sessions and saves. **Costs:** package manager UI/loading work. **Risks:** dependency combinatorics; limit v1 to required dependencies and exact closure hash.
- **Dependencies:** X2 registry and package manifest version. **Prototype needed?** Yes, missing dependency/cycle/mismatch cases.
- **Open Asset Lab impact:** dependency resolver and explanatory graph. **Networking impact:** handshake compares gameplay closure. **Android impact:** startup work only; bound package count and memory.

## LATER

### L1. Save/migration schema for scripted worlds

- **Problem:** definition renames and mutable scripted state can break saved experiences.
- **Observed solution:** Factorio migrations/storage, Bethesda master/reference stability, tModLoader paired save/load hooks.
- **Proposed MegaMod adaptation:** save canonical definition IDs, package lockfile, placed IDs, typed mutable state, and explicit schema migrations.
- **Benefits:** durable worlds and mod updates. **Costs:** migration tooling and compatibility matrix. **Risks:** unrecoverable old saves; keep backup/preview.
- **Dependencies:** X1–X4. **Prototype needed?** Yes, rename one ID and migrate one saved door.
- **Open Asset Lab impact:** migration/alias validation. **Networking impact:** host loads canonical state then sends snapshots. **Android impact:** bounded save parsing at load, no hot-loop cost.

### L2. Client cosmetic scripting and narrow prediction

- **Problem:** creator effects and responsiveness may need local behavior after host Lua exists.
- **Observed solution:** GMod/NS2 client/server realms, Source prediction.
- **Proposed MegaMod adaptation:** separate read-only cosmetic Lua; predict only measured owner-controlled actions with C reconciliation.
- **Benefits:** responsiveness and creator presentation. **Costs:** two realms, debugging, replays. **Risks:** authority leakage/desync; capability-separate APIs.
- **Dependencies:** X3 plus network metrics. **Prototype needed?** Yes, one movement/weapon action under simulated loss.
- **Open Asset Lab impact:** realm validation and asset preloading. **Networking impact:** prediction/reconciliation fields. **Android impact:** second VM/bytecode budget must be justified.

### L3. Extensible package container

- **Problem:** repeated OALMAP/OALASSET forks may duplicate manifests and loaders.
- **Observed solution:** content compiler outputs across Factorio/Arma/Source; packaged typed records across Bethesda.
- **Proposed MegaMod adaptation:** only when another real section demands it, evolve toward bounded typed chunks with optional-skip and required-feature rules, preserving v1/v2 loaders.
- **Benefits:** extensibility. **Costs:** format migration and dual-loader period. **Risks:** breaking private bundles; coordinate regression across both repos.
- **Dependencies:** X2/X4 and measured format pressure. **Prototype needed?** Yes, round-trip one original package and legacy compatibility.
- **Open Asset Lab impact:** compiler and validator changes. **Networking impact:** manifest/hash version update. **Android impact:** benchmark mmap/decompression/startup memory.

## EXPERIMENTAL

These are future research/proof targets, not the next implementation
milestones. Before any donor-inspired prototype, read the
[research index](README.md), relevant reports and
[capability direction](../GAMEPLAY_CAPABILITY_MODULES.md); preserve
**Fact / Inference / Unknown** and check code/asset rights.

### E1. Precomputed structural destruction

- **Problem:** dynamic destruction is expensive for an Android host and network.
- **Observed solution:** RF:G's authored destruction experience and Space Engineers' physics components; internal Geo-Mod algorithm remains unknown.
- **Proposed MegaMod adaptation:** OAL-generated or hand-authored support graph over a tiny original mesh; C host computes bounded detach events, clients spawn cosmetic debris.
- **Benefits:** interactive destruction with small runtime state. **Costs:** authoring tools, collider/fragment generation. **Risks:** bad support analysis and body explosion.
- **Dependencies:** X1 physics/event model, mobile budget measurements. **Prototype needed?** Yes, explicitly exploratory.
- **Open Asset Lab impact:** optional structural IR and graph validator. **Networking impact:** compact break IDs and late-join state. **Android impact:** strict body/piece caps and profiling.

### E2. Internet transport evaluation

- **Problem:** direct-IP LAN protocol lacks several Internet-scale services.
- **Observed solution:** GameNetworkingSockets reliable/unreliable channels, fragmentation, encryption, ICE/NAT.
- **Proposed MegaMod adaptation:** benchmark an adapter only after real Internet requirements and loss tests; keep gameplay codec independent.
- **Benefits:** mature connection services if needed. **Costs:** dependency, Android binary size, license audit. **Risks:** transport migration bugs and operational relay needs.
- **Dependencies:** N3 metrics, X4 content match, Internet test target. **Prototype needed?** Yes, adapter A/B under loss and mobile build measurement.
- **Open Asset Lab impact:** only package lockfile/hash distribution. **Networking impact:** major transport layer evaluation. **Android impact:** size, CPU, battery measurement required.

### E3. One radically different gameplay capability [Someday]

A bounded alternate locomotion experiment, such as skating, could test
the shared roster and a clean native/Lua boundary after a real product
need exists. Possible acceptance: normal FPS play remains intact; one
alternate locomotion works for more than one normalized character;
generic APIs contain no donor names; host and client agree and late join
reconstructs state; Android CPU/frame cost is measured; original or
synthetic content proves the capability is not tied to a donor game.
Skate physics, tricks and character retargeting have not been researched
or implemented by this recommendation.

### E4. Party Playlist validation [Someday]

When enough capabilities and Lua/data mode rules exist, a tiny 2–3-round
playlist could test recombination. Possible acceptance: the same roster
and session score persist without an Engine restart; previous-round state
is cleared; at least two rounds use different capability combinations;
rules live primarily in Lua/data; peers agree on round, score and state;
and generic APIs carry no donor terms. This is an architectural stress
test, not a backlog of microgames or a scheduled milestone.

## AVOID

| Rejected idea | Why | Better path |
| --- | --- | --- |
| Wholesale ECS rewrite | Existing specialized pools work; no measured need for universal storage | X1 adds only needed capabilities and handles |
| Large C++/C#-style entity inheritance tree | Poor fit for C, Android binary size, and independent mods | Immutable definitions plus C pools and event callbacks |
| Silent last-wins overrides/load-order IDs | Bethesda/xEdit shows conflict and save fragility | Namespaced IDs; explicit typed patches only when needed |
| Run source-game scripts or assets directly | Legal/provenance, security, and semantic mismatch | OAL normalizes permitted data; creator-authored MegaMod scripts only |
| Deterministic Lua on every peer or client-owned physics | More desync/authority surface than current host model needs | Host gameplay scripts and state/event replication |
| Internet transport replacement now | Current LAN stack works and missing requirements are unmeasured | N3 metrics, then E2 evaluation |
| Runtime parsing of every source format | Expands phone CPU, attack surface, and dependencies | OAL compiles generic bounded content |
| Whole-world dynamic fracture now | RF:G internals unverified; high mobile/network cost | E1 tiny original-content experiment |

**Licensing:** no recommendation depends on reusing Space Engineers, Source, NS, Terraria, Arma, PZ, Bethesda, or RF:G code or game data. The [resource index](README.md#resource-index-and-license-boundary) records source restrictions. Any future importer needs its own rights and format feasibility review.
