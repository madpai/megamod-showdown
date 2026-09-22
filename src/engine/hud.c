#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WeaponHUDInterface (380) */
#define WPHI_CROSSHAIRS    132u
/* WeaponHUDInterfaceCrosshair (104) */
#define XHAIR_SIZE         104u
#define XHAIR_TYPE           0u
#define XHAIR_BITMAP        36u   /* TagDependency; tag id at +12 */
#define XHAIR_OVERLAYS      52u
/* WeaponHUDInterfaceCrosshairOverlay (108) */
#define WPHI_NUMBERS       120u   /* TagReflexive of WeaponHUDInterfaceNumber */
#define WNUM_SIZE          160u
#define WNUM_OFFSET         36u   /* Point2DInt */
#define WNUM_COLOR          72u   /* ColorARGBInt */
#define WNUM_MAX_DIGITS    104u   /* int8 */
#define WNUM_FLAGS         105u   /* bit 0: show leading zeros */

#define XOVER_SIZE         108u   /* WeaponHUDInterfaceCrosshairOverlay */
#define XOVER_OFFSET         0u   /* Point2DInt: two int16 */
#define XOVER_COLOR         36u   /* ColorARGBInt: blue, green, red, alpha */
#define XOVER_SEQUENCE      70u
#define XHAIR_TYPE_AIM       0u
#define XHAIR_TYPE_ZOOM      1u
/* WeaponHUDInterfaceCrosshairOverlayFlags */
#define XOVER_FLAGS         72u
#define XOVER_NOT_A_SPRITE  0x02u
#define XOVER_ONLY_ZOOMED   0x04u
#define XOVER_NOT_ZOOMED    0x40u
/* WeaponHUDInterface element lists. Note these panels are laid out
 * DIFFERENTLY from the unit HUD's -- same idea, different offsets, and
 * reusing the unit HUD's numbers here reads colour out of the flash fields. */
#define WPHI_CHILD_HUD       0u   /* TagDependency; tag id at +12 */
#define WPHI_ANCHOR         60u
#define WPHI_STATICS        96u
#define WPHI_METERS        108u
/* WeaponHUDInterfaceStaticElement (180) */
#define WSTAT_SIZE         180u
#define WSTAT_OFFSET        36u
#define WSTAT_BITMAP        72u
#define WSTAT_COLOR         88u
#define WSTAT_SEQUENCE     120u
/* WeaponHUDInterfaceMeter (180) */
#define WMETER_SIZE        180u
#define WMETER_OFFSET       36u
#define WMETER_BITMAP       72u
#define WMETER_COLOR_MIN    88u
#define WMETER_COLOR_MAX    92u
#define WMETER_SEQUENCE    106u
#define WMETER_COLOR_EMPTY 100u

/* UnitHUDInterface (1388). Every panel shares a shape, so these are the
 * starts; the field offsets inside a panel are below. All five struct sizes
 * reconcile, which is the check that catches a mis-ordered field. */
#define UNHI_ANCHOR             0u
#define UNHI_SHIELD_BACKGROUND 140u
#define UNHI_SHIELD_METER      244u
#define UNHI_HEALTH_BACKGROUND 380u
#define UNHI_HEALTH_METER      484u
/* within a background panel */
#define PANEL_OFFSET             0u   /* Point2DInt */
#define PANEL_BITMAP            36u   /* TagDependency; tag id at +12 */
#define PANEL_COLOR             52u   /* ColorARGBInt */
#define PANEL_SEQUENCE          84u   /* Index */
/* within a meter panel */
#define METER_OFFSET             0u
#define METER_BITMAP            36u
#define METER_COLOR_MIN         52u
#define METER_COLOR_MAX         56u
#define METER_SEQUENCE          70u

void hta_hud_free(hta_hud *h)
{
    if (h) hta_font_digits_free(&h->digits);
    if (!h) return;
    hta_bsp_free(&h->mesh);
    memset(h, 0, sizeof(*h));
    h->fade_elem = -1;      /* zero is a valid element index; -1 is "none" */
}

/* ColorARGBInt is stored blue, green, red, alpha -- read the other way the
 * assault rifle's cyan comes out orange. An alpha of 0 in these tags means
 * opaque, not invisible; a real alpha is honoured. */
static void unpack_color(const uint8_t c[4], float out[4])
{
    out[0] = (float)c[2] / 255.0f;
    out[1] = (float)c[1] / 255.0f;
    out[2] = (float)c[0] / 255.0f;
    out[3] = c[3] ? (float)c[3] / 255.0f : 1.0f;
}

/* Interns a block of RGBA that did not come from a bitmap tag: the digit
 * atlas is built from a font, which has no `bitm` of its own. */
static uint32_t intern_rgba(hta_hud *h, const uint8_t *rgba,
                            uint32_t w, uint32_t hgt)
{
    if (!h->mesh.textures || !rgba || !w || !hgt) return ~0u;
    if (h->mesh.texture_count >= HTA_HUD_MAX_ELEMENTS) return ~0u;
    size_t bytes = (size_t)w * hgt * 4u;
    uint8_t *copy = (uint8_t *)malloc(bytes);
    if (!copy) return ~0u;
    memcpy(copy, rgba, bytes);
    uint32_t i = h->mesh.texture_count++;
    h->mesh.textures[i].rgba = copy;
    h->mesh.textures[i].width = w;
    h->mesh.textures[i].height = hgt;
    h->mesh.textures[i].tag_id = 0xF0000000u + i;   /* never a real tag id */
    return i;
}

