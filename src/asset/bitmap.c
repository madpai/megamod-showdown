#include "bitmap.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIM 4096u
#define MAX_TEX  256u

static void fail(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap);
}

void hta_bitmap_free(hta_bitmap *b)
{
    if (!b) return;
    free(b->rgba);
    memset(b, 0, sizeof(*b));
}

bool hta_resource_open_typed(hta_resource_map *r, const uint8_t *data, size_t size,
                             uint32_t expect_type, char *err, size_t errlen)
{
    if (!r || !data || size < 16) {
        fail(err, errlen, "resource map too small");
        return false;
    }
    uint32_t type = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                    ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    /* 1 = bitmaps, 2 = sounds, 3 = loc. Picking the wrong file is the likely
     * mistake, so name what was expected and what arrived. */
    if (type != expect_type) {
        fail(err, errlen, "resource map is type %u, expected %u (%s)",
             type, expect_type,
             expect_type == HTA_RESOURCE_BITMAPS ? "bitmaps.map" :
             expect_type == HTA_RESOURCE_SOUNDS  ? "sounds.map"  : "?");
        return false;
    }
    r->data = data;
    r->size = size;
    r->type = type;
    return true;
}

bool hta_resource_open(hta_resource_map *r, const uint8_t *data, size_t size,
                       char *err, size_t errlen)
{
    return hta_resource_open_typed(r, data, size, HTA_RESOURCE_BITMAPS, err, errlen);
}

static void unpack_565(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)(((c >> 11) & 31) * 255 / 31);
    *g = (uint8_t)(((c >> 5)  & 63) * 255 / 63);
    *b = (uint8_t)((c         & 31) * 255 / 31);
}

static void dxt_colors(uint16_t c0, uint16_t c1, int opaque4,
                       uint8_t r[4], uint8_t g[4], uint8_t b[4], uint8_t a[4])
{
    unpack_565(c0, &r[0], &g[0], &b[0]); a[0] = 255;
    unpack_565(c1, &r[1], &g[1], &b[1]); a[1] = 255;
    if (opaque4 || c0 > c1) {
        r[2] = (uint8_t)((2 * r[0] + r[1]) / 3);
        g[2] = (uint8_t)((2 * g[0] + g[1]) / 3);
        b[2] = (uint8_t)((2 * b[0] + b[1]) / 3); a[2] = 255;
        r[3] = (uint8_t)((r[0] + 2 * r[1]) / 3);
        g[3] = (uint8_t)((g[0] + 2 * g[1]) / 3);
        b[3] = (uint8_t)((b[0] + 2 * b[1]) / 3); a[3] = 255;
    } else {
        r[2] = (uint8_t)((r[0] + r[1]) / 2);
        g[2] = (uint8_t)((g[0] + g[1]) / 2);
        b[2] = (uint8_t)((b[0] + b[1]) / 2); a[2] = 255;
        r[3] = 0; g[3] = 0; b[3] = 0; a[3] = 0;
    }
}

static void blit_dxt1(uint8_t *dst, uint32_t w, uint32_t h,
                      uint32_t bx, uint32_t by, const uint8_t *src, int opaque4)
{
    uint16_t c0 = (uint16_t)(src[0] | (src[1] << 8));
    uint16_t c1 = (uint16_t)(src[2] | (src[3] << 8));
    uint8_t r[4], g[4], b[4], a[4];
    dxt_colors(c0, c1, opaque4, r, g, b, a);
    uint32_t bits = src[4] | ((uint32_t)src[5] << 8) |
                    ((uint32_t)src[6] << 16) | ((uint32_t)src[7] << 24);
    for (uint32_t py = 0; py < 4; py++) {
        uint32_t y = by + py; if (y >= h) continue;
        for (uint32_t px = 0; px < 4; px++) {
            uint32_t x = bx + px; if (x >= w) continue;
            uint32_t idx = (bits >> (2u * (py * 4u + px))) & 3u;
            uint8_t *p = dst + ((size_t)y * w + x) * 4;
            p[0] = r[idx]; p[1] = g[idx]; p[2] = b[idx]; p[3] = a[idx];
        }
    }
}

