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
#define XOVER_SIZE         108u
#define XOVER_OFFSET         0u   /* Point2DInt: two int16 */
#define XOVER_W_SCALE        4u
#define XOVER_H_SCALE        8u
#define XOVER_COLOR         36u   /* ColorARGBInt: blue, green, red, alpha */
#define XOVER_SEQUENCE      70u

#define XHAIR_TYPE_AIM       0u

void hta_hud_free(hta_hud *h)
{
    if (!h) return;
    hta_bsp_free(&h->mesh);
    memset(h, 0, sizeof(*h));
}

bool hta_hud_load(hta_hud *h, const hta_cache *c, const hta_resource_map *bitmaps,
                  const hta_weapon_def *weap, char *err, size_t errlen)
{
    if (!h || !c || !weap) {
        if (err) snprintf(err, errlen, "bad arguments");
        return false;
    }
    memset(h, 0, sizeof(*h));

    h->mesh.textures = (hta_bsp_texture *)calloc(16, sizeof(hta_bsp_texture));
    if (!h->mesh.textures) {
        if (err) snprintf(err, errlen, "oom");
        return false;
    }

    /* A weapon and its HUD interface share a tag path in Halo, so the `wphi`
     * beside `weapons\...\assault rifle` is this weapon's. */
    uint32_t wphi = 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_FOURCC('w','p','h','i')) continue;
        char path[192];
        if (!hta_cache_tag_path(c, &t, path, sizeof(path))) continue;
        if (strcmp(path, weap->path) == 0) { wphi = t.tag_id; break; }
    }
    if (!wphi) {
        if (err) snprintf(err, errlen, "no wphi named '%s'", weap->path);
        h->loaded = true;
        return true;          /* no crosshair is not a failure */
    }

    int32_t ti = hta_cache_find_tag_by_id(c, wphi);
    hta_tag_entry wt;
    uint32_t base = 0;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &wt) ||
        !hta_cache_ptr_to_offset(c, wt.tag_data_ptr, &base)) {
        if (err) snprintf(err, errlen, "wphi body unreadable");
        h->loaded = true;
        return true;
    }

    uint32_t xc = 0, xp = 0, xo = 0;
    if (!hta_read_reflexive(c, base + WPHI_CROSSHAIRS, &xc, &xp) || !xc ||
        !hta_cache_ptr_to_offset(c, xp, &xo)) {
        if (err) snprintf(err, errlen, "wphi has no crosshairs");
        h->loaded = true;
        return true;
    }

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

        /* The sprite's share of the sheet, in the sheet's own pixels, is the
         * reticle's native size on Halo's 640x480 canvas. */
        float sheet_w = (float)h->mesh.textures[tex].width;
        float sheet_h = (float)h->mesh.textures[tex].height;
        float px_w = (sp.u1 - sp.u0) * sheet_w;
        float px_h = (sp.v1 - sp.v0) * sheet_h;
        h->cross_px = px_w > px_h ? px_w : px_h;
        if (h->cross_px < 1.0f) continue;

        h->cross_uv[0] = sp.u0; h->cross_uv[1] = sp.v0;
        h->cross_uv[2] = sp.u1; h->cross_uv[3] = sp.v1;
        h->cross_offset[0] = (float)ax;
        h->cross_offset[1] = (float)ay;

        /* Four vertices and one submesh. ColorARGBInt is stored
         * blue, green, red, alpha -- and every HUD element in the Trial
         * carries alpha 0, which means opaque, not invisible. */
        hta_vertex *v = (hta_vertex *)calloc(4, sizeof(hta_vertex));
        uint32_t *idx = (uint32_t *)calloc(6, sizeof(uint32_t));
        hta_submesh *sm = (hta_submesh *)calloc(1, sizeof(hta_submesh));
        if (!v || !idx || !sm) { free(v); free(idx); free(sm); break; }
        uint32_t tri[6] = { 0, 1, 2, 0, 2, 3 };
        memcpy(idx, tri, sizeof(tri));
        sm->first_index = 0;
        sm->index_count = 6;
        sm->albedo_tex = tex;
        sm->lightmap_tex = ~0u;
        sm->lightmap_index = 0xFFFFu;
        sm->draw_mode = HTA_DRAW_ALPHA;
        sm->tint[0] = (float)col[2] / 255.0f;   /* red */
        sm->tint[1] = (float)col[1] / 255.0f;   /* green */
        sm->tint[2] = (float)col[0] / 255.0f;   /* blue */
        sm->tint[3] = col[3] ? (float)col[3] / 255.0f : 1.0f;

        h->mesh.vertices = v;
        h->mesh.vertex_count = 4;
        h->mesh.indices = idx;
        h->mesh.index_count = 6;
        h->mesh.submeshes = sm;
        h->mesh.submesh_count = 1;
        h->cross_vertex = 0;
        h->have_cross = true;
    }

    h->loaded = true;
    return true;
}

void hta_hud_layout(hta_hud *h, uint32_t screen_w, uint32_t screen_h)
{
    if (!h || !h->have_cross || !h->mesh.vertices) return;
    if (!screen_w || !screen_h) return;

    /* Halo scales its HUD by height, so the reticle keeps its size relative
     * to the vertical field of view whatever the aspect ratio. */
    float scale = (float)screen_h / HTA_HUD_CANVAS_H;
    float half_px = h->cross_px * scale * 0.5f;

    float cx = (float)screen_w * 0.5f + h->cross_offset[0] * scale;
    float cy = (float)screen_h * 0.5f - h->cross_offset[1] * scale;

    float x0 = cx - half_px, x1 = cx + half_px;
    float y0 = cy - half_px, y1 = cy + half_px;

    /* Pixels to clip space. Vulkan's Y points down the screen, which is the
     * same direction as the pixel coordinates, so Y needs no flip. */
    float sx = 2.0f / (float)screen_w, sy = 2.0f / (float)screen_h;
    float qx[4] = { x0, x1, x1, x0 };
    float qy[4] = { y0, y0, y1, y1 };
    float qu[4] = { h->cross_uv[0], h->cross_uv[2], h->cross_uv[2], h->cross_uv[0] };
    float qv[4] = { h->cross_uv[1], h->cross_uv[1], h->cross_uv[3], h->cross_uv[3] };

    for (uint32_t i = 0; i < 4; i++) {
        hta_vertex *v = &h->mesh.vertices[h->cross_vertex + i];
        v->pos[0] = qx[i] * sx - 1.0f;
        v->pos[1] = qy[i] * sy - 1.0f;
        v->pos[2] = 0.0f;
        v->normal[0] = 0.0f; v->normal[1] = 0.0f; v->normal[2] = 1.0f;
        v->uv[0] = qu[i];
        v->uv[1] = qv[i];
        v->lm_uv[0] = 0.0f;
        v->lm_uv[1] = 0.0f;
    }
}