/* Appends one quad. Returns its element index, or -1. */
static int add_elem(hta_hud *h, uint32_t tex, const hta_bitmap_sprite *sp,
                    float sheet_w, float sheet_h,
                    int16_t off_x, int16_t off_y, uint8_t anchor,
                    const float tint[4], float meter)
{
    if (h->elem_count >= HTA_HUD_MAX_ELEMENTS) return -1;
    uint32_t base_v = h->mesh.vertex_count;
    uint32_t base_i = h->mesh.index_count;
    uint32_t base_s = h->mesh.submesh_count;

    hta_vertex *nv = (hta_vertex *)realloc(h->mesh.vertices,
                                           (size_t)(base_v + 4) * sizeof(hta_vertex));
    if (!nv) return -1;
    h->mesh.vertices = nv;
    uint32_t *ni = (uint32_t *)realloc(h->mesh.indices,
                                       (size_t)(base_i + 6) * sizeof(uint32_t));
    if (!ni) return -1;
    h->mesh.indices = ni;
    hta_submesh *ns = (hta_submesh *)realloc(h->mesh.submeshes,
                                             (size_t)(base_s + 1) * sizeof(hta_submesh));
    if (!ns) return -1;
    h->mesh.submeshes = ns;

    memset(&h->mesh.vertices[base_v], 0, 4 * sizeof(hta_vertex));
    uint32_t tri[6] = { base_v + 0, base_v + 1, base_v + 2,
                        base_v + 0, base_v + 2, base_v + 3 };
    memcpy(&h->mesh.indices[base_i], tri, sizeof(tri));

    hta_submesh *sm = &h->mesh.submeshes[base_s];
    hta_submesh_init(sm);
    sm->first_index = base_i;
    sm->index_count = 6;
    sm->albedo_tex = tex;
    sm->lightmap_tex = ~0u;
    sm->lightmap_index = 0xFFFFu;
    sm->draw_mode = HTA_DRAW_ALPHA;
    memcpy(sm->tint, tint, 4 * sizeof(float));
    sm->meter = meter;
    sm->mask = 0.0f;

    h->mesh.vertex_count = base_v + 4;
    h->mesh.index_count = base_i + 6;
    h->mesh.submesh_count = base_s + 1;

    hta_hud_elem *e = &h->elem[h->elem_count];
    e->vertex = base_v;
    e->submesh = base_s;
    e->w_px = (sp->u1 - sp->u0) * sheet_w;
    e->h_px = (sp->v1 - sp->v0) * sheet_h;
    e->offset[0] = (float)off_x;
    e->offset[1] = (float)off_y;
    e->anchor = anchor;
    e->extra_scale = 1.0f;
    e->zoom_level = 0;
    e->fullscreen = false;
    e->uv[0] = sp->u0; e->uv[1] = sp->v0;
    e->uv[2] = sp->u1; e->uv[3] = sp->v1;
    return (int)h->elem_count++;
}

/* A HUD bitmap is addressed by sequence, but Halo uses two shapes for that:
 * a SPRITE sheet, where the sequence holds a rectangle of one sheet, and a
 * multi-frame bitmap, where the sequence IS the frame and covers all of it.
 * The unit HUD mixes both -- its meters are sprites and its backgrounds are
 * frames -- so a sprite-only lookup silently loses the backgrounds. */
static bool sprite_or_frame(const hta_cache *c, uint32_t bm, uint16_t seq,
                            hta_bitmap_sprite *out)
{
    if (hta_bitmap_sprite_at(c, bm, seq, out) && out->u1 > out->u0 && out->v1 > out->v0)
        return true;
    uint32_t frames = hta_bitmap_frame_count(c, bm);
    if (seq >= frames) return false;
    out->bitmap_index = seq;
    out->u0 = 0.0f; out->u1 = 1.0f;
    out->v0 = 0.0f; out->v1 = 1.0f;
    return true;
}

/* A HUD element described by explicit field offsets, because the weapon and
 * unit HUD panels are shaped differently. */
static int add_tag_elem(hta_hud *h, const hta_cache *c,
                        const hta_resource_map *bitmaps,
                        uint32_t e, uint32_t bitmap_off, uint32_t offset_off,
                        uint32_t color_off, uint32_t seq_off,
                        uint8_t anchor, float meter, float out_color[4])
{
    uint32_t bm = 0;
    if (!hta_rd_u32(c, e + bitmap_off + 12u, &bm) || !bm) return -1;
    uint16_t seq = 0;
    hta_rd_u16(c, e + seq_off, &seq);
    hta_bitmap_sprite sp;
    if (!sprite_or_frame(c, bm, seq, &sp)) return -1;
    uint32_t tex = hta_mesh_intern_bitmap(&h->mesh, c, bitmaps, bm, sp.bitmap_index);
    if (tex == ~0u) return -1;

    int16_t ax = 0, ay = 0;
    hta_rd_u16(c, e + offset_off + 0u, (uint16_t *)&ax);
    hta_rd_u16(c, e + offset_off + 2u, (uint16_t *)&ay);

    uint8_t col[4] = {255, 255, 255, 0};
    hta_rd_bytes(c, e + color_off, col, 4);
    float tint[4];
    unpack_color(col, tint);
    /* An all-zero colour means the element has none of its own: Halo uses
     * the HUD's colour and the art purely as a MASK. The assault rifle's
     * empty-pip layer is exactly that -- black art named "alphas" -- and
     * multiplying its RGB paints a black grid over the corner. */
    bool as_mask = (col[0] == 0 && col[1] == 0 && col[2] == 0);
    if (as_mask) {
        tint[0] = 40.0f/255.0f; tint[1] = 150.0f/255.0f; tint[2] = 1.0f;
        tint[3] = 1.0f;
    }
    if (out_color) memcpy(out_color, tint, 4 * sizeof(float));

    int ei = add_elem(h, tex, &sp,
                      (float)h->mesh.textures[tex].width,
                      (float)h->mesh.textures[tex].height,
                      ax, ay, anchor, tint, meter);
    if (ei >= 0 && as_mask)
        h->mesh.submeshes[h->elem[ei].submesh].mask = 1.0f;
    return ei;
}

/* One panel of a unit HUD: its bitmap, sprite, offset and colour. */
static int add_panel(hta_hud *h, const hta_cache *c, const hta_resource_map *bitmaps,
                     uint32_t base, uint32_t panel, uint8_t anchor,
                     uint32_t color_off, uint32_t seq_off, float meter,
                     float out_color[4])
{
    uint32_t bm = 0;
    if (!hta_rd_u32(c, base + panel + PANEL_BITMAP + 12u, &bm) || !bm) return -1;
    uint16_t seq = 0;
    hta_rd_u16(c, base + panel + seq_off, &seq);
    hta_bitmap_sprite sp;
    if (!sprite_or_frame(c, bm, seq, &sp)) return -1;
    uint32_t tex = hta_mesh_intern_bitmap(&h->mesh, c, bitmaps, bm, sp.bitmap_index);
    if (tex == ~0u) return -1;

    int16_t ax = 0, ay = 0;
    hta_rd_u16(c, base + panel + PANEL_OFFSET + 0u, (uint16_t *)&ax);
    hta_rd_u16(c, base + panel + PANEL_OFFSET + 2u, (uint16_t *)&ay);

    uint8_t col[4] = {255, 255, 255, 0};
    hta_rd_bytes(c, base + panel + color_off, col, 4);
    float tint[4];
    unpack_color(col, tint);
    if (out_color) memcpy(out_color, tint, 4 * sizeof(float));

    return add_elem(h, tex, &sp,
                    (float)h->mesh.textures[tex].width,
                    (float)h->mesh.textures[tex].height,
                    ax, ay, anchor, tint, meter);
}