static void decode_dxt1(uint8_t *dst, uint32_t w, uint32_t h,
                        const uint8_t *src, uint32_t src_len, int opaque4)
{
    uint32_t nbx = (w + 3) / 4, nby = (h + 3) / 4;
    uint32_t need = nbx * nby * 8u;
    if (need > src_len) need = src_len;
    uint32_t i = 0;
    for (uint32_t by = 0; by < nby; by++)
        for (uint32_t bx = 0; bx < nbx; bx++, i++) {
            if ((i + 1) * 8u > need) return;
            blit_dxt1(dst, w, h, bx * 4, by * 4, src + i * 8, opaque4);
        }
}

static void decode_dxt3(uint8_t *dst, uint32_t w, uint32_t h,
                        const uint8_t *src, uint32_t src_len)
{
    uint32_t nbx = (w + 3) / 4, nby = (h + 3) / 4;
    uint32_t i = 0;
    for (uint32_t by = 0; by < nby; by++)
        for (uint32_t bx = 0; bx < nbx; bx++, i++) {
            if ((i + 1) * 16u > src_len) return;
            const uint8_t *blk = src + i * 16;
            blit_dxt1(dst, w, h, bx * 4, by * 4, blk + 8, 1);
            for (uint32_t py = 0; py < 4; py++) {
                uint32_t y = by * 4 + py; if (y >= h) continue;
                uint16_t row = (uint16_t)(blk[py * 2] | (blk[py * 2 + 1] << 8));
                for (uint32_t px = 0; px < 4; px++) {
                    uint32_t x = bx * 4 + px; if (x >= w) continue;
                    uint32_t a4 = (row >> (px * 4)) & 0xF;
                    dst[((size_t)y * w + x) * 4 + 3] = (uint8_t)(a4 * 17);
                }
            }
        }
}

static void decode_dxt5(uint8_t *dst, uint32_t w, uint32_t h,
                        const uint8_t *src, uint32_t src_len)
{
    uint32_t nbx = (w + 3) / 4, nby = (h + 3) / 4;
    uint32_t i = 0;
    for (uint32_t by = 0; by < nby; by++)
        for (uint32_t bx = 0; bx < nbx; bx++, i++) {
            if ((i + 1) * 16u > src_len) return;
            const uint8_t *blk = src + i * 16;
            blit_dxt1(dst, w, h, bx * 4, by * 4, blk + 8, 1);
            uint8_t a0 = blk[0], a1 = blk[1];
            uint8_t av[8];
            av[0] = a0; av[1] = a1;
            if (a0 > a1) {
                for (int k = 1; k <= 6; k++)
                    av[k + 1] = (uint8_t)(((7 - k) * a0 + k * a1) / 7);
            } else {
                for (int k = 1; k <= 4; k++)
                    av[k + 1] = (uint8_t)(((5 - k) * a0 + k * a1) / 5);
                av[6] = 0; av[7] = 255;
            }
            uint64_t bits = 0;
            for (int k = 0; k < 6; k++) bits |= (uint64_t)blk[2 + k] << (8 * k);
            for (uint32_t py = 0; py < 4; py++) {
                uint32_t y = by * 4 + py; if (y >= h) continue;
                for (uint32_t px = 0; px < 4; px++) {
                    uint32_t x = bx * 4 + px; if (x >= w) continue;
                    uint32_t idx = (uint32_t)((bits >> (3u * (py * 4u + px))) & 7u);
                    dst[((size_t)y * w + x) * 4 + 3] = av[idx];
                }
            }
        }
}

