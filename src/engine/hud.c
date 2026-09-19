#include "hud.h"

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
#define XOVER_OFFSET         0u   /* Point2DInt: two int16 */
#define XOVER_COLOR         36u   /* ColorARGBInt: blue, green, red, alpha */
#define XOVER_SEQUENCE      70u
#define XHAIR_TYPE_AIM       0u
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
    if (!h) return;
    hta_bsp_free(&h->mesh);
    memset(h, 0, sizeof(*h));
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
    memset(sm, 0, sizeof(*sm));
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
    /* A weapon and its HUD interface share a tag path in Halo. */
    uint32_t wphi = 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_FOURCC('w','p','h','i')) continue;
        char path[192];
        if (!hta_cache_tag_path(c, &t, path, sizeof(path))) continue;
        if (strcmp(path, weap->path) == 0) { wphi = t.tag_id; break; }
    }
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
            h->have_ammo = true;
        }
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
    h->shield_meter = -1;
    h->health_meter = -1;
    h->ammo_meter = -1;

    h->mesh.textures = (hta_bsp_texture *)calloc(16, sizeof(hta_bsp_texture));
    if (!h->mesh.textures) {
        if (err) snprintf(err, errlen, "oom");
        return false;
    }

    load_unit(h, c, bitmaps);

    /* A weapon and its HUD interface share a tag path in Halo. */
    uint32_t wphi = 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_FOURCC('w','p','h','i')) continue;
        char path[192];
        if (!hta_cache_tag_path(c, &t, path, sizeof(path))) continue;
        if (strcmp(path, weap->path) == 0) { wphi = t.tag_id; break; }
    }
    load_weapon_hud(h, c, bitmaps, wphi, 0);
    load_crosshair(h, c, bitmaps, weap);

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
