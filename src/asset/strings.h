/* Halo's own words: `ustr` UnicodeStringList tags.
 *
 * UnicodeStringList (12, reconciles) is one reflexive of
 * UnicodeStringListString (20, reconciles), each a TagDataOffset holding
 * NUL-terminated UTF-16LE. Blood Gulch carries the kill feed's phrasing in
 * `ui\multiplayer_game_text` and the names Halo gives unnamed players in
 * `ui\random_player_names`.
 */
#ifndef HTA_STRINGS_H
#define HTA_STRINGS_H

#include "cache.h"

/* The `ustr` at this path, or 0. Exact, case-insensitive match. */
uint32_t hta_ustr_find(const hta_cache *c, const char *path);

/* How many strings the list holds. */
uint32_t hta_ustr_count(const hta_cache *c, uint32_t ustr_tag_id);

/* String `index` as UTF-8 (anything outside ASCII becomes '?', which the
 * Trial's English strings never need). False if absent. */
bool hta_ustr_get(const hta_cache *c, uint32_t ustr_tag_id, uint32_t index,
                  char *out, size_t outlen);

#endif