static void load_crosshair(hta_hud *h, const hta_cache *c,
                           const hta_resource_map *bitmaps,
                           const hta_weapon_def *weap)
{
    /* The weapon names its own HUD interface. Matching tag paths instead
     * looks right until the rocket launcher, whose wphi is
     * "rocket_launcher", and the flamethrower's "flame thrower". */
    uint32_t wphi = weap->hud_interface_id;
    if (!wphi) return;

    int32_t ti = hta_cache_find_tag_by_id(c, wphi);
    hta_tag_entry wt;
    uint32_t base = 0;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &wt) ||
        !hta_cache_ptr_to_offset(c, wt.tag_data_ptr, &base)) return;

    uint32_t xc = 0, xp = 0, xo = 0;
    if (!hta_read_reflexive(c, base + WPHI_CROSSHAIRS, &xc, &xp) || !xc) return;
    if (!hta_cache_ptr_to_offset(c, xp, &xo)) return;

    for (uint32_t k = 0; k < xc && !h->have_cross; k++) {
        uint32_t e = xo + k * XHAIR_SIZE;
        uint16_t type = 0;
        uint32_t bm = 0;
        hta_rd_u16(c, e + XHAIR_TYPE, &type);
        /* Only the plain aiming reticle. The rest are zoom overlays and
         * low-ammo flashes, which need state we do not track yet. */
        if (type != XHAIR_TYPE_AIM) continue;
        if (!hta_rd_u32(c, e + XHAIR_BITMAP + 12u, &bm) || !bm) continue;

        uint32_t oc = 0, op = 0, oof = 0;
        if (!hta_read_reflexive(c, e + XHAIR_OVERLAYS, &oc, &op) || !oc) continue;
        if (!hta_cache_ptr_to_offset(c, op, &oof)) continue;

        int16_t ax = 0, ay = 0;
        uint16_t seq = 0;
        uint8_t col[4] = {255, 255, 255, 0};
        hta_rd_u16(c, oof + XOVER_OFFSET + 0u, (uint16_t *)&ax);
        hta_rd_u16(c, oof + XOVER_OFFSET + 2u, (uint16_t *)&ay);
        hta_rd_u16(c, oof + XOVER_SEQUENCE, &seq);
        hta_rd_bytes(c, oof + XOVER_COLOR, col, 4);
        /* The sniper's first aim block is its plain reticle; its later ones
         * are scope ticks, which belong to load_scope. */
        uint32_t oflags = 0;
        hta_rd_u32(c, oof + XOVER_FLAGS, &oflags);
        if (oflags & XOVER_ONLY_ZOOMED) continue;

        hta_bitmap_sprite sp;
        if (!hta_bitmap_sprite_at(c, bm, seq, &sp)) continue;
        uint32_t tex = hta_mesh_intern_bitmap(&h->mesh, c, bitmaps, bm, sp.bitmap_index);
        if (tex == ~0u) continue;

        float tint[4];
        unpack_color(col, tint);
        int ei = add_elem(h, tex, &sp,
                          (float)h->mesh.textures[tex].width,
                          (float)h->mesh.textures[tex].height,
                          ax, ay, HTA_HUD_ANCHOR_CENTER, tint, -1.0f);
        if (ei < 0) continue;
        h->cross_elem = (uint32_t)ei;
        h->cross_px = h->elem[ei].w_px > h->elem[ei].h_px
                    ? h->elem[ei].w_px : h->elem[ei].h_px;
        h->have_cross = true;
    }
}

/* The scope: everything in the weapon's HUD tag flagged "show only when
 * zoomed".
 *
 * The sniper carries two `zoom overlay` blocks and two only-zoomed `aim`
 * blocks, one pair per zoom level, and the tag has no field naming which
 * level a block belongs to -- so the Nth zoom-only block of a given type is
 * taken as level N. That is the one assumption here; everything else is the
 * tag's.
 *
 * A `zoom overlay` is the magnification label. Its sequence is not one
 * sprite but TWO side by side in the same 64x64 sheet -- "2x" at u 0..0.391
 * and "8x" at 0.391..0.797 -- so the zoom level picks the SPRITE, not the
 * sequence. Drawing sprite 0 at both levels put "2x" on the screen while
 * scoped to eight.
 *
 * An overlay flagged "not a sprite" addresses a whole bitmap FRAME by its
 * sequence index instead; the scope's reticle ticks are all of those.
 */
static void load_scope(hta_hud *h, const hta_cache *c,
                       const hta_resource_map *bitmaps,
                       const hta_weapon_def *weap)
{
    uint32_t wphi = weap->hud_interface_id;
    if (!wphi) return;
    int32_t ti = hta_cache_find_tag_by_id(c, wphi);
    hta_tag_entry wt;
    uint32_t base = 0;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &wt) ||
        !hta_cache_ptr_to_offset(c, wt.tag_data_ptr, &base)) return;

    uint32_t xc = 0, xp = 0, xo = 0;
    if (!hta_read_reflexive(c, base + WPHI_CROSSHAIRS, &xc, &xp) || !xc) return;
    if (!hta_cache_ptr_to_offset(c, xp, &xo)) return;

    int seen_zoom = 0, seen_aim = 0;
    for (uint32_t k = 0; k < xc; k++) {
        uint32_t e = xo + k * XHAIR_SIZE;
        uint16_t type = 0;
        uint32_t bm = 0;
        hta_rd_u16(c, e + XHAIR_TYPE, &type);
        if (type != XHAIR_TYPE_AIM && type != XHAIR_TYPE_ZOOM) continue;
        if (!hta_rd_u32(c, e + XHAIR_BITMAP + 12u, &bm) || !bm) continue;

        uint32_t oc = 0, op = 0, oof = 0;
        if (!hta_read_reflexive(c, e + XHAIR_OVERLAYS, &oc, &op) || !oc) continue;
        if (!hta_cache_ptr_to_offset(c, op, &oof)) continue;

        /* Which level this block belongs to, decided once for the block so
         * every overlay in it shows and hides together. */
        int level = 0;
        for (uint32_t q = 0; q < oc && !level; q++) {
            uint32_t f = 0;
            hta_rd_u32(c, oof + q * XOVER_SIZE + XOVER_FLAGS, &f);
            if (f & XOVER_ONLY_ZOOMED)
                level = (type == XHAIR_TYPE_ZOOM) ? ++seen_zoom : ++seen_aim;
        }
        if (!level) continue;

        for (uint32_t q = 0; q < oc; q++) {
            uint32_t ov = oof + q * XOVER_SIZE;
            uint32_t f = 0;
            hta_rd_u32(c, ov + XOVER_FLAGS, &f);
            if (!(f & XOVER_ONLY_ZOOMED)) continue;

            int16_t ax = 0, ay = 0;
            uint16_t seq = 0;
            uint8_t col[4] = {255, 255, 255, 0};
            hta_rd_u16(c, ov + XOVER_OFFSET + 0u, (uint16_t *)&ax);
            hta_rd_u16(c, ov + XOVER_OFFSET + 2u, (uint16_t *)&ay);
            hta_rd_u16(c, ov + XOVER_SEQUENCE, &seq);
            hta_rd_bytes(c, ov + XOVER_COLOR, col, 4);

            hta_bitmap_sprite sp;
            if (f & XOVER_NOT_A_SPRITE) {
                /* A whole frame, addressed by the sequence index. */
                uint32_t frames = hta_bitmap_frame_count(c, bm);
                if (seq >= frames) continue;
                sp.bitmap_index = seq;
                sp.u0 = 0.0f; sp.u1 = 1.0f;
                sp.v0 = 0.0f; sp.v1 = 1.0f;
            } else if (type == XHAIR_TYPE_ZOOM) {
                /* The magnification label: the level chooses the sprite. */
                uint32_t nsp = hta_bitmap_sprite_count(c, bm, seq);
                uint32_t want = (uint32_t)(level - 1);
                if (want >= nsp) want = nsp ? nsp - 1u : 0u;
                if (!hta_bitmap_sprite_in(c, bm, seq, want, &sp)) continue;
            } else if (!sprite_or_frame(c, bm, seq, &sp)) {
                continue;
            }

            uint32_t tex = hta_mesh_intern_bitmap(&h->mesh, c, bitmaps, bm,
                                                  sp.bitmap_index);
            if (tex == ~0u) continue;
            float tint[4];
            unpack_color(col, tint);

            int ei = add_elem(h, tex, &sp,
                              (float)h->mesh.textures[tex].width,
                              (float)h->mesh.textures[tex].height,
                              ax, ay, HTA_HUD_ANCHOR_CENTER, tint, -1.0f);
            if (ei < 0) return;             /* out of element slots */
            h->elem[ei].zoom_level = (int8_t)level;
        }
    }
}

