# N2: first canonical content ID grammar

**Status:** RESEARCH ONLY, 2026-09-26. This is a recommendation for Claude's small validation experiment; it does not change OALMAP/OALASSET or existing saves/network packets.

## Recommendation

Use **`namespace:type/name`**, for example `megamod:weapon/ion_rifle` and `lab_demo:world/relay_test`. The package declares one `namespace` that it owns; definitions in that package must use it. A dependency may reference another package's ID only if that package is declared as a dependency. Treat the complete string as the authored identity; assign dense numeric indices only after loading a resolved content set. A placed object should have a separate ID unique within its world, and a reference to its definition by canonical content ID.

| Rule for the first experiment | Recommendation |
| --- | --- |
| Character set | ASCII lowercase `a-z`, digits `0-9`, underscore `_`; allow a single hyphen `-` within namespace or name if existing names need it. The `:` and `/` are separators only. No spaces, dots, Unicode, percent escapes, path components, or additional separators. |
| Segment shape | Each of namespace, type and name starts with a letter, then contains allowed characters; no empty segment. Reserve `type` as one known registry category such as `world`, `weapon`, or `character`. |
| Length | At most 96 ASCII bytes total; at most 40 for namespace, 24 for type, 48 for name. These are proposed N2 limits, to be checked against actual current names before adoption. Reject overflow instead of truncating. |
| Case and normalization | Canonical form is already lowercase ASCII. OAL may *suggest* a lowercase candidate for a legacy name, but must not silently fold two authored IDs into one. Hash and compare the exact canonical byte string. |
| Ownership and duplicates | One owner per namespace in the resolved package set. Reject duplicate full IDs, including duplicates that arise through aliases. A changed package version keeps the same namespace if it is the same package lineage; version is package metadata, not part of a definition ID. |
| Files and display | File paths, source engine class/tag names, presentation labels, and binary array indices are never identity. Moving an asset file or translating a label must not rename content. |
| Aliases | No general alias graph in N2. Permit a small explicit `old_id → new_id` migration list only when a real legacy reference needs it. Reject cycles, duplicate old IDs, missing targets and collisions with live IDs. Never infer an alias from filename similarity. |
| Legacy packages | Continue loading the current packages through their existing path/name rules. For the experiment, produce a report with proposed IDs and collisions; require an explicit mapping before using them in saved state or package hashes. Do not silently rewrite legacy manifests. |

**Why this shape:** [Factorio prototype names](FACTORIO.md) show the value of stable type/name lookup; [tModLoader ownership](TMODLOADER.md) shows mod-scoped identity; [Arma addon dependencies](ARMA3.md) make cross-package references explicit; [Bethesda/xEdit](BETHESDA_XEDIT.md) exposes the cost of identity tied to load order and conflict resolution. These are architectural comparisons, not claims that those engines use this syntax. The current [N1 review](CURRENT_RUNTIME_CONTENT_BOUNDARY_REVIEW.md) shows that `map_id` comes from a BSP stem, assets lack ownership, loadouts use labels, and network ordinals depend on filename sort.

**N2 acceptance evidence:** with original synthetic packages, validate one own-namespace definition, one declared cross-package reference, one undeclared reference, one duplicate ID, one case collision suggestion, one invalid/overlong string, and one legacy mapping. Show diagnostics with package, source location, target ID and reason. A second build with renamed/moved files should produce the same canonical IDs. This validates grammar and tooling without promising save or multiplayer compatibility yet.

**Confidence:** MEDIUM. **MegaMod applicability:** CRITICAL. **OAL applicability:** CRITICAL. **Status:** RESEARCH ONLY. **Implementation urgency:** NOW. The precise length and hyphen rule should follow a read-only inventory of current authored names; the separation from paths, display labels and dense indices is high confidence.
