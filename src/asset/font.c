#include "font.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define FONT_CHARACTERS 124u   /* TagReflexive of FontCharacter */
/* TagDataOffset is {size, flags, file offset, POINTER, unused}: the cache
 * addresses the blob by the pointer at +12, not the file offset at +8. */
#define FONT_PIXELS     136u
#define FONT_PIXELS_PTR  12u
#define FONT_ASCENT       4u
#define FCHAR_SIZE       20u
#define FCHAR_CHARACTER   0u
#define FCHAR_WIDTH       2u
#define FCHAR_BITMAP_W    4u
#define FCHAR_BITMAP_H    6u
#define FCHAR_ORIGIN_Y   10u
#define FCHAR_PIXELS     16u   /* int32 into the blob */

#define HTA_TAG_HUDG HTA_FOURCC('h','u','d','g')
#define HUDG_FULLSCREEN_FONT 72u   /* TagDependency -> font */

uint32_t hta_hud_font(const hta_cache *c)
{
    if (!c) return 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_TAG_HUDG) continue;
        uint32_t off;
        if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) continue;
        uint32_t id = 0;
        if (!hta_rd_u32(c, off + HUDG_FULLSCREEN_FONT + 12u, &id)) continue;
        if (id && id != 0xFFFFFFFFu) return id;
    }
    return 0;
}

void hta_font_digits_free(hta_font_digits *f)
{
    if (!f) return;
    free(f->rgba);
    memset(f, 0, sizeof(*f));
}

bool hta_font_digits_load(const hta_cache *c, uint32_t font_tag_id,
                          hta_font_digits *out, char *err, size_t errlen)
{
    if (!c || !out) return false;
    memset(out, 0, sizeof(*out));
    int32_t ti = hta_cache_find_tag_by_id(c, font_tag_id);
    if (ti < 0) {
        if (err) snprintf(err, errlen, "no such font tag");
        return false;
    }
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return false;
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return false;

    hta_rd_u16(c, base + FONT_ASCENT, (uint16_t *)&out->ascent);

    /* The pixel blob is addressed by the POINTER in the tag data offset,
     * the same way every other cache pointer is. */
    uint32_t pixels_ptr = 0, pixels_off = 0;
    if (!hta_rd_u32(c, base + FONT_PIXELS + FONT_PIXELS_PTR, &pixels_ptr)) return false;
    if (!hta_cache_ptr_to_offset(c, pixels_ptr, &pixels_off)) {
        if (err) snprintf(err, errlen, "font pixel data does not translate");
        return false;
    }

    uint32_t n = 0, p = 0, off = 0;
    if (!hta_read_reflexive(c, base + FONT_CHARACTERS, &n, &p) || !n) return false;
    if (!hta_cache_ptr_to_offset(c, p, &off)) return false;

    /* Find the ten digits and measure the atlas they need. */
    struct { uint32_t entry; uint16_t w, h; int16_t adv, oy; int32_t pix; } g[10];
    memset(g, 0, sizeof(g));
    int found = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t ch = 0;
        if (!hta_rd_u16(c, off + i * FCHAR_SIZE + FCHAR_CHARACTER, &ch)) break;
        if (ch < '0' || ch > '9') continue;
        int d = ch - '0';
        if (g[d].w) continue;
        hta_rd_u16(c, off + i * FCHAR_SIZE + FCHAR_WIDTH,    (uint16_t *)&g[d].adv);
        hta_rd_u16(c, off + i * FCHAR_SIZE + FCHAR_BITMAP_W, &g[d].w);
        hta_rd_u16(c, off + i * FCHAR_SIZE + FCHAR_BITMAP_H, &g[d].h);
        hta_rd_u16(c, off + i * FCHAR_SIZE + FCHAR_ORIGIN_Y, (uint16_t *)&g[d].oy);
        hta_rd_u32(c, off + i * FCHAR_SIZE + FCHAR_PIXELS,   (uint32_t *)&g[d].pix);
        if (g[d].w && g[d].h) found++;
    }
    if (found != 10) {
        if (err) snprintf(err, errlen, "font has %d of 10 digits", found);
        return false;
    }

    uint32_t aw = 0, ah = 0;
    for (int d = 0; d < 10; d++) {
        aw += g[d].w + 1u;              /* a column of gap stops bleeding */
        if (g[d].h > ah) ah = g[d].h;
    }
    out->rgba = (uint8_t *)calloc((size_t)aw * ah * 4u, 1);
    if (!out->rgba) return false;
    out->atlas_w = aw;
    out->atlas_h = ah;

    uint32_t x = 0;
    for (int d = 0; d < 10; d++) {
        for (uint32_t yy = 0; yy < g[d].h; yy++) {
            for (uint32_t xx = 0; xx < g[d].w; xx++) {
                uint8_t v = 0;
                hta_rd_u8(c, pixels_off + (uint32_t)g[d].pix + yy * g[d].w + xx, &v);
                uint8_t *px = &out->rgba[(((size_t)yy * aw) + x + xx) * 4u];
                /* White, with the glyph's coverage as alpha: the HUD tints
                 * it the same way it tints every other element. */
                px[0] = px[1] = px[2] = 255;
                px[3] = v;
            }
        }
        out->digit[d].w = g[d].w;
        out->digit[d].h = g[d].h;
        out->digit[d].advance = g[d].adv;
        out->digit[d].origin_y = g[d].oy;
        out->digit[d].u0 = (float)x / (float)aw;
        out->digit[d].u1 = (float)(x + g[d].w) / (float)aw;
        out->digit[d].v0 = 0.0f;
        out->digit[d].v1 = (float)g[d].h / (float)ah;
        x += g[d].w + 1u;
    }
    out->loaded = true;
    return true;
}