/* The rounds counter. The weapon's tag says where it goes and how many
 * digits it gets; the glyphs come from the HUD font. */
static void load_numbers(hta_hud *h, const hta_cache *c, uint32_t base,
                         uint8_t anchor)
{
    if (h->number_count) return;              /* the first block wins */
    uint32_t n = 0, p = 0, off = 0;
    if (!hta_read_reflexive(c, base + WPHI_NUMBERS, &n, &p) || !n) return;
    if (!hta_cache_ptr_to_offset(c, p, &off)) return;
    if (!h->digits.loaded) return;

    uint32_t tex = intern_rgba(h, h->digits.rgba,
                               h->digits.atlas_w, h->digits.atlas_h);
    if (tex == ~0u) return;

    int16_t ax = 0, ay = 0;
    uint8_t col[4] = {255, 255, 255, 0};
    int8_t  maxd = 0;
    uint8_t flags = 0;
    hta_rd_u16(c, off + WNUM_OFFSET + 0u, (uint16_t *)&ax);
    hta_rd_u16(c, off + WNUM_OFFSET + 2u, (uint16_t *)&ay);
    hta_rd_bytes(c, off + WNUM_COLOR, col, 4);
    hta_rd_u8(c, off + WNUM_MAX_DIGITS, (uint8_t *)&maxd);
    hta_rd_u8(c, off + WNUM_FLAGS, &flags);
    if (maxd <= 0) maxd = 3;
    if (maxd > 8) maxd = 8;

    float tint[4];
    unpack_color(col, tint);

    /* One cell per digit, as wide as the widest glyph, so the number does
     * not shuffle sideways as it counts down. */
    float cell = 0.0f;
    for (int d = 0; d < 10; d++)
        if ((float)h->digits.digit[d].advance > cell)
            cell = (float)h->digits.digit[d].advance;
    h->number_cell = cell;
    h->number_base[0] = (float)ax;
    h->number_base[1] = (float)ay;
    h->number_leading_zeros = (flags & 1u) != 0;

    for (int i = 0; i < maxd; i++) {
        hta_bitmap_sprite sp;
        sp.bitmap_index = 0;
        sp.u0 = h->digits.digit[0].u0; sp.u1 = h->digits.digit[0].u1;
        sp.v0 = h->digits.digit[0].v0; sp.v1 = h->digits.digit[0].v1;
        int ei = add_elem(h, tex, &sp,
                          (float)h->digits.atlas_w, (float)h->digits.atlas_h,
                          (int16_t)(ax + i * (int)cell), ay,
                          anchor, tint, -1.0f);
        if (ei < 0) break;
        h->elem[ei].extra_scale = HTA_HUD_WEAPON_SCALE;
        h->number_elem[h->number_count++] = ei;
    }
}

/* UnitHUDInterface sounds: reflexive at +960, 56 bytes an entry, the sound
 * at +0 and `latched to` at +16. The 56 is the definition's own; the 960 is
 * probed, because the walk to it drifts by 32. */
#define UNHI_SOUNDS        960u
#define UNHI_SOUND_SIZE     56u
#define UNHI_SOUND_LATCHED  16u

uint32_t hta_unit_hud_sound(const hta_cache *c, uint32_t latched_to,
                            bool *out_looping)
{
    if (out_looping) *out_looping = false;
    if (!c || !latched_to) return 0;

    uint32_t unhi = 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_FOURCC('u','n','h','i')) continue;
        char path[192];
        if (!hta_cache_tag_path(c, &t, path, sizeof(path))) continue;
        if (strstr(path, "cyborg_mp")) { unhi = t.tag_id; break; }
    }
    if (!unhi) return 0;

    int32_t ti = hta_cache_find_tag_by_id(c, unhi);
    hta_tag_entry ut;
    uint32_t base = 0;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &ut) ||
        !hta_cache_ptr_to_offset(c, ut.tag_data_ptr, &base))
        return 0;

    uint32_t count = 0, ptr = 0, off = 0;
    if (!hta_read_reflexive(c, base + UNHI_SOUNDS, &count, &ptr)) return 0;
    if (!count || !hta_cache_ptr_to_offset(c, ptr, &off)) return 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t e = off + i * UNHI_SOUND_SIZE;
        uint32_t latch = 0;
        if (!hta_rd_u32(c, e + UNHI_SOUND_LATCHED, &latch)) continue;
        if (!(latch & latched_to)) continue;
        uint32_t id = 0;
        if (!hta_rd_u32(c, e + 12u, &id) || !id || id == 0xFFFFFFFFu) continue;
        if (out_looping) {
            int32_t si = hta_cache_find_tag_by_id(c, id);
            hta_tag_entry st;
            if (si >= 0 && hta_cache_tag(c, (uint32_t)si, &st))
                *out_looping = (st.primary_class == HTA_FOURCC('l','s','n','d'));
        }
        return id;
    }
    return 0;
}

