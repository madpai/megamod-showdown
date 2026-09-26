# N2: canonical proposed content ID grammar

**Status:** read-only audit implemented in Open Asset Lab at `1dabd68`; no runtime package, save, or network migration. [OAL's grammar and audit results](https://github.com/madpai/open-asset-lab/blob/main/docs/CONTENT_IDS.md) are the current source of truth. This note records the architectural consequences and supersedes the earlier broader draft.

Use **`namespace:type/name`**. Examples: `core:weapon/rocket_launcher`, `source:world/de_dust2`, `original:character/security_robot`, `mypack:weapon/plasma_burst`. The earlier `kind:namespace.name` sketch is retired. The complete canonical byte string is definition identity; a placed object's ID is separate and unique within its world.

| Part | Audited recommendation |
| --- | --- |
| Namespace | `[a-z][a-z0-9_]*`, at most 40 ASCII bytes; one owning package lineage per namespace in a resolved set. |
| Type | A deliberately registered category, at most 24 bytes. The **current audit** accepts `world`, `character`, `weapon`, `sounds`; future `ability`, `vehicle`, `world_entity`, etc. require explicit schema additions. |
| Name | `[a-z][a-z0-9_]*`, at most 48 ASCII bytes, unique within namespace and type. The one `/` is a separator, not hierarchy or a filesystem path. |
| Whole ID | At most 96 bytes, exactly one `:` and one `/`. Lowercase ASCII, digits and underscore only. Hyphens, dots, whitespace, Unicode, extra slashes, escapes and empty segments are invalid. |
| Comparison | Exact canonical bytes; no case folding, Unicode normalization, percent decoding, path cleanup, silent truncation or automatic spelling repair. |
| Ownership and duplicates | A package may define IDs only in its declared namespace; cross-package references require declared dependencies. Duplicate full IDs, including identical definitions from different packages, are errors. Package version is metadata, not part of the ID. |
| Legacy names and aliases | Current `map_id`, OALASSET `name`, filenames, display labels, Halo tag fragments and roster indices remain legacy fields, not IDs. The read-only audit proposes mappings and reports collisions. A real migration requires explicit aliases for old references; never infer an alias from filename similarity. Reject alias cycles, duplicate sources, live-ID collisions and missing targets if aliases are introduced. |
| Invalid input | The audit reports package, legacy name, proposed ID and reason without rewriting files. A future authoring compiler should fail invalid authored IDs/references; a future runtime should reject invalid compiled IDs rather than repair them. |

The audited bundle has 35 packages with no declared namespace, one duplicate `cs_office`/`cs_office_lit` map identity, and 53 legacy loadout/base references requiring explicit mapping. All other current names are canonical under the audited segment rules. OALASSET's fixed `name[48]` cannot hold a whole 96-byte ID; any future runtime adoption needs a separate field/storage, not a reinterpretation of `name`.

The grammar is an authoring recommendation only. Protocol v10 still compares ordered legacy content through the [current compatibility contract](../CONTENT_COMPATIBILITY.md); it does not transmit these IDs. A future stable-ID negotiation can replace order-sensitive wire assumptions only when the packet contract changes.
