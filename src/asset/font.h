/* Halo's HUD numbers are drawn with a FONT, not a digit bitmap.
 *
 * `hud_globals` names two: a fullscreen font and a splitscreen one. On the
 * Trial the fullscreen font is `ui\large_ui`, whose digits are 12x13.
 *
 * A `font` tag is a character table plus one flat blob of 8-bit coverage.
 * Each character says where its glyph starts in that blob and how big it
 * is; there is no sheet and no packing to undo. Font 156, FontCharacter 20,
 * both reconcile.
 *
 * Only the ten digits are wanted here, so that is all this builds: one
 * small RGBA atlas, white with the glyph's coverage as alpha, which the HUD
 * tints like any other element.
 */
#ifndef HTA_FONT_H
#define HTA_FONT_H

#include "cache.h"
#include "bsp.h"

typedef struct {
    uint16_t w, h;          /* the glyph's own pixels */
    int16_t  advance;       /* how far the pen moves after drawing it */
    int16_t  origin_y;      /* baseline offset, from the top of the cell */
    float    u0, v0, u1, v1;
} hta_glyph;

typedef struct {
    hta_glyph digit[10];
    uint8_t  *rgba;         /* atlas_w * atlas_h * 4, malloc'd */
    uint32_t  atlas_w, atlas_h;
    int16_t   ascent;
    bool      loaded;
} hta_font_digits;

/* The `font` the HUD draws numbers with: hud_globals' fullscreen font.
 * Returns 0 when there is no hud_globals in the cache. */
uint32_t hta_hud_font(const hta_cache *c);

/* Builds the ten digits of a font into one atlas. */
bool hta_font_digits_load(const hta_cache *c, uint32_t font_tag_id,
                          hta_font_digits *out, char *err, size_t errlen);
void hta_font_digits_free(hta_font_digits *f);

#endif