static void load_unit(hta_hud *h, const hta_cache *c,
                      const hta_resource_map *bitmaps)
{
    /* The multiplayer cyborg: Blood Gulch is a multiplayer map. */
    uint32_t unhi = 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_FOURCC('u','n','h','i')) continue;
        char path[192];
        if (!hta_cache_tag_path(c, &t, path, sizeof(path))) continue;
        if (strstr(path, "cyborg_mp")) { unhi = t.tag_id; break; }
    }
    if (!unhi) return;

    int32_t ti = hta_cache_find_tag_by_id(c, unhi);
    hta_tag_entry ut;
    uint32_t base = 0;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &ut) ||
        !hta_cache_ptr_to_offset(c, ut.tag_data_ptr, &base)) return;

    uint16_t anchor16 = 0;
    hta_rd_u16(c, base + UNHI_ANCHOR, &anchor16);
    uint8_t anchor = (uint8_t)anchor16;

    /* Background first so the meters draw over it. */
    add_panel(h, c, bitmaps, base, UNHI_SHIELD_BACKGROUND, anchor,
              PANEL_COLOR, PANEL_SEQUENCE, -1.0f, NULL);
    add_panel(h, c, bitmaps, base, UNHI_HEALTH_BACKGROUND, anchor,
              PANEL_COLOR, PANEL_SEQUENCE, -1.0f, NULL);

    float cmin[4], cmax[4];
    uint8_t raw[4];
    h->shield_meter = add_panel(h, c, bitmaps, base, UNHI_SHIELD_METER, anchor,
                                METER_COLOR_MAX, METER_SEQUENCE, 1.0f, cmax);
    if (h->shield_meter >= 0) {
        hta_rd_bytes(c, base + UNHI_SHIELD_METER + METER_COLOR_MIN, raw, 4);
        unpack_color(raw, cmin);
        memcpy(h->shield_min, cmin, 3 * sizeof(float));
        memcpy(h->shield_max, cmax, 3 * sizeof(float));
    }
    h->health_meter = add_panel(h, c, bitmaps, base, UNHI_HEALTH_METER, anchor,
                                METER_COLOR_MAX, METER_SEQUENCE, 1.0f, cmax);
    if (h->health_meter >= 0) {
        hta_rd_bytes(c, base + UNHI_HEALTH_METER + METER_COLOR_MIN, raw, 4);
        unpack_color(raw, cmin);
        memcpy(h->health_min, cmin, 3 * sizeof(float));
        memcpy(h->health_max, cmax, 3 * sizeof(float));
    }
    h->have_unit = (h->shield_meter >= 0 || h->health_meter >= 0);
}

/* The weapon's ammo block, and whatever its `child hud` chain adds. Halo
 * draws the assault rifle's magazine as a grid of pips -- a meter like the
 * shield -- sitting on a plate that the child HUD supplies. */
static void load_weapon_hud(hta_hud *h, const hta_cache *c,
                            const hta_resource_map *bitmaps, uint32_t wphi,
                            int depth)
{
    if (!wphi || depth > 3) return;
    int32_t ti = hta_cache_find_tag_by_id(c, wphi);
    hta_tag_entry t;
    uint32_t base = 0;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return;

    uint16_t anchor16 = 0;
    hta_rd_u16(c, base + WPHI_ANCHOR, &anchor16);
    uint8_t anchor = (uint8_t)anchor16;

    /* The plate and outline the child HUD draws sit UNDER this weapon's own
     * pips, so take the child first. */
    uint32_t child = 0;
    if (hta_rd_u32(c, base + WPHI_CHILD_HUD + 12u, &child) && child &&
        child != 0xFFFFFFFFu)
        load_weapon_hud(h, c, bitmaps, child, depth + 1);

    uint32_t n = 0, p = 0, off = 0;
    if (hta_read_reflexive(c, base + WPHI_STATICS, &n, &p) && n &&
        hta_cache_ptr_to_offset(c, p, &off)) {
        for (uint32_t k = 0; k < n; k++) {
            int ei = add_tag_elem(h, c, bitmaps, off + k * WSTAT_SIZE,
                                  WSTAT_BITMAP, WSTAT_OFFSET, WSTAT_COLOR,
                                  WSTAT_SEQUENCE, anchor, -1.0f, NULL);
            if (ei >= 0) h->elem[ei].extra_scale = HTA_HUD_WEAPON_SCALE;
        }
    }

    load_numbers(h, c, base, anchor);

    if (hta_read_reflexive(c, base + WPHI_METERS, &n, &p) && n &&
        hta_cache_ptr_to_offset(c, p, &off)) {
        for (uint32_t k = 0; k < n; k++) {
            float cmax[4];
            int ei = add_tag_elem(h, c, bitmaps, off + k * WMETER_SIZE,
                                  WMETER_BITMAP, WMETER_OFFSET,
                                  WMETER_COLOR_MAX, WMETER_SEQUENCE,
                                  anchor, 1.0f, cmax);
            if (ei >= 0) h->elem[ei].extra_scale = HTA_HUD_WEAPON_SCALE;
            if (ei < 0 || h->ammo_meter >= 0) continue;
            uint8_t raw[4];
            float cmin[4];
            hta_rd_bytes(c, off + k * WMETER_SIZE + WMETER_COLOR_MIN, raw, 4);
            unpack_color(raw, cmin);
            h->ammo_meter = ei;
            memcpy(h->ammo_min, cmin, 3 * sizeof(float));
            memcpy(h->ammo_max, cmax, 3 * sizeof(float));
            /* The unfired pips. Halo draws them rather than dropping them,
             * and the assault rifle's minimum and maximum colours are the
             * SAME blue -- so without this a full magazine and an empty one
             * look identical apart from which pips vanish. */
            uint8_t eraw[4];
            hta_rd_bytes(c, off + k * WMETER_SIZE + WMETER_COLOR_EMPTY, eraw, 4);
            float ecol[4];
            unpack_color(eraw, ecol);
            hta_submesh *esm = &h->mesh.submeshes[h->elem[ei].submesh];
            esm->empty[0] = ecol[0]; esm->empty[1] = ecol[1];
            esm->empty[2] = ecol[2]; esm->empty[3] = 1.0f;
            h->have_ammo = true;
        }
    }
}

/* A black sheet for dying behind.
 *
 * Added LAST, because submeshes draw in order and this one goes over
 * everything. It is one white texel drawn in mask mode, so the shader takes
 * the art's alpha (1) times the tint's -- the tint carries both the colour
 * and how far the fade has gone. */