static void put_bgra(uint8_t *p, uint8_t b, uint8_t g, uint8_t r, uint8_t a)
{
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

bool hta_bitmap_decode_pixels(uint16_t format, uint32_t w, uint32_t h,
                              const uint8_t *src, uint32_t src_len,
                              hta_bitmap *out, char *err, size_t errlen)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!src || w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM) {
        fail(err, errlen, "bad bitmap size %ux%u", w, h);
        return false;
    }
    size_t nbytes = (size_t)w * h * 4u;
    uint8_t *dst = (uint8_t *)malloc(nbytes);
    if (!dst) { fail(err, errlen, "oom decoding %ux%u", w, h); return false; }
    memset(dst, 0xFF, nbytes);

    switch (format) {
    case HTA_FMT_DXT1: decode_dxt1(dst, w, h, src, src_len, 0); break;
    case HTA_FMT_DXT3: decode_dxt3(dst, w, h, src, src_len); break;
    case HTA_FMT_DXT5: decode_dxt5(dst, w, h, src, src_len); break;
    case HTA_FMT_R5G6B5: {
        uint32_t n = w * h; if (src_len < n * 2u) n = src_len / 2u;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t p = (uint16_t)(src[i * 2] | (src[i * 2 + 1] << 8));
            uint8_t r, g, b; unpack_565(p, &r, &g, &b);
            put_bgra(dst + i * 4, b, g, r, 255);
        }
        break;
    }
    case HTA_FMT_A1R5G5B5: {
        uint32_t n = w * h; if (src_len < n * 2u) n = src_len / 2u;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t p = (uint16_t)(src[i * 2] | (src[i * 2 + 1] << 8));
            uint8_t a = (p & 0x8000) ? 255 : 0;
            uint8_t r = (uint8_t)(((p >> 10) & 31) * 255 / 31);
            uint8_t g = (uint8_t)(((p >> 5)  & 31) * 255 / 31);
            uint8_t b = (uint8_t)((p         & 31) * 255 / 31);
            put_bgra(dst + i * 4, b, g, r, a);
        }
        break;
    }
    case HTA_FMT_A4R4G4B4: {
        uint32_t n = w * h; if (src_len < n * 2u) n = src_len / 2u;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t p = (uint16_t)(src[i * 2] | (src[i * 2 + 1] << 8));
            uint8_t a = (uint8_t)(((p >> 12) & 15) * 17);
            uint8_t r = (uint8_t)(((p >> 8)  & 15) * 17);
            uint8_t g = (uint8_t)(((p >> 4)  & 15) * 17);
            uint8_t b = (uint8_t)((p         & 15) * 17);
            put_bgra(dst + i * 4, b, g, r, a);
        }
        break;
    }
    case HTA_FMT_X8R8G8B8:
    case HTA_FMT_A8R8G8B8: {
        uint32_t n = w * h; if (src_len < n * 4u) n = src_len / 4u;
        int force_a = (format == HTA_FMT_X8R8G8B8);
        for (uint32_t i = 0; i < n; i++) {
            /* DirectX A8R8G8B8 in memory: B, G, R, A */
            uint8_t b = src[i * 4 + 0], g = src[i * 4 + 1];
            uint8_t r = src[i * 4 + 2], a = src[i * 4 + 3];
            put_bgra(dst + i * 4, b, g, r, force_a ? 255 : a);
        }
        break;
    }
    default:
        free(dst);
        fail(err, errlen, "unsupported bitmap format %u", format);
        return false;
    }

    out->width = w;
    out->height = h;
    out->rgba = dst;
    return true;
}

