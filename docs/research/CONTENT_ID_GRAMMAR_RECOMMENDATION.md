# N2: canonical content ID grammar

**Status:** research recommendation, 2026-09-26. This settles the research syntax; it does not change OALMAP/OALASSET, saves, network packets, or runtime registration. N2's read-only audit should test the rules against actual legacy names before a format adopts them.

## Canonical form

`namespace:type/name`, for example `core:weapon/rocket_launcher`, `source:map/de_dust2`, `original:character/security_robot`, or `mypack:ability/plasma_burst`. The complete byte string is the definition's identity. `kind:namespace.name` in the earlier entity comparison was an inconsistent sketch and is superseded here. A placed object uses a separate world-local placed ID; it may refer to a definition by this canonical content ID.

The proposed ASCII grammar is:

```text
id          = namespace ":" type "/" local_path
namespace   = lower (lower | digit | "_" | "-")*
type        = lower (lower | digit | "_")*
local_path  = segment ("/" segment)*
segment     = lower (lower | digit | "_" | "-")*
lower       = "a" ... "z"
digit       = "0" ... "9"
```

| Rule | Recommendation |
| --- | --- |
| Length | At most 96 ASCII bytes for the complete ID; namespace ≤40, type ≤24, local path ≤48, each segment ≤32, and at most four local segments. All limits apply together; reject overflow rather than truncating. These are proposed bounds, not current loader limits. |
| Type | A registered closed category, initially `map`, `character`, `weapon`, `projectile`, `ability`, `vehicle`, `material`, `sound`, `animation`, `skeleton`, `world_entity`, `game_mode`, or `mutator`. An extension adds a type through a versioned schema, not by accepting arbitrary spelling. `map` denotes a world/map definition; `world_entity` denotes a reusable placed-entity kind. |
| Case and normalization | The ID is already lowercase ASCII. Compare and hash its exact bytes. No Unicode normalization, case folding, percent decoding, path cleanup, whitespace trimming, or implicit replacement of punctuation. OAL may offer a proposed lowercase spelling as a diagnostic, but must not silently convert an authored ID. |
| Reserved characters | Exactly one `:` and at least the first `/` are structural. Extra `/` separate local segments. `.`, `\\`, `@`, `#`, `%`, whitespace, control characters and non-ASCII are invalid. Empty segments, leading/trailing `/`, and `.`/`..` paths cannot occur. |
| Local path meaning | Additional `/` is part of the stable local identity and can group names for authors. It grants no filesystem hierarchy, inheritance, ownership, fallback lookup, or relative-reference semantics. Renaming a segment changes the ID. |
| Package ownership | A package declares one namespace, preferably equal to its canonical package ID, and may define only IDs under it. The resolved package closure has exactly one owner per namespace. Version is package metadata, not part of an ID; a later version of the same lineage may retain IDs. A reference to another namespace requires a declared dependency. |
| Duplicates | Two definitions with the same full ID are an error even if bytes are identical or one would win by load order. A type change also changes the ID; no two package lineages may claim one namespace in a resolved closure. Diagnose exact duplicates and candidate collisions created by suggested legacy normalization. |
| Aliases | No automatic aliases and no general alias graph in N2. When a real saved or cross-package reference must migrate, allow an explicit canonical `old_id → new_id` map owned by the namespace's package. Reject cycles, duplicate sources, live-ID collisions, missing targets, and cross-namespace redirects without an explicit migration/dependency policy. Never infer aliases from filenames or labels. |
| Invalid input | OAL fails validation of an authored ID or reference with package, source location, rejected bytes and reason. A future runtime rejects an invalid compiled ID/required reference at load; it must not silently repair, truncate or substitute another definition. |

Legacy `map_id`, OALASSET `name`, Halo tag fragments, filenames, display labels and array positions remain legacy lookup/provenance data. They do not become canonical IDs merely because they happen to parse. N2 can report proposed mappings and collisions without rewriting packages. Existing packages continue through their current loader path until an explicit migration. A 96-byte ID needs new storage: OALASSET `name[48]` is too small.

**Why this shape:** [Factorio prototype names](FACTORIO.md) support stable type/name lookup; [tModLoader ownership](TMODLOADER.md) supports mod-scoped identity; [Arma dependencies](ARMA3.md) support explicit cross-package references; [Bethesda/xEdit](BETHESDA_XEDIT.md) shows the cost of load-order identity. These are architectural comparisons, not claims that those engines use this spelling. [N1](CURRENT_RUNTIME_CONTENT_BOUNDARY_REVIEW.md) shows current path, label and ordinal lookup are distinct migration cases.

**N2 acceptance evidence:** synthetic own-namespace and declared cross-package references; undeclared reference; duplicate full ID; case-collision suggestion; invalid and overlong strings; explicit legacy mapping; and a file move/rename that leaves a deliberately authored ID unchanged. Diagnostics identify package, source location, target and reason.