static void load_fade(hta_hud *h)
{
    h->fade_elem = -1;
    static const uint8_t white[4] = { 255u, 255u, 255u, 255u };
    uint32_t tex = intern_rgba(h, white, 1u, 1u);
    if (tex == ~0u) return;
    hta_bitmap_sprite sp;
    memset(&sp, 0, sizeof(sp));
    sp.u0 = 0.0f; sp.v0 = 0.0f; sp.u1 = 1.0f; sp.v1 = 1.0f;
    const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   /* invisible until asked */
    int e = add_elem(h, tex, &sp, 1.0f, 1.0f, 0, 0, HTA_HUD_ANCHOR_CENTER,
                     black, -1.0f);
    if (e < 0) return;
    h->elem[e].fullscreen = true;
    h->mesh.submeshes[h->elem[e].submesh].mask = 1.0f;   /* the art is a mask */
    h->fade_elem = e;
}

/* globals -> interface bitmaps (304 each): sweep at +112, blip at +192.
 * hud_globals: motion sensor scale at +728. */
#define MATG_INTERFACE_BITMAPS 320u
#define IFB_SWEEP  112u
#define IFB_BLIP   192u
#define IFB_HUD_GLOBALS 96u
#define HUDG_SENSOR_SCALE 728u
#define HUDG_MP_SCALE     444u
#define UNHI_SENSOR_BACKGROUND 620u
#define UNHI_SENSOR_FOREGROUND 724u
#define UNHI_SENSOR_CENTER     860u

static int add_whole(hta_hud *h, const hta_cache *c, const hta_resource_map *bitmaps,
                     uint32_t bm, const float tint[4])
{
    hta_bitmap_sprite sp;
    if (!bm || !sprite_or_frame(c, bm, 0, &sp)) return -1;
    uint32_t tex = hta_mesh_intern_bitmap(&h->mesh, c, bitmaps, bm, sp.bitmap_index);
    if (tex == ~0u) return -1;
    return add_elem(h, tex, &sp, (float)h->mesh.textures[tex].width,
                    (float)h->mesh.textures[tex].height, 0, 0,
                    HTA_HUD_ANCHOR_TOP_LEFT, tint, -1.0f);
}

static void load_sensor(hta_hud *h, const hta_cache *c, const hta_resource_map *bitmaps)
{
    h->have_sensor = false;
    for (int i = 0; i < 3; i++) h->sensor_elem[i] = -1;
    for (int i = 0; i < HTA_HUD_MAX_BLIPS; i++) h->blip_elem[i] = -1;
    uint32_t unhi = 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        char path[192];
        if (!hta_cache_tag(c, i, &t) || t.indexed ||
            t.primary_class != HTA_FOURCC('u','n','h','i')) continue;
        if (hta_cache_tag_path(c, &t, path, sizeof(path)) && strstr(path, "cyborg_mp")) {
            unhi = t.tag_id; break;
        }
    }
    int32_t ti = hta_cache_find_tag_by_id(c, unhi);
    hta_tag_entry ut;
    uint32_t base = 0;
    if (!unhi || ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &ut) ||
        !hta_cache_ptr_to_offset(c, ut.tag_data_ptr, &base)) return;
    uint32_t sweep = 0, blip = 0;
    float radius = 32.0f;
    int32_t gi = hta_cache_find_tag_by_class(c, HTA_TAG_MATG);
    hta_tag_entry gt;
    uint32_t gb, n = 0, ptr = 0, arr = 0;
    if (gi >= 0 && hta_cache_tag(c, (uint32_t)gi, &gt) &&
        hta_cache_ptr_to_offset(c, gt.tag_data_ptr, &gb) &&
        hta_read_reflexive(c, gb + MATG_INTERFACE_BITMAPS, &n, &ptr) && n &&
        hta_cache_ptr_to_offset(c, ptr, &arr)) {
        hta_rd_u32(c, arr + IFB_SWEEP + 12u, &sweep);
        hta_rd_u32(c, arr + IFB_BLIP + 12u, &blip);
        uint32_t hudg = 0, ho;
        hta_rd_u32(c, arr + IFB_HUD_GLOBALS + 12u, &hudg);
        int32_t hi = hta_cache_find_tag_by_id(c, hudg);
        hta_tag_entry ht;
        float sc = 0.0f;
        if (hi >= 0 && hta_cache_tag(c, (uint32_t)hi, &ht) &&
            hta_cache_ptr_to_offset(c, ht.tag_data_ptr, &ho) &&
            hta_rd_f32(c, ho + HUDG_SENSOR_SCALE, &sc) && sc > 1.0f && sc < 200.0f) radius = sc;
        float mp = 0.0f;
        h->sensor_scale = 1.0f;
        if (hi >= 0 && hta_rd_f32(c, ho + HUDG_MP_SCALE, &mp) && mp > 0.2f && mp <= 1.0f)
            h->sensor_scale = mp;
    }
    if (sweep == 0xFFFFFFFFu) sweep = 0;
    if (blip == 0xFFFFFFFFu || !blip) return;
    /* Disc, sweep ring, then the view cone and its range label on top. */
    h->sensor_elem[0] = add_panel(h, c, bitmaps, base, UNHI_SENSOR_BACKGROUND,
                                  HTA_HUD_ANCHOR_TOP_LEFT, PANEL_COLOR, PANEL_SEQUENCE, -1.0f, NULL);
    /* The sweep ring is opaque black round its edge: drawn over the disc
     * it shows as a grey square. It animates the sweep in Halo; the disc
     * and cone carry the look without it. */
    (void)sweep;
    h->sensor_elem[1] = -1;
    h->sensor_elem[2] = add_panel(h, c, bitmaps, base, UNHI_SENSOR_FOREGROUND,
                                  HTA_HUD_ANCHOR_TOP_LEFT, PANEL_COLOR, PANEL_SEQUENCE, -1.0f, NULL);
    if (h->sensor_elem[0] < 0) return;
    int16_t cx = 32, cy = 32;
    hta_rd_u16(c, base + UNHI_SENSOR_CENTER, (uint16_t *)&cx);
    hta_rd_u16(c, base + UNHI_SENSOR_CENTER + 2u, (uint16_t *)&cy);
    h->sensor_center[0] = cx;
    h->sensor_center[1] = cy;
    h->sensor_radius = radius;
    h->sensor_origin[0] = HTA_HUD_SENSOR_X;
    h->sensor_origin[1] = HTA_HUD_SENSOR_Y;
    /* Everything shares the disc's corner in the bottom-left; the sweep
     * and cone are centred on it. Offsets are canvas px from the corner. */
    float es = h->sensor_scale > 0.0f ? h->sensor_scale : 1.0f;
    const hta_hud_elem *disc = &h->elem[h->sensor_elem[0]];
    float dw = disc->w_px, dh = disc->h_px;
    for (int i = 0; i < 3; i++) {
        if (h->sensor_elem[i] < 0) continue;
        hta_hud_elem *e = &h->elem[h->sensor_elem[i]];
        e->anchor = HTA_HUD_ANCHOR_BOTTOM_LEFT;
        e->extra_scale = es;
        e->offset[0] = HTA_HUD_SENSOR_X + (dw - e->w_px) * 0.5f * es;
        e->offset[1] = HTA_HUD_SENSOR_Y + (dh - e->h_px) * 0.5f * es;
    }
    h->sensor_center[0] = dw * 0.5f * es;
    h->sensor_center[1] = dh * 0.5f * es;
    h->sensor_radius *= es;
    const float red[4] = { 1.0f, 0.25f, 0.15f, 1.0f };
    for (int i = 0; i < HTA_HUD_MAX_BLIPS; i++) {
        h->blip_elem[i] = add_whole(h, c, bitmaps, blip, red);
        if (h->blip_elem[i] < 0) break;
        h->elem[h->blip_elem[i]].anchor = HTA_HUD_ANCHOR_BOTTOM_LEFT;
        if (i == 0) h->blip_px = h->elem[h->blip_elem[0]].w_px;
        h->elem[h->blip_elem[i]].w_px = 0.0f;
        h->elem[h->blip_elem[i]].h_px = 0.0f;
    }
    h->have_sensor = true;
}

