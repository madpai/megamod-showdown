# Content compatibility: what two peers must agree on

**Status:** authoritative for protocol v10 (2026-09-26). Verified against
the code at the commit that introduced it; supersedes the N1 questions in
[the boundary review](research/CURRENT_RUNTIME_CONTENT_BOUNDARY_REVIEW.md)
where they differ. File and line references are to that commit.

## The rule

The LAN protocol names content by **position** in tables each peer builds
for itself. Dense positions are fine inside one process; across peers they
are only valid if both built the same table from the same inputs in the
same order. So a joiner is admitted only when:

1. **map check** -- `cache.crc32 ^ world_ext.key` (`src/app/match_load.c`,
   `hta_match_begin`) equals the host's: a 32-bit check combining the Trial
   cache's **stored header CRC** and FNV-1a of the imported world manifest,
   `src/asset/external_map.c`). It does not cover the `.oalmap` binary
   geometry, collision or spawn payload. See the [world-key trace](research/V10_FINGERPRINT_ARCHITECTURE_REVIEW.md#exact-world-package-key-guarantee).
2. **content fingerprint** (new in v10, `src/app/compat.c`) equals the
   host's: the same imported characters and weapons, with the hashed gameplay
   fields in the same order and float values in the same rounding buckets.

A host that knows neither (a test harness, `megamod-fakehost`) takes anyone.
Before v10 a joiner sending a map check of 0 skipped check 1; it is now
refused ("not the host's map").

## Tables the protocol indexes, and what builds them

| Table | Built from | Order | Guard |
|---|---|---|---|
| Trial weapons, flag weapon | Trial tags (`game.c` `hta_game_load`) | tag order | map check |
| Projectile pools | Trial projectiles, grenade; vehicle triggers' pools | registration order | map check |
| Imported weapons (appended to the weapon roster) | `weapons/*.oalasset` | file name (strcmp) | **fingerprint** |
| Hero-ability weapons (appended) | characters with `ability_base` | character order | **fingerprint** (+ map check for the base tag) |
| Vehicle trigger weapons (appended) | Trial vehicles | placement order | map check + fingerprint (their index shifts with the imported count) |
| Characters (`game.characters`) | `characters/*.oalasset` with kind "character" | file name | **fingerprint** |
| Vehicles, seats, hulls | Trial scenario placements | placement order | map check |
| Items, item choices | Trial scenario | spawn order | map check (choice now bounds-checked on the wire) |
| Breakable props (GAME prop mask) | `.oalmap` `breakables` in manifest order | manifest order | map check (package key) |
| Units, peers, flag carrier, winner | runtime slots | host-assigned | host-authoritative, not content |

Imported weapons whose `base` is not found are dropped on every peer alike
(same Trial map, same file): deterministic, covered by both checks. The
64-weapon cap can cut vehicle triggers; also deterministic from the inputs.

## Fields on the wire that are content positions

| Packet.field | Table | Mismatch effect before v10 |
|---|---|---|
| WORLD `entity.character` (kind>>2, idx+1) | characters | wrong body, voice, hero effects; host stats snap position |
| WORLD `entity.carry[2]` | weapon roster | wrong gun model / HUD / sound (index past the roster ignored) |
| WORLD `entity.weapon` | weapon roster | unused by clients today |
| WORLD `item_present` / `item_choice[]` | items | choice > 7 read past an 8-entry array (**fixed**: rejected by `hta_net_world_unpack`) |
| CONTROL `loadout[2]` | the **joiner's** roster read as the **host's** | host hands out a different weapon |
| CONTROL `character` (idx+1) | the joiner's characters read as the host's | wrong hero; wrong unique-hero refusal |
| FX FIRE / IMPACT `weapon` | weapon roster incl. vehicle/ability | wrong tracer, sound, particles; hero effects on plain shots |
| FX DETONATE `weapon` (= pool) | pools | wrong blast sound / particles |
| FX WRECK `weapon` (= car & 31) | vehicles | unused by clients |
| PROJECTILES `pool` / `slot` | pools (12/13: the host's own) | see "remaining" below |
| VEHICLES `index`, `occupant[]` | vehicles, units | -- (map check) |
| DROPS `weapon` | weapon roster | wrong gun on the ground |
| GAME `prop_broken` bits, `hull[32]` | props, vehicles | -- (map check) |

Strings, not positions (safe): the class/loadout choice from the menus
(`nativeSetLoadout`, `nativeChooseClass`) and kill-feed text.

## The fingerprint (schema 1)

`hta_content_fingerprint()` in `src/app/compat.c` -- FNV-1a 64 over:

- `HTA_CONTENT_SCHEMA` (u32), then the character roster, then the weapon
  roster, each a u32 count and its entries **in roster order** (file name
  order: the order the network indexes);
- per entry, each field tagged with a one-byte field number:
  kind, name, display name, base, rounds_per_second, damage_scale,
  spread_scale, magazine, reserve, melee, mount, fly_speed, reload_rounds,
  reload_seconds, recharge, loadout[0..1], body_health, body_shield,
  body_damage, body_speed, can_fly, fly_damage, hero_group, unique_limit,
  ability_name, ability_base, ability_damage/cooldown/duration/interval/
  radius/force/cone, ability_beam, knockback;
- integers and booleans little-endian u32; floats rounded to 1/10000 as
  i32 (clamped, NaN fixed), so ARM and x86 agree on the fingerprint;
  strings length-prefixed,
  exactly as stored (no case folding: loadouts match display labels
  exactly, so a relabel is a gameplay change).
- 0 is never produced (0 means "none").

The 1/10000 rounding is **fingerprint equivalence only**. Gameplay uses
the loaded float without this rounding. Distinct values within one rounding
bucket can therefore affect movement or damage while passing the v10 content
check; see the [targeted case](research/V10_FINGERPRINT_ARCHITECTURE_REVIEW.md#targeted-float-quantization-case).

**Left out (cosmetic):** models, sounds, view mirroring, mount placement,
hold type, crosshair, ability colour, file paths, provenance. A different
skin with the same numbers joins; different stats, labels, sets or order
do not. The UI sound pack is not included.

**Order is part of the contract today.** Two peers with the same assets
under file names that sort differently are refused: their positions really
differ. Canonical IDs (N2) would let the host send a name-to-index table
instead; not yet.

## Handshake (v10)

HELLO grows from 8 to 16 bytes: nonce, map check, fingerprint (two u32,
low first). The host refuses before allocating a player slot, with REJECT
reason `HTA_NET_REJECT_MAP` (2) or the new `HTA_NET_REJECT_CONTENT` (3),
counts it (`stats.refused`, `last_refusal`), and the tick logs
`[net] refused a joiner: different characters/weapons`. The phone shows
"CHARACTERS/WEAPONS DO NOT MATCH"; `megamod-join` prints
`REFUSED (characters/weapons differ from the host's)`. A client ignores a
REJECT with a reason it does not know. v9 and v10 packets are refused by
each other's header check, as any version change: the joiner never
connects, nothing is reinterpreted.

`megamod-content` prints a machine's fingerprint; `megamod-join` computes
its own from `--bundle` / `HTA_BUNDLE_DIR` (none: only a host with no
imported content takes it).

**Consequence for guests:** a friend's plain (guest) APK carries no
imported characters or weapons, so it is refused by a personal build that
has them. Before v10 it got in and saw stand-ins or wrong bodies. Letting
an empty-roster guest in with stand-ins would be a deliberate policy change
(the host would have to stop naming characters it cannot show); not done.

## Remaining position assumptions (known, not fixed here)

- **PROJECTILES pools 12/13** mean "the host's own launcher / grenades" but
  a joiner draws them with *its own* held weapon's pool: the host's
  first-person rockets can look wrong on a joiner. Visual only.
- **Hero availability bits** are set by `game.characters` index
  (`hero_occupancy`) but tested by `imp_char` index
  (`nativeCharacterAvailable`); these differ if a `characters/` file is not
  of kind "character". Local UI; both sides identical under the fingerprint.
- **World package key** hashes the whole manifest, provenance included: a
  provenance-only change refuses otherwise identical maps (safe direction).
  Valid binary geometry, collision or spawn differences outside that manifest
  are not detected by this key.
- Kill feed and HUD strings are sent as text; no issue.

## Found alongside (fixed)

- **Phantom host body (step 4 regression, `ab2b116`).** The tick pumps the
  server before the host's own client hears WELCOME; `hta_host_peers`
  recognised the host's client only by id, so for one frame it was a
  stranger and got a remote body ("Player 1") that stayed. Now also
  recognised by its HELLO nonce (`tests/test_host_net.c`).