bool hta_bitmap_decode(const hta_cache *c, const hta_resource_map *bitmaps,
                       uint32_t tag_id, uint32_t index,
                       hta_bitmap *out, char *err, size_t errlen)
{
    if (!c || !out) { fail(err, errlen, "bad arguments"); return false; }
    int32_t ti = hta_cache_find_tag_by_id(c, tag_id);
    if (ti < 0) { fail(err, errlen, "bitmap tag 0x%08X not found", tag_id); return false; }
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.primary_class != HTA_TAG_BITM) {
        fail(err, errlen, "tag 0x%08X is not 'bitm'", tag_id); return false;
    }
    uint32_t toff;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &toff)) {
        fail(err, errlen, "bitmap tag data pointer out of range"); return false;
    }
    uint32_t count = 0, ptr = 0;
    if (!hta_read_reflexive(c, toff + HTA_BITM_DATA_REFLEXIVE, &count, &ptr) || count == 0) {
        fail(err, errlen, "bitmap has no bitmap_data"); return false;
    }
    if (index >= count) {
        fail(err, errlen, "bitmap index %u >= %u", index, count); return false;
    }
    uint32_t arr;
    if (!hta_cache_ptr_to_offset(c, ptr, &arr)) {
        fail(err, errlen, "bitmap_data pointer out of range"); return false;
    }
    uint32_t e = arr + index * HTA_BITM_DATA_SIZE;
    uint16_t w = 0, h = 0, fmt = 0, flags = 0;
    uint32_t poff = 0, psz = 0;
    if (!hta_rd_u16(c, e + HTA_BITM_DATA_WIDTH, &w) ||
        !hta_rd_u16(c, e + HTA_BITM_DATA_HEIGHT, &h) ||
        !hta_rd_u16(c, e + HTA_BITM_DATA_FORMAT, &fmt) ||
        !hta_rd_u16(c, e + HTA_BITM_DATA_FLAGS, &flags) ||
        !hta_rd_u32(c, e + HTA_BITM_DATA_PIXEL_OFF, &poff) ||
        !hta_rd_u32(c, e + HTA_BITM_DATA_PIXEL_SIZE, &psz)) {
        fail(err, errlen, "bitmap_data unreadable"); return false;
    }
    const uint8_t *src = NULL;
    uint32_t src_len = psz;
    if (flags & HTA_BITM_FLAG_EXTERNAL) {
        if (!bitmaps || !bitmaps->data) {
            fail(err, errlen, "bitmap is in bitmaps.map but no resource map was supplied");
            return false;
        }
        if ((uint64_t)poff + psz > (uint64_t)bitmaps->size) {
            fail(err, errlen, "pixel data [0x%X +%u] outside bitmaps.map (%zu)",
                 poff, psz, bitmaps->size);
            return false;
        }
        src = bitmaps->data + poff;
    } else {
        if ((uint64_t)poff + psz > (uint64_t)c->size) {
            fail(err, errlen, "pixel data [0x%X +%u] outside cache (%zu)",
                 poff, psz, c->size);
            return false;
        }
        src = c->data + poff;
    }
    return hta_bitmap_decode_pixels(fmt, w, h, src, src_len, out, err, errlen);
}

uint32_t hta_shader_detail_bitmap(const hta_cache *c, uint32_t shader_tag_id,
                                  float *out_scale)
{
    if (out_scale) *out_scale = 0.0f;
    if (!c || !shader_tag_id || shader_tag_id == 0xFFFFFFFFu) return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, shader_tag_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return 0;
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;

    /* The environment shader and the model shader both layer a detail map
     * over the base; they just keep it in different places. 34 of Blood
     * Gulch's 117 model shaders have one, the cyborg's first-person hands
     * among them -- which is to say every weapon you hold. */
    uint32_t map_off, scale_off;
    if (t.primary_class == HTA_TAG_SENV) {
        map_off = HTA_SENV_PRIMARY_DETAIL;
        scale_off = HTA_SENV_PRIMARY_DETAIL_SCALE;
    } else if (t.primary_class == HTA_TAG_SOSO) {
        map_off = HTA_SOSO_DETAIL;
        scale_off = HTA_SOSO_DETAIL_SCALE;
    } else {
        return 0;
    }

    uint32_t id = 0;
    float scale = 0.0f;
    if (!hta_rd_u32(c, off + map_off + 0x0C, &id)) return 0;
    if (!id || id == 0xFFFFFFFFu) return 0;
    hta_rd_f32(c, off + scale_off, &scale);
    /* A zero scale means "once across the surface", which for a detail map
     * is never what is wanted; Halo's own default is 1. */
    if (!(scale > 0.0f)) scale = 1.0f;
    if (out_scale) *out_scale = scale;
    return id;
}