void hta_hud_set_blips(hta_hud *h, const hta_hud_blip *b, uint32_t n)
{
    if (!h || !h->have_sensor) return;
    uint32_t used = 0;
    for (uint32_t i = 0; i < n && used < HTA_HUD_MAX_BLIPS; i++) {
        float d = sqrtf(b[i].x * b[i].x + b[i].y * b[i].y);
        if (d > 1.0f) continue;
        int32_t ei = h->blip_elem[used];
        if (ei < 0) break;
        hta_hud_elem *e = &h->elem[ei];
        /* The art is a soft dot the size of the disc's quarter: Halo draws
         * a contact at about a sixth of that. */
        float size = h->blip_px * 0.28f * (b[i].size > 0.0f ? b[i].size : 1.0f);
        e->w_px = e->h_px = size;
        /* Bottom-left anchored: y counts up from the bottom edge. */
        e->offset[0] = h->sensor_origin[0] + h->sensor_center[0] + b[i].x * h->sensor_radius - size * 0.5f;
        e->offset[1] = h->sensor_origin[1] + h->sensor_center[1] + b[i].y * h->sensor_radius - size * 0.5f;
        float *tint = h->mesh.submeshes[e->submesh].tint;
        if (b[i].friendly) { tint[0] = 1.0f; tint[1] = 0.9f; tint[2] = 0.25f; }
        else { tint[0] = 1.0f; tint[1] = 0.25f; tint[2] = 0.15f; }
        tint[3] = 1.0f;
        used++;
    }
    for (uint32_t i = used; i < HTA_HUD_MAX_BLIPS; i++) {
        if (h->blip_elem[i] < 0) break;
        h->elem[h->blip_elem[i]].w_px = h->elem[h->blip_elem[i]].h_px = 0.0f;
    }
}

bool hta_hud_load(hta_hud *h, const hta_cache *c, const hta_resource_map *bitmaps,
                  const hta_weapon_def *weap, char *err, size_t errlen)
{
    if (!h || !c || !weap) {
        if (err) snprintf(err, errlen, "bad arguments");
        return false;
    }
    memset(h, 0, sizeof(*h));
    h->fade_elem = -1;
    h->shield_meter = -1;
    h->health_meter = -1;
    h->ammo_meter = -1;

    /* One slot per element is always enough, and interning past the end of
     * this table corrupts the heap silently -- the scope's three extra
     * bitmaps overran a 16-entry table and turned up as a double free. */
    h->mesh.textures = (hta_bsp_texture *)calloc(HTA_HUD_MAX_ELEMENTS,
                                                 sizeof(hta_bsp_texture));
    if (!h->mesh.textures) {
        if (err) snprintf(err, errlen, "oom");
        return false;
    }

    /* The HUD font first: the rounds counter is built while walking the
     * weapon HUD, and the number elements live on its CHILD hud, not on
     * the weapon's own tag. A cache with no hud_globals gets no numbers. */
    {
        char ferr[HTA_ERRLEN];
        uint32_t font = hta_hud_font(c);
        if (font) hta_font_digits_load(c, font, &h->digits, ferr, sizeof(ferr));
    }

    load_unit(h, c, bitmaps);
    load_weapon_hud(h, c, bitmaps, weap->hud_interface_id, 0);

    load_sensor(h, c, bitmaps);
    load_crosshair(h, c, bitmaps, weap);
    load_scope(h, c, bitmaps, weap);
    load_fade(h);

    if (err && errlen) {
        snprintf(err, errlen, "crosshair %s, unit hud %s, ammo %s",
                 h->have_cross ? "yes" : "no", h->have_unit ? "yes" : "no",
                 h->have_ammo ? "yes" : "no");
    }
    h->loaded = true;
    return true;
}

static void set_meter(hta_hud *h, int32_t which, float f,
                      const float cmin[3], const float cmax[3])
{
    if (!h || which < 0 || (uint32_t)which >= h->elem_count) return;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    hta_submesh *sm = &h->mesh.submeshes[h->elem[which].submesh];
    sm->meter = f;
    /* Halo lerps the bar's colour from its empty colour to its full one, so
     * a failing shield goes dark and low health goes red without any
     * threshold of ours. */
    for (int i = 0; i < 3; i++) sm->tint[i] = cmin[i] + (cmax[i] - cmin[i]) * f;
    sm->tint[3] = 1.0f;
}

void hta_hud_set_shield(hta_hud *h, float fraction)
{
    if (!h) return;
    set_meter(h, h->shield_meter, fraction, h->shield_min, h->shield_max);
}

void hta_hud_set_health(hta_hud *h, float fraction)
{
    if (!h) return;
    set_meter(h, h->health_meter, fraction, h->health_min, h->health_max);
}

void hta_hud_set_ammo(hta_hud *h, float fraction)
{
    if (!h) return;
    set_meter(h, h->ammo_meter, fraction, h->ammo_min, h->ammo_max);
}

/* Where an anchor corner sits, and which way offsets and the sprite run from
 * it. Offsets are measured INWARD: the health meter's +29 on a top-right
 * anchor moves it 29 to the left, and the shield plate's -7 lets it bleed
 * a little past the corner, which is what Halo's plate does. */
static void anchor_origin(uint8_t anchor, float w, float h,
                          float *ox, float *oy, float *gx, float *gy)
{
    switch (anchor) {
    case HTA_HUD_ANCHOR_TOP_LEFT:     *ox = 0;      *oy = 0;      *gx =  1; *gy =  1; break;
    case HTA_HUD_ANCHOR_TOP_RIGHT:    *ox = w;      *oy = 0;      *gx = -1; *gy =  1; break;
    case HTA_HUD_ANCHOR_BOTTOM_LEFT:  *ox = 0;      *oy = h;      *gx =  1; *gy = -1; break;
    case HTA_HUD_ANCHOR_BOTTOM_RIGHT: *ox = w;      *oy = h;      *gx = -1; *gy = -1; break;
    default:                          *ox = w*0.5f; *oy = h*0.5f; *gx =  0; *gy =  0; break;
    }
}

