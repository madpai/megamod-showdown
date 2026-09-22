#include "menu.h"
#include "../asset/model.h"
#include "../engine/scene_light.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Scenario: cutscene camera points (104 bytes each, reconciles) at +1264. */
#define SCNR_CAMERA_POINTS 1264u
#define CAMPOINT_SIZE       104u

static const char *const ITEM_BITMAP[HTA_MENU_ITEMS] = {
    "ui\\shell\\main_menu\\menu_load_campaign",
    "ui\\shell\\main_menu\\menu_multiplayer",
    "ui\\shell\\main_menu\\menu_profile",
    "ui\\shell\\main_menu\\menu_settings",
    "ui\\shell\\main_menu\\menu_credits",
    "ui\\shell\\main_menu\\menu_quit",
};

/* Layout on the PC Trial's screen, measured from a 1920x1080 capture and
 * kept as fractions of a 16:9 canvas centred on the screen. Ours. */
/* The shell is drawn for 4:3 and stretched, so width and height scale
 * separately. The logo's letters span u 0.055..0.576 and v 0.371..0.664 of
 * its bitmap and sit at x 0.089..0.922, centre y 0.333 on the capture. */
#define LOGO_W       1.600f   /* bitmap width, of canvas width */
#define LOGO_H       0.505f   /* bitmap height, of screen height */
#define LOGO_X0      0.0005f  /* bitmap left edge, of canvas width */
#define LOGO_CY      0.324f   /* bitmap centre, of screen height */
/* A word fills the TOP half of its 256x64 bitmap, 80% of its width. */
#define ITEM_CY0     0.548f   /* first word's centre */
#define ITEM_STEP    0.0741f
#define ITEM_W       0.375f   /* bitmap width, of canvas width */
#define ITEM_H       0.093f   /* bitmap height, of screen height */
#define ITEM_HIT_W   0.30f
#define ITEM_HIT_H   0.070f
/* The plain frame is half-transparent in the bitmap itself, which is the
 * dim blue of an unselected word; the selected frame glows. */
static const float ITEM_TINT[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float ITEM_OFF[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };

static uint32_t find_tag(const hta_cache *c, uint32_t cls, const char *path)
{
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        char p[256];
        if (!hta_cache_tag(c, i, &t) || t.primary_class != cls) continue;
        if (hta_cache_tag_path(c, &t, p, sizeof(p)) && !strcasecmp(p, path)) return t.tag_id;
    }
    return 0;
}

static uint32_t intern(hta_bsp_mesh *m, hta_bitmap *b)
{
    if (!m->textures || m->texture_count >= 64u) { hta_bitmap_free(b); return ~0u; }
    uint32_t i = m->texture_count++;
    m->textures[i].rgba = b->rgba;          /* the mesh owns it now */
    m->textures[i].width = b->width;
    m->textures[i].height = b->height;
    m->textures[i].tag_id = 0xF1000000u + i;
    b->rgba = NULL;
    return i;
}

