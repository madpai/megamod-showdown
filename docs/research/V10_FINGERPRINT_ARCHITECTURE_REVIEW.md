# Independent review of the landed protocol v10 fingerprint

**Status:** reconciled against MegaMod `ac1d010` and [the authoritative compatibility contract](../CONTENT_COMPATIBILITY.md), 2026-09-26. This is an assessment, not an alternate v10 wire specification. No runtime/network code was changed here.

## Actual v10 behavior

[`hta_content_fingerprint`](../../src/app/compat.c) hashes the loaded **imported** character array, then the loaded imported weapon array, each with its count and in filesystem roster order. It uses schema `1`, tagged fields, little-endian 32-bit integers/booleans, length-prefixed stored strings, gameplay names/labels/base/loadout/body/ability/knockback/stat fields, and float values rounded to units of 1/10,000. It excludes meshes, sounds, crosshair, mount placement, color, file paths and provenance. FNV-1a produces 64 bits (zero is remapped to one); HELLO sends the low and high halves. [`session.c`](../../src/net/session.c) checks map and content values before allocating a player slot. Tests cover changed counts, order, labels and several stats, cosmetic crosshair variation, mismatch rejection, and a zero joiner value.

The fingerprint is a **per-match legacy roster compatibility detector**, not a canonical package hash or proof of asset possession. The selected world is checked separately by `cache.crc32 XOR world_ext.key`; the world key is discussed below. Built-in weapons, hidden ability shots, projectile pools, vehicles, pickups and props are reconstructed from the same Trial cache, world package and imported inputs by the same protocol-v10 game code. The fingerprint does not serialize the final resolved `game.weapons[]` or `game.pools[]` directly; that deterministic reconstruction is the current invariant and should remain a regression target whenever roster construction changes.

| Must match for current indexed behavior | May differ when independent | Not directly hashed by v10 |
| --- | --- | --- |
| Ordered imported character/weapon arrays, their names and labels, base/loadout resolution strings, and enumerated gameplay stats | Cosmetic models, textures, sounds, skins, crosshair, mount placement and color | Source/staging paths, provenance URLs, timestamps, warnings and import dates |
| Selected Trial/world compatibility key and protocol schema | Locally chosen presentation for a host-owned event | Full OALMAP binary geometry, final resolved roster bytes, and package closure/lockfile |

Display labels are still compatibility inputs because current loadouts resolve by label. The table describes what v10 actually checks; it does not declare all omitted bytes gameplay-equivalent.

## Prior review warnings, resolved against the landed code

| Earlier warning | Status | Current conclusion |
| --- | --- | --- |
| Zero/missing joiner value bypass | **RESOLVED BY IMPLEMENTATION** for a real match | A host with initialized nonzero map and content values refuses zero/mismatched joiner values before slot allocation. There is an explicit zero-host exception for `megamod-fakehost`/scripted test hosts, which have no loaded gameplay closure; do not use that exception as a production match policy. |
| Imported arrays versus final resolved roster | **PARTIALLY APPLIES** | The arrays are hashed, then current common code derives hidden ability, vehicle-gun and projectile entries from those arrays plus the map/cache. This is sufficient for the tested current construction, but a future independent roster input requires a new schema/test or a final-roster projection. |
| Float rounding | **STILL APPLIES** | Distinct effective gameplay binary32 values can share one quantized hash value. The runtime does not itself quantize those values before simulation. See targeted case below. |
| Provenance in world key | **STILL APPLIES**, safe direction | A provenance-only manifest edit can reject an otherwise identical world. This is over-rejection, not a false acceptance. |
| Geometry/collision coverage | **STILL APPLIES** | The key excludes the OALMAP binary payload; same checked manifest with different valid gameplay geometry can pass. See construction trace below. |
| SHA-256 required for v10 | **SUPERSEDED** | v10 detects accidental LAN mismatch; 64-bit FNV is proportionate to that goal. Cryptographic package integrity is a different requirement. |

## Targeted float-quantization case

Create two otherwise identical original OALASSET bundles. In A set a character's `body_speed` to `1.2`; in B set it to `1.20001`. Both loaded binary32 values are distinct (`0x3f99999a` versus `0x3f9999ed`), but both round to integer `12000` in `compat.c`, so their content fingerprints match. [`hta_game_body`](../../src/game/game.c) uses the unrounded value; `hta_game_body_physics` multiplies run speeds and acceleration by it. The same pattern holds for an imported weapon's `damage_scale` of `1.6` versus `1.60001`: both hash to `16000`, while `hta_game_hurt_jpt_scaled` multiplies damage by the distinct unrounded value. A targeted compatibility test should assert **equal fingerprint and different effective physics/damage** for those pairs, then drive a join to expose the current equivalence boundary.

Thus v10 *treats values in the same 1/10,000 rounding bucket as compatible*, but MegaMod does **not** currently define them as simulation-equivalent. The phrase “difference below 0.0001” is not by itself sufficient: values on opposite sides of a rounding boundary can hash differently; the two examples are within one bucket. If the desired property is identical effective gameplay values, recommend encoding canonical exact finite runtime binary32 bits (with a stated `-0` rule) in a later content-schema revision. An alternative is to quantize values at load **and use those quantized values in gameplay**. Do not change schema 1 silently. Clamping and NaN-sentinel behavior should likewise be reviewed if authored inputs can reach them.

## Why 64 bits are sufficient for this LAN detector

For two unrelated uniformly distributed 64-bit fingerprints, accidental equality is about `1/2^64` (≈5.4×10⁻²⁰); even one million distinct configurations have a birthday estimate around 2.7×10⁻⁸. FNV is not a cryptographic random oracle, so those are scale estimates, not a security proof. They are ample for ordinary accidental LAN mismatch detection. A malicious client can simply send the host's reported 64-bit value regardless of whether the digest is FNV or SHA-256. Authentication, content-addressed downloads, package integrity and future lockfiles should use a cryptographic digest and an appropriate trust model; they do not justify enlarging this v10 HELLO solely for accidental compatibility.

## Exact world-package key guarantee

OAL [`compile_map`](https://github.com/madpai/open-asset-lab/blob/main/assetlab/package.py) writes a 64-byte header, canonical JSON manifest bytes, then vertices, indices, material groups/flags, textures and binary spawn records. MegaMod [`external_map.c`](../../src/asset/external_map.c) parses all of those sections, but sets `world_ext.key` to **32-bit FNV-1a of only the `ml` manifest bytes at offset 64**. [`hta_match_begin`](../../src/app/match_load.c) XORs that key with the loaded Trial cache's stored `crc32` field; HELLO compares the resulting 32-bit value. The loader bounds-checks binary geometry and rejects malformed data, but does not hash its accepted payload into the key. OAL's full-package SHA-256 appears in its conversion report, not in the runtime join check.

Consequently, with the same Trial cache and byte-identical manifest, two OALMAP files whose **valid** vertex positions, triangle indices, collision group flags or binary spawn positions differ yield the same world key and can pass v10's world comparison. This follows directly from the byte range hashed; no hypothetical importer behavior is needed. Conversely, changing only manifest provenance changes the key and may refuse the join. The XOR and 32-bit FNV also do not independently prove equality of the Trial cache and manifest; they are compatibility checks, not content integrity. A later semantic world projection should cover gameplay geometry/collision, ordered breakable-to-group mapping and spawns while excluding provenance and cosmetic textures. No v10 world-key code was changed in this reconciliation.