void hta_hud_set_number(hta_hud *h, int value)
{
    if (!h || !h->number_count || !h->digits.loaded) return;
    if (value < 0) value = 0;

    uint32_t n = h->number_count;
    float pen = h->number_base[0];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t place = n - 1u - i;          /* rightmost digit is place 0 */
        int div = 1;
        for (uint32_t k = 0; k < place; k++) div *= 10;
        int d = (value / div) % 10;

        /* Blank the leading zeros unless the tag asks for them. */
        bool blank = !h->number_leading_zeros && place > 0 && value < div;
        hta_hud_elem *e = &h->elem[h->number_elem[i]];
        const hta_glyph *gl = &h->digits.digit[d];
        e->uv[0] = gl->u0; e->uv[1] = gl->v0;
        e->uv[2] = gl->u1; e->uv[3] = gl->v1;
        e->w_px = blank ? 0.0f : (float)gl->w;
        e->h_px = blank ? 0.0f : (float)gl->h;
        /* Proportional, like the font: the pen advances by the glyph the
         * font says, not by a fixed cell. Leading zeros are shown on every
         * Trial weapon, so the number does not shuffle as it counts. */
        e->offset[0] = pen;
        e->offset[1] = h->number_base[1];
        /* The glyph is drawn at the weapon block's scale, so the pen has
         * to advance at that scale too or the digits sit apart. */
        pen += blank ? 0.0f : (float)gl->advance * HTA_HUD_WEAPON_SCALE;
    }
}

void hta_hud_set_zoom(hta_hud *h, int level)
{
    if (!h) return;
    h->zoom_level = level > 0 ? level : 0;
}

void hta_hud_set_fade(hta_hud *h, float alpha)
{
    if (!h || !h->loaded || h->fade_elem < 0) return;
    if ((uint32_t)h->fade_elem >= h->elem_count) return;
    if (!(alpha > 0.0f)) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    hta_submesh *sm = &h->mesh.submeshes[h->elem[h->fade_elem].submesh];
    sm->tint[0] = sm->tint[1] = sm->tint[2] = 0.0f;
    sm->tint[3] = alpha;
}

void hta_hud_layout(hta_hud *h, uint32_t screen_w, uint32_t screen_h)
{
    if (!h || !h->mesh.vertices || !screen_w || !screen_h) return;

    /* Halo scales its HUD by height, so everything keeps its size relative
     * to the vertical field of view whatever the aspect ratio. */
    float scale = (float)screen_h / HTA_HUD_CANVAS_H * HTA_HUD_PHONE_SCALE;
    float fw = (float)screen_w, fh = (float)screen_h;
    float sx = 2.0f / fw, sy = 2.0f / fh;

    for (uint32_t i = 0; i < h->elem_count; i++) {
        hta_hud_elem *e = &h->elem[i];
        /* Scope furniture belongs to one zoom level. Collapsing the quad
         * rather than skipping the draw keeps the index buffer fixed. */
        if (e->zoom_level && e->zoom_level != h->zoom_level) {
            for (uint32_t k = 0; k < 4; k++) {
                hta_vertex *v = &h->mesh.vertices[e->vertex + k];
                v->pos[0] = v->pos[1] = v->pos[2] = 0.0f;
            }
            continue;
        }
        if (e->fullscreen) {
            /* The draw loop treats a tint alpha of zero as "this element
             * has no tint" and falls back to opaque white -- so a fade of
             * nothing would paint the screen WHITE rather than skip. It is
             * collapsed here instead, which also costs no fill. */
            if (h->mesh.submeshes[e->submesh].tint[3] <= 0.0f) {
                for (uint32_t k = 0; k < 4; k++) {
                    hta_vertex *v = &h->mesh.vertices[e->vertex + k];
                    v->pos[0] = v->pos[1] = v->pos[2] = 0.0f;
                }
                continue;
            }
            /* Straight to the corners of clip space: no anchor, no canvas
             * scale, no aspect ratio. */
            const float cx4[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
            const float cy4[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
            for (uint32_t k = 0; k < 4; k++) {
                hta_vertex *v = &h->mesh.vertices[e->vertex + k];
                v->pos[0] = cx4[k];
                v->pos[1] = cy4[k];
                v->pos[2] = 0.0f;
                v->uv[0] = (cx4[k] + 1.0f) * 0.5f;
                v->uv[1] = (cy4[k] + 1.0f) * 0.5f;
            }
            continue;
        }
        float ox, oy, gx, gy;
        anchor_origin(e->anchor, fw, fh, &ox, &oy, &gx, &gy);

        /* The element's anchor-side corner sits at the anchor plus its
         * offset, and the sprite grows from there into the screen. */
        float es = e->extra_scale > 0.0f ? e->extra_scale : 1.0f;
        float w = e->w_px * scale * es, hh = e->h_px * scale * es;
        float cx = ox + e->offset[0] * scale * (gx != 0.0f ? gx : 1.0f);
        float cy = oy + e->offset[1] * scale * (gy != 0.0f ? gy : 1.0f);
        float x0, y0;
        if (gx == 0.0f) { x0 = cx - w * 0.5f; y0 = cy - hh * 0.5f; }
        else {
            x0 = (gx > 0.0f) ? cx : cx - w;
            y0 = (gy > 0.0f) ? cy : cy - hh;
        }
        float x1 = x0 + w, y1 = y0 + hh;

        float qx[4] = { x0, x1, x1, x0 };
        float qy[4] = { y0, y0, y1, y1 };
        float qu[4] = { e->uv[0], e->uv[2], e->uv[2], e->uv[0] };
        float qv[4] = { e->uv[1], e->uv[1], e->uv[3], e->uv[3] };
        for (uint32_t k = 0; k < 4; k++) {
            hta_vertex *v = &h->mesh.vertices[e->vertex + k];
            /* Pixels to clip space. Vulkan's Y and pixel Y both point down,
             * so there is no flip. */
            v->pos[0] = qx[k] * sx - 1.0f;
            v->pos[1] = qy[k] * sy - 1.0f;
            v->pos[2] = 0.0f;
            v->normal[0] = 0.0f; v->normal[1] = 0.0f; v->normal[2] = 1.0f;
            v->uv[0] = qu[k];
            v->uv[1] = qv[k];
            v->lm_uv[0] = 0.0f;
            v->lm_uv[1] = 0.0f;
        }
    }
}