uint32_t hta_shader_detail2_bitmap(const hta_cache *c, uint32_t shader_tag_id,
                                   float *out_scale)
{
    if (out_scale) *out_scale = 0.0f;
    if (!c || !shader_tag_id || shader_tag_id == 0xFFFFFFFFu) return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, shader_tag_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return 0;
    if (t.primary_class != HTA_TAG_SENV) return 0;
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;

    uint32_t id = 0;
    float scale = 0.0f;
    if (!hta_rd_u32(c, off + HTA_SENV_SECONDARY_DETAIL + 0x0C, &id)) return 0;
    if (!id || id == 0xFFFFFFFFu) return 0;
    hta_rd_f32(c, off + HTA_SENV_SECONDARY_DETAIL_SCALE, &scale);
    if (!(scale > 0.0f)) scale = 1.0f;
    if (out_scale) *out_scale = scale;
    return id;
}

uint32_t hta_shader_multipurpose(const hta_cache *c, uint32_t shader_tag_id,
                                 uint8_t *out_mask)
{
    if (out_mask) *out_mask = 0;
    if (!c || !shader_tag_id || shader_tag_id == 0xFFFFFFFFu) return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, shader_tag_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return 0;
    if (t.primary_class != HTA_TAG_SOSO) return 0;
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;

    uint16_t mask = 0;
    hta_rd_u16(c, off + HTA_SOSO_DETAIL_MASK, &mask);
    if (!mask || mask > 8u) return 0;          /* "none", or out of range */

    uint32_t id = 0;
    if (!hta_rd_u32(c, off + HTA_SOSO_MULTIPURPOSE + 0x0C, &id)) return 0;
    if (!id || id == 0xFFFFFFFFu) return 0;
    if (out_mask) *out_mask = (uint8_t)mask;
    return id;
}

uint32_t hta_shader_base_bitmap(const hta_cache *c, uint32_t shader_tag_id)
{
    if (!c || !shader_tag_id || shader_tag_id == 0xFFFFFFFFu) return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, shader_tag_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return 0;
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;

    if (t.primary_class == HTA_TAG_SENV) {
        uint32_t id = 0;
        if (hta_rd_u32(c, off + HTA_SENV_BASE_MAP + 0x0C, &id) &&
            id && id != 0xFFFFFFFFu) return id;
    }

    /* Glass: the diffuse map is the surface. Falling through to the generic
     * scan picks the REFLECTION cube map instead, because it comes first. */
    if (t.primary_class == HTA_TAG_SGLA) {
        uint32_t id = 0;
        if (hta_rd_u32(c, off + HTA_SGLA_DIFFUSE + 0x0C, &id) &&
            id && id != 0xFFFFFFFFu) return id;
        if (hta_rd_u32(c, off + HTA_SGLA_BACKGROUND_TINT + 0x0C, &id) &&
            id && id != 0xFFFFFFFFu) return id;
    }

    /* Generic: first TagDependency whose class fourcc is 'bitm' in the first
     * 512 bytes of the shader. Covers soso / schi / scex well enough. */
    for (uint32_t rel = 0x28; rel + 16 <= 512; rel += 4) {
        uint32_t cls = 0, id = 0;
        if (!hta_rd_u32(c, off + rel, &cls)) break;
        if (cls != HTA_TAG_BITM) continue;
        if (!hta_rd_u32(c, off + rel + 0x0C, &id)) break;
        if (id && id != 0xFFFFFFFFu && hta_cache_find_tag_by_id(c, id) >= 0)
            return id;
    }
    return 0;
}

