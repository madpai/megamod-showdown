/* What two ends of a LAN match must agree on before a joiner spawns
 * (docs/CONTENT_COMPATIBILITY.md). The protocol names imported characters
 * and weapons by their position in each peer's own roster, so the rosters
 * must be the same definitions in the same order -- otherwise character #3
 * or weapon #12 means something different on each side and the match
 * shows or plays the wrong thing.
 *
 * The Trial map and the world package are matched by the map check
 * (cache CRC ^ package key) as before; this fingerprint covers the
 * imported rosters.
 *
 * Portable: no platform header here. */
#ifndef HTA_APP_COMPAT_H
#define HTA_APP_COMPAT_H

#include <stdint.h>
#include "../asset/oal_asset.h"

/* The version of what the fingerprint covers and how it is encoded. A
 * change to either is a new number: two builds that disagree on it hash
 * differently and refuse each other, which is right. */
#define HTA_CONTENT_SCHEMA 2u

/* A 64-bit FNV-1a over the rosters' gameplay values in roster order.
 * Never 0 (0 means "not computed"). Cosmetic values -- models, sounds,
 * crosshair, mount placement, colours -- are left out: a different skin
 * with the same numbers still joins. See the .c for the exact encoding. */
uint64_t hta_content_fingerprint(const hta_oal_asset *chars, uint32_t char_count,
                                 const hta_oal_asset *weaps, uint32_t weap_count);

#endif
