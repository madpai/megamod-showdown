/* A bounded reader for Open Asset Lab's canonical manifest JSON: the
 * primitives world_def.c (world_entities, scripts) and package.c (the X4
 * "package" member) parse with. Byte pointers only, no allocation, nesting
 * bounded (HTA_MJ_MAX_DEPTH), every read checked against the end.
 *
 * Portable C11. */
#ifndef HTA_MJSON_H
#define HTA_MJSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_MJ_MAX_DEPTH 64

typedef struct {
    const uint8_t *p, *end;
    bool bad;
    /* hta_mj_skip passes a number that is valid JSON but not finite
     * (1e999): a reader that only looks for one member (package.c) must not
     * refuse a manifest over a value another reader will judge. */
    bool lenient;
} hta_mj;

void hta_mj_ws(hta_mj *r);
/* Skips whitespace, then takes `c` if it is next. */
bool hta_mj_eat(hta_mj *r, char c);
bool hta_mj_peek(hta_mj *r, char c);
/* A string into `out` (NUL-terminated, at most cap-1 bytes; longer is an
 * error -- `out` is still terminated), or skipped when out is NULL. Escapes are accepted and, when kept,
 * must decode to printable ASCII. */
bool hta_mj_str(hta_mj *r, char *out, size_t cap);
/* A finite number (at most 39 characters). */
bool hta_mj_num(hta_mj *r, double *out);
/* Any value, skipped. Iterative over nesting, bounded depth. */
bool hta_mj_skip(hta_mj *r);

#endif