uint8_t hta_shader_draw_mode(const hta_cache *c, uint32_t shader_tag_id)
{
    if (!c || !shader_tag_id || shader_tag_id == 0xFFFFFFFFu) return HTA_DRAW_OPAQUE;
    int32_t ti = hta_cache_find_tag_by_id(c, shader_tag_id);
    if (ti < 0) return HTA_DRAW_OPAQUE;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return HTA_DRAW_OPAQUE;
    if (t.primary_class == HTA_TAG_SENV) return HTA_DRAW_OPAQUE;
    char path[192] = {0};
    hta_cache_tag_path(c, &t, path, sizeof(path));
    /* Blood Gulch sky portals are chicago shaders named "... light black". */
    for (char *p = path; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    if (strstr(path, "black")) return HTA_DRAW_SKIP;
    if (strstr(path, "light") || strstr(path, "teleporter") || strstr(path, "shield"))
        return HTA_DRAW_ADD;
    /* Transparent glass is never opaque. The needler's needles are a
     * `sgla` -- "needler luminous" -- and drawn opaque they came out as
     * dark solid spikes instead of the lit crystal they are. Additive is
     * the honest stand-in: we have no refraction or cube-map reflection,
     * and what the eye reads on these is the glow. */
    if (t.primary_class == HTA_TAG_SGLA) return HTA_DRAW_ADD;

    if (t.primary_class == HTA_TAG_SCHI || t.primary_class == HTA_TAG_SCEX) {
        /* Ask the tag rather than the tag's name. Shader base is 40 bytes,
         * so a chicago shader's framebuffer blend function sits at +44.
         * The assault rifle's display quads declare ADD; drawn as ALPHA
         * their black background paints a box over the gun. */
        uint32_t base = 0;
        uint16_t blend = 0;
        if (hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base) &&
            hta_rd_u16(c, base + 44u, &blend)) {
            if (blend == 3u) return HTA_DRAW_ADD;   /* add */
        }
        return HTA_DRAW_ALPHA;
    }
    return HTA_DRAW_OPAQUE;
}

uint32_t hta_mesh_intern_bitmap(hta_bsp_mesh *mesh, const hta_cache *c,
                                const hta_resource_map *bitmaps,
                                uint32_t tag_id, uint32_t index)
{
    if (!tag_id || tag_id == 0xFFFFFFFFu || !mesh || !mesh->textures) return ~0u;
    for (uint32_t i = 0; i < mesh->texture_count; i++) {
        if (mesh->textures[i].tag_id == tag_id &&
            mesh->textures[i].index == index) return i;
    }
    if (mesh->texture_count >= MAX_TEX) return ~0u;
    hta_bitmap bm;
    char err[HTA_ERRLEN];
    if (!hta_bitmap_decode(c, bitmaps, tag_id, index, &bm, err, sizeof(err)))
        return ~0u;
    uint32_t slot = mesh->texture_count++;
    mesh->textures[slot].tag_id = tag_id;
    mesh->textures[slot].index  = index;
    mesh->textures[slot].width  = bm.width;
    mesh->textures[slot].height = bm.height;
    mesh->textures[slot].rgba   = bm.rgba;
    bm.rgba = NULL;
    return slot;
}