static int32_t add_quad(hta_bsp_mesh *m, uint32_t tex, const float tint[4])
{
    uint32_t bv = m->vertex_count, bi = m->index_count, bs = m->submesh_count;
    hta_vertex *nv = (hta_vertex *)realloc(m->vertices, (size_t)(bv + 4) * sizeof(hta_vertex));
    if (!nv) return -1;
    m->vertices = nv;
    uint32_t *ni = (uint32_t *)realloc(m->indices, (size_t)(bi + 6) * sizeof(uint32_t));
    if (!ni) return -1;
    m->indices = ni;
    hta_submesh *ns = (hta_submesh *)realloc(m->submeshes, (size_t)(bs + 1) * sizeof(hta_submesh));
    if (!ns) return -1;
    m->submeshes = ns;
    memset(&m->vertices[bv], 0, 4 * sizeof(hta_vertex));
    static const float UV[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
    for (int k = 0; k < 4; k++) { m->vertices[bv + k].uv[0] = UV[k][0]; m->vertices[bv + k].uv[1] = UV[k][1]; }
    uint32_t tri[6] = { bv, bv + 1, bv + 2, bv, bv + 2, bv + 3 };
    memcpy(&m->indices[bi], tri, sizeof(tri));
    hta_submesh *sm = &m->submeshes[bs];
    hta_submesh_init(sm);
    sm->first_index = bi;
    sm->index_count = 6;
    sm->albedo_tex = tex;
    sm->lightmap_tex = ~0u;
    sm->lightmap_index = 0xFFFFu;
    sm->draw_mode = HTA_DRAW_ALPHA;
    memcpy(sm->tint, tint, sizeof(sm->tint));
    sm->meter = -1.0f;
    sm->mask = 0.0f;
    m->vertex_count = bv + 4;
    m->index_count = bi + 6;
    m->submesh_count = bs + 1;
    return (int32_t)bv;
}

static void place(hta_bsp_mesh *m, int32_t quad, float x0, float y0, float x1, float y1,
                  uint32_t w, uint32_t h)
{
    if (quad < 0) return;
    /* Pixels to clip space; Vulkan's y runs down the screen. */
    float cx0 = x0 / (float)w * 2.0f - 1.0f, cx1 = x1 / (float)w * 2.0f - 1.0f;
    float cy0 = y0 / (float)h * 2.0f - 1.0f, cy1 = y1 / (float)h * 2.0f - 1.0f;
    hta_vertex *v = &m->vertices[quad];
    v[0].pos[0] = cx0; v[0].pos[1] = cy0;
    v[1].pos[0] = cx1; v[1].pos[1] = cy0;
    v[2].pos[0] = cx1; v[2].pos[1] = cy1;
    v[3].pos[0] = cx0; v[3].pos[1] = cy1;
}

static void hide(hta_bsp_mesh *m, int32_t quad)
{
    if (quad < 0) return;
    for (int k = 0; k < 4; k++) m->vertices[quad + k].pos[0] = m->vertices[quad + k].pos[1] = 0.0f;
}

static bool decode(const hta_cache *c, const hta_resource_map *bm, const char *path,
                   uint32_t index, hta_bitmap *out)
{
    uint32_t id = find_tag(c, HTA_FOURCC('b','i','t','m'), path);
    char err[HTA_ERRLEN];
    memset(out, 0, sizeof(*out));
    return id && hta_bitmap_decode(c, bm, id, index, out, err, sizeof(err));
}

bool hta_menu_load(hta_menu *m, const hta_cache *c, const hta_resource_map *bm,
                   char *err, size_t errlen)
{
    if (!m || !c) { if (err) snprintf(err, errlen, "bad arguments"); return false; }
    memset(m, 0, sizeof(*m));
    m->selected = HTA_MENU_MULTIPLAYER;
    m->backdrop_quad = -1;
    char e2[HTA_ERRLEN];

    /* The scene: the sky, and the ring as scenery. The ui BSP itself is
     * empty -- there is nothing else in the level. */
    hta_sky_load(&m->sky, c, bm, e2, sizeof(e2));
    m->scene.textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
    if (m->scene.textures)
        hta_scenario_add_objects(&m->scene, c, bm, e2, sizeof(e2));
    m->have_scene = m->scene.index_count > 0;
    for (uint32_t i = 0; i < m->scene.submesh_count; i++) m->scene.submeshes[i].scene_lit = true;
    /* A sun from off to the side, so the ring's edge catches the light. */
    m->light.light_dir[0] = -0.55f; m->light.light_dir[1] = 0.35f; m->light.light_dir[2] = -0.76f;
    m->light.light_color[0] = 0.85f; m->light.light_color[1] = 0.85f; m->light.light_color[2] = 0.95f;
    m->light.ambient[0] = 0.32f; m->light.ambient[1] = 0.33f; m->light.ambient[2] = 0.40f;

    /* The named camera points. */
    int32_t si = hta_cache_find_tag_by_id(c, c->scenario_tag_id);
    hta_tag_entry st;
    uint32_t sb, cnt = 0, ptr = 0, off = 0;
    m->home_cam = 0;
    if (si >= 0 && hta_cache_tag(c, (uint32_t)si, &st) &&
        hta_cache_ptr_to_offset(c, st.tag_data_ptr, &sb) &&
        hta_read_reflexive(c, sb + SCNR_CAMERA_POINTS, &cnt, &ptr) && cnt &&
        hta_cache_ptr_to_offset(c, ptr, &off)) {
        for (uint32_t i = 0; i < cnt && m->cam_count < HTA_MENU_MAX_CAMS; i++) {
            hta_menu_cam *k = &m->cams[m->cam_count];
            uint32_t b = off + i * CAMPOINT_SIZE;
            memset(k, 0, sizeof(*k));
            hta_rd_bytes(c, b + 4u, k->name, 31);
            for (int j = 0; j < 3; j++) hta_rd_f32(c, b + 40u + (uint32_t)j * 4u, &k->pos[j]);
            hta_rd_f32(c, b + 52u, &k->yaw);
            hta_rd_f32(c, b + 56u, &k->pitch);
            hta_rd_f32(c, b + 60u, &k->roll);
            hta_rd_f32(c, b + 64u, &k->fov);
            if (!strcmp(k->name, "uicam")) m->home_cam = (int32_t)m->cam_count;
            m->cam_count++;
        }
    }

    /* The overlay. */
    m->overlay.textures = (hta_bsp_texture *)calloc(64, sizeof(hta_bsp_texture));
    if (!m->overlay.textures) { if (err) snprintf(err, errlen, "out of memory"); return false; }
    hta_bitmap b;
    const float white[4] = { 1, 1, 1, 1 };
    if (!m->have_scene && decode(c, bm, "ui\\shell\\bitmaps\\background", 0, &b)) {
        /* No ring to fly past: the shell's own still of it. Its alpha is
         * not meant for blending, so make it opaque. */
        for (size_t p = 0; p < (size_t)b.width * b.height; p++) b.rgba[p * 4 + 3] = 255;
        m->backdrop_quad = add_quad(&m->overlay, intern(&m->overlay, &b), white);
    }
    if (!decode(c, bm, "ui\\shell\\main_menu\\halo_logo", 0, &b)) {
        if (err) snprintf(err, errlen, "no halo_logo in this ui.map");
        hta_menu_free(m);
        return false;
    }
    m->logo_quad = (uint32_t)add_quad(&m->overlay, intern(&m->overlay, &b), white);
    for (int i = 0; i < HTA_MENU_ITEMS; i++) {
        m->item_quad[i][0] = m->item_quad[i][1] = -1;
        for (int f = 0; f < 2; f++) {
            if (!decode(c, bm, ITEM_BITMAP[i], (uint32_t)f, &b)) continue;
            m->item_quad[i][f] = add_quad(&m->overlay, intern(&m->overlay, &b),
                                          f ? ITEM_TINT : ITEM_OFF);
        }
    }
    /* What works yet. Campaign and profiles are there, as in the Trial,
     * but dimmed until they do something. */
    m->item_enabled[HTA_MENU_MULTIPLAYER] = true;
    m->item_enabled[HTA_MENU_SETTINGS] = true;
    m->item_enabled[HTA_MENU_CREDITS] = true;
    m->item_enabled[HTA_MENU_QUIT] = true;
    for (int i = 0; i < HTA_MENU_ITEMS; i++)
        if (!m->item_enabled[i] && m->item_quad[i][0] >= 0) {
            /* One quad, one submesh, in order. */
            m->overlay.submeshes[(uint32_t)m->item_quad[i][0] / 4u].tint[3] = 0.40f;
        }

    m->snd_cursor = find_tag(c, HTA_FOURCC('s','n','d','!'), "sound\\sfx\\ui\\cursor");
    m->snd_forward = find_tag(c, HTA_FOURCC('s','n','d','!'), "sound\\sfx\\ui\\forward");
    m->snd_back = find_tag(c, HTA_FOURCC('s','n','d','!'), "sound\\sfx\\ui\\back");
    m->music = find_tag(c, HTA_FOURCC('l','s','n','d'), "sound\\music\\title1\\title1");
    m->loaded = true;
    if (err && errlen)
        snprintf(err, errlen, "ring %u verts, sky %u verts, %u camera points, %u overlay quads",
                 m->scene.vertex_count, m->sky.vertex_count, m->cam_count,
                 m->overlay.vertex_count / 4u);
    return true;
}

void hta_menu_free(hta_menu *m)
{
    if (!m) return;
    hta_bsp_free(&m->scene);
    hta_bsp_free(&m->sky);
    hta_bsp_free(&m->overlay);
    memset(m, 0, sizeof(*m));
}

void hta_menu_layout(hta_menu *m, uint32_t w, uint32_t h)
{
    if (!m || !m->loaded || !w || !h) return;
    m->width = w; m->height = h;
    float fh = (float)h, cw = fh * 16.0f / 9.0f;
    if (cw > (float)w) cw = (float)w;
    float cx = (float)w * 0.5f;
    if (m->backdrop_quad >= 0) place(&m->overlay, m->backdrop_quad, 0, 0, (float)w, fh, w, h);
    float left = cx - cw * 0.5f;
    float lx0 = left + cw * LOGO_X0, lh = fh * LOGO_H;
    place(&m->overlay, (int32_t)m->logo_quad, lx0, fh * LOGO_CY - lh * 0.5f,
          lx0 + cw * LOGO_W, fh * LOGO_CY + lh * 0.5f, w, h);
    float iw = cw * ITEM_W, ih = fh * ITEM_H;
    for (int i = 0; i < HTA_MENU_ITEMS; i++) {
        float cy = fh * (ITEM_CY0 + ITEM_STEP * (float)i);
        m->item_rect[i][0] = cx - cw * ITEM_HIT_W * 0.5f;
        m->item_rect[i][1] = cy - fh * ITEM_HIT_H * 0.5f;
        m->item_rect[i][2] = cx + cw * ITEM_HIT_W * 0.5f;
        m->item_rect[i][3] = cy + fh * ITEM_HIT_H * 0.5f;
        bool sel = i == m->selected && m->item_quad[i][1] >= 0;
        for (int f = 0; f < 2; f++) {
            if ((f == 1) == sel)
                place(&m->overlay, m->item_quad[i][f], cx - iw * 0.5f, cy - ih * 0.25f,
                      cx + iw * 0.5f, cy + ih * 0.75f, w, h);
            else
                hide(&m->overlay, m->item_quad[i][f]);
        }
    }
}

void hta_menu_update(hta_menu *m, float dt)
{
    if (m && dt > 0.0f) m->time += dt;
}

void hta_menu_camera(const hta_menu *m, hta_camera *out, float aspect)
{
    hta_camera_init(out);
    out->aspect = aspect;
    out->znear = 0.05f;
    out->zfar = 60000.0f;   /* the sky model is 7,000 to 27,000 units out */
    if (!m || !m->cam_count) return;
    const hta_menu_cam *k = &m->cams[m->home_cam];
    for (int j = 0; j < 3; j++) out->pos[j] = k->pos[j];
    /* A slow sway about the home shot, the way the Trial's shell drifts. */
    out->yaw = k->yaw + sinf(m->time * 0.07f) * 0.06f;
    out->pitch = k->pitch + sinf(m->time * 0.05f + 1.0f) * 0.03f;
    if (k->fov > 0.1f) out->fov_y = k->fov / (aspect > 0.1f ? aspect : 1.0f) * 1.2f;
}

int hta_menu_hit(const hta_menu *m, float x, float y)
{
    if (!m) return -1;
    for (int i = 0; i < HTA_MENU_ITEMS; i++) {
        if (!m->item_enabled[i]) continue;
        const float *r = m->item_rect[i];
        if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]) return i;
    }
    return -1;
}