bool hta_bsp_load_textures(const hta_cache *c, const hta_resource_map *bitmaps,
                           hta_bsp_mesh *mesh, char *err, size_t errlen)
{
    if (!c || !mesh) { fail(err, errlen, "bad arguments"); return false; }
    free(mesh->textures);
    mesh->textures = (hta_bsp_texture *)calloc(MAX_TEX, sizeof(hta_bsp_texture));
    if (!mesh->textures) { fail(err, errlen, "oom"); return false; }
    mesh->texture_count = 0;

    for (uint32_t i = 0; i < mesh->submesh_count; i++) {
        hta_submesh *sm = &mesh->submeshes[i];
        sm->albedo_tex = ~0u;
        sm->lightmap_tex = ~0u;
        sm->detail_tex = ~0u;
        sm->detail_scale = 0.0f;
        sm->detail2_tex = ~0u;
        sm->detail2_scale = 0.0f;
        sm->draw_mode = hta_shader_draw_mode(c, sm->shader_tag_id);
        uint32_t base = hta_shader_base_bitmap(c, sm->shader_tag_id);
        if (base) sm->albedo_tex = hta_mesh_intern_bitmap(mesh, c, bitmaps, base, 0);
        float dscale = 0.0f;
        uint32_t detail = hta_shader_detail_bitmap(c, sm->shader_tag_id, &dscale);
        if (detail) {
            uint32_t dt = hta_mesh_intern_bitmap(mesh, c, bitmaps, detail, 0);
            if (dt != ~0u) { sm->detail_tex = dt; sm->detail_scale = dscale; }
        }
        float d2scale = 0.0f;
        uint32_t detail2 = hta_shader_detail2_bitmap(c, sm->shader_tag_id, &d2scale);
        if (detail2) {
            uint32_t dt = hta_mesh_intern_bitmap(mesh, c, bitmaps, detail2, 0);
            if (dt != ~0u) { sm->detail2_tex = dt; sm->detail2_scale = d2scale; }
        }
        if (mesh->lightmaps_bitmap_id && sm->lightmap_index != 0xFFFFu)
            sm->lightmap_tex = hta_mesh_intern_bitmap(mesh, c, bitmaps,
                                          mesh->lightmaps_bitmap_id,
                                          sm->lightmap_index);
    }
    (void)errlen;
    return true;
}

#define HTA_BITM_SEQUENCES    84u
#define HTA_BITM_SEQ_SIZE     64u
#define HTA_BITM_SEQ_SPRITES  52u
#define HTA_BITM_SPRITE_SIZE  32u

static bool sequences_of(const hta_cache *c, uint32_t tag_id,
                         uint32_t *out_off, uint32_t *out_count)
{
    int32_t ti = hta_cache_find_tag_by_id(c, tag_id);
    if (ti < 0) return false;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return false;
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return false;
    uint32_t n = 0, ptr = 0, off = 0;
    if (!hta_read_reflexive(c, base + HTA_BITM_SEQUENCES, &n, &ptr)) return false;
    if (!n || !hta_cache_ptr_to_offset(c, ptr, &off)) return false;
    *out_off = off;
    *out_count = n;
    return true;
}

uint32_t hta_bitmap_sequence_count(const hta_cache *c, uint32_t tag_id)
{
    uint32_t off = 0, n = 0;
    if (!c || !sequences_of(c, tag_id, &off, &n)) return 0;
    return n;
}

uint32_t hta_bitmap_sprite_count(const hta_cache *c, uint32_t tag_id, uint32_t seq)
{
    uint32_t off = 0, n = 0;
    if (!c || !sequences_of(c, tag_id, &off, &n) || seq >= n) return 0;
    uint32_t sc = 0, sp = 0;
    if (!hta_read_reflexive(c, off + seq * HTA_BITM_SEQ_SIZE + HTA_BITM_SEQ_SPRITES,
                            &sc, &sp))
        return 0;
    return sc;
}

bool hta_bitmap_sprite_at(const hta_cache *c, uint32_t tag_id, uint32_t seq,
                          hta_bitmap_sprite *out)
{
    return hta_bitmap_sprite_in(c, tag_id, seq, 0, out);
}

bool hta_bitmap_sprite_in(const hta_cache *c, uint32_t tag_id, uint32_t seq,
                          uint32_t index, hta_bitmap_sprite *out)
{
    if (!c || !out) return false;
    memset(out, 0, sizeof(*out));
    uint32_t off = 0, n = 0;
    if (!sequences_of(c, tag_id, &off, &n) || seq >= n) return false;

    uint32_t sc = 0, sp = 0, so = 0;
    if (!hta_read_reflexive(c, off + seq * HTA_BITM_SEQ_SIZE + HTA_BITM_SEQ_SPRITES,
                            &sc, &sp))
        return false;
    if (!sc || index >= sc || !hta_cache_ptr_to_offset(c, sp, &so)) return false;
    so += index * HTA_BITM_SPRITE_SIZE;

    uint16_t bi = 0;
    if (!hta_rd_u16(c, so, &bi)) return false;
    out->bitmap_index = bi;
    if (!hta_rd_f32(c, so + 8u,  &out->u0) ||
        !hta_rd_f32(c, so + 12u, &out->u1) ||
        !hta_rd_f32(c, so + 16u, &out->v0) ||
        !hta_rd_f32(c, so + 20u, &out->v1))
        return false;
    return true;
}

uint8_t hta_shader_numeric_limit(const hta_cache *c, uint32_t shader_tag_id)
{
    if (!c || !shader_tag_id || shader_tag_id == 0xFFFFFFFFu) return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, shader_tag_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return 0;
    if (t.primary_class != HTA_TAG_SCHI && t.primary_class != HTA_TAG_SCEX) return 0;
    uint32_t base = 0;
    uint8_t limit = 0;
    /* Shader base is 40 bytes; numeric counter limit is chicago's first field. */
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return 0;
    if (!hta_rd_u8(c, base + 40u, &limit)) return 0;
    return limit;
}

uint32_t hta_bitmap_frame_count(const hta_cache *c, uint32_t tag_id)
{
    if (!c || !tag_id || tag_id == 0xFFFFFFFFu) return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, tag_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return 0;
    uint32_t base = 0, n = 0, ptr = 0;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return 0;
    if (!hta_read_reflexive(c, base + HTA_BITM_DATA_REFLEXIVE, &n, &ptr)) return 0;
    return n;
}

uint32_t hta_mesh_intern_atlas(hta_bsp_mesh *mesh, const hta_cache *c,
                               const hta_resource_map *bitmaps,
                               uint32_t tag_id, uint32_t count)
{
    if (!mesh || !mesh->textures || !c || !tag_id || count == 0) return ~0u;
    /* Atlases are keyed by tag with a marker index so they are not confused
     * with frame 0 of the same bitmap. */
    const uint32_t ATLAS_INDEX = 0xA71A5u;
    for (uint32_t i = 0; i < mesh->texture_count; i++)
        if (mesh->textures[i].tag_id == tag_id &&
            mesh->textures[i].index == ATLAS_INDEX) return i;
    if (mesh->texture_count >= MAX_TEX) return ~0u;

    hta_bitmap *frames = (hta_bitmap *)calloc(count, sizeof(hta_bitmap));
    if (!frames) return ~0u;
    char err[HTA_ERRLEN];
    uint32_t fw = 0, fh = 0, got = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!hta_bitmap_decode(c, bitmaps, tag_id, i, &frames[i], err, sizeof(err)))
            break;
        if (i == 0) { fw = frames[0].width; fh = frames[0].height; }
        /* Frames of one readout are the same size; anything else is not an
         * atlas we can lay out by shifting U. */
        if (frames[i].width != fw || frames[i].height != fh) { got = i; break; }
        got = i + 1;
    }
    if (got != count || fw == 0 || fh == 0) {
        for (uint32_t i = 0; i < count; i++) hta_bitmap_free(&frames[i]);
        free(frames);
        return ~0u;
    }

    uint32_t aw = fw * count;
    uint8_t *rgba = (uint8_t *)malloc((size_t)aw * fh * 4u);
    if (!rgba) {
        for (uint32_t i = 0; i < count; i++) hta_bitmap_free(&frames[i]);
        free(frames);
        return ~0u;
    }
    for (uint32_t y = 0; y < fh; y++)
        for (uint32_t f = 0; f < count; f++)
            memcpy(rgba + ((size_t)y * aw + (size_t)f * fw) * 4u,
                   frames[f].rgba + (size_t)y * fw * 4u, (size_t)fw * 4u);
    for (uint32_t i = 0; i < count; i++) hta_bitmap_free(&frames[i]);
    free(frames);

    uint32_t slot = mesh->texture_count++;
    mesh->textures[slot].tag_id = tag_id;
    mesh->textures[slot].index  = ATLAS_INDEX;
    mesh->textures[slot].width  = aw;
    mesh->textures[slot].height = fh;
    mesh->textures[slot].rgba   = rgba;
    return slot;
}
