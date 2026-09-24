#include "menu.h"
#include "../asset/model.h"
#include "../engine/scene_light.h"
#include "../asset/strings.h"

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

/* MEGAMOD SHOWDOWN: the title art, and its words in the art's colours --
 * a warm cream, glowing fire-orange when selected. Ours. */
static struct { uint8_t *rgba; uint32_t w, h; float right; } g_art;
static const float ART_ITEM_OFF[4] = { 1.00f, 0.92f, 0.80f, 0.90f };
static const float ART_ITEM_ON[4]  = { 1.00f, 0.52f, 0.10f, 1.00f };

void hta_menu_set_art(const uint8_t *rgba, uint32_t w, uint32_t h, float art_right)
{
    free(g_art.rgba);
    memset(&g_art, 0, sizeof(g_art));
    if (!rgba || !w || !h || (uint64_t)w * h > 16u * 1024u * 1024u) return;
    g_art.rgba = (uint8_t *)malloc((size_t)w * h * 4u);
    if (!g_art.rgba) return;
    memcpy(g_art.rgba, rgba, (size_t)w * h * 4u);
    g_art.w = w; g_art.h = h;
    g_art.right = art_right > 0.1f && art_right < 0.95f ? art_right : 0.55f;
}

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
    m->cam_blend = 1.0f;
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

    m->cam_from = m->cam_to = m->home_cam;

    /* The overlay. */
    m->overlay.textures = (hta_bsp_texture *)calloc(64, sizeof(hta_bsp_texture));
    if (!m->overlay.textures) { if (err) snprintf(err, errlen, "out of memory"); return false; }
    hta_bitmap b;
    const float white[4] = { 1, 1, 1, 1 };
    if (g_art.rgba) {
        /* The game's own title art, first so everything draws over it. */
        hta_bitmap a;
        memset(&a, 0, sizeof(a));
        a.width = g_art.w; a.height = g_art.h;
        a.rgba = (uint8_t *)malloc((size_t)g_art.w * g_art.h * 4u);
        if (a.rgba) {
            memcpy(a.rgba, g_art.rgba, (size_t)g_art.w * g_art.h * 4u);
            m->backdrop_quad = add_quad(&m->overlay, intern(&m->overlay, &a), white);
            m->art = m->backdrop_quad >= 0;
            m->art_w = g_art.w; m->art_h = g_art.h;
        }
    }
    if (!m->art && !m->have_scene && decode(c, bm, "ui\\shell\\bitmaps\\background", 0, &b)) {
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
                                          m->art ? (f ? ART_ITEM_ON : ART_ITEM_OFF) : f ? ITEM_TINT : ITEM_OFF);
            /* Over the title art the words take its colours: drawn as
             * shapes in the tint, not in Halo's blue. */
            if (m->art && m->item_quad[i][f] >= 0)
                m->overlay.submeshes[(uint32_t)m->item_quad[i][f] / 4u].mask = 1.0f;
        }
    }
    /* What works yet. Profiles is there, as in the Trial, but dimmed
     * until it does something. CAMPAIGN is the solo door: a bot match
     * now, the campaign itself once b30 plays. */
    m->item_enabled[HTA_MENU_CAMPAIGN] = true;
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
    /* Title art covers the screen, cropped rather than stretched. */
    float band_l = 0.0f, band_r = (float)w;
    if (m->art && m->backdrop_quad >= 0) {
        float sw = (float)w / (float)m->art_w, sh = fh / (float)m->art_h;
        float sc = sw > sh ? sw : sh;
        float u = ((float)m->art_w - (float)w / sc) * 0.5f / (float)m->art_w;
        float v = ((float)m->art_h - fh / sc) * 0.5f / (float)m->art_h;
        hta_vertex *q = &m->overlay.vertices[m->backdrop_quad];
        q[0].uv[0] = u;        q[0].uv[1] = v;
        q[1].uv[0] = 1.0f - u; q[1].uv[1] = v;
        q[2].uv[0] = 1.0f - u; q[2].uv[1] = 1.0f - v;
        q[3].uv[0] = u;        q[3].uv[1] = 1.0f - v;
        place(&m->overlay, m->backdrop_quad, 0, 0, (float)w, fh, w, h);
        band_l = (float)w * 0.5f + (g_art.right - 0.5f) * (float)m->art_w * sc;
        if (band_l > (float)w * 0.8f) band_l = (float)w * 0.8f;
    } else if (m->backdrop_quad >= 0) place(&m->overlay, m->backdrop_quad, 0, 0, (float)w, fh, w, h);
    if (m->shell) {
        /* A submenu owns the screen; only the ring stays. */
        hide(&m->overlay, (int32_t)m->logo_quad);
        for (int i = 0; i < HTA_MENU_ITEMS; i++) {
            hide(&m->overlay, m->item_quad[i][0]);
            hide(&m->overlay, m->item_quad[i][1]);
            m->item_rect[i][0] = m->item_rect[i][2] = -1.0f;
        }
        return;
    }
    float left = cx - cw * 0.5f;
    float lx0 = left + cw * LOGO_X0, lh = fh * LOGO_H;
    float iw = cw * ITEM_W, ih = fh * ITEM_H, cy0 = ITEM_CY0;
    float hit_w = cw * ITEM_HIT_W;
    if (m->art) {
        /* The art has its own title: no HALO logo. The words stack in the
         * middle of the band right of the art, a little larger. */
        hide(&m->overlay, (int32_t)m->logo_quad);
        float band = band_r - band_l;
        cx = band_l + band * 0.5f;
        iw *= 1.15f; ih *= 1.15f;
        if (iw > band * 1.05f) { ih *= band * 1.05f / iw; iw = band * 1.05f; }
        hit_w = iw * 0.8f;
        cy0 = 0.5f - ITEM_STEP * 1.15f * (float)(HTA_MENU_ITEMS - 1) * 0.5f;
    } else {
        place(&m->overlay, (int32_t)m->logo_quad, lx0, fh * LOGO_CY - lh * 0.5f,
              lx0 + cw * LOGO_W, fh * LOGO_CY + lh * 0.5f, w, h);
    }
    float step = m->art ? ITEM_STEP * 1.15f : ITEM_STEP;
    for (int i = 0; i < HTA_MENU_ITEMS; i++) {
        float cy = fh * (cy0 + step * (float)i);
        m->item_rect[i][0] = cx - hit_w * 0.5f;
        m->item_rect[i][1] = cy - fh * ITEM_HIT_H * 0.5f;
        m->item_rect[i][2] = cx + hit_w * 0.5f;
        m->item_rect[i][3] = cy + fh * ITEM_HIT_H * 0.5f;
        bool sel = i == m->selected && m->item_quad[i][1] >= 0;
        /* Over title art the first slot says SINGLEPLAYER, drawn by the
         * platform in the rect above; the Trial's CAMPAIGN word hides. */
        bool words = !(m->art && i == HTA_MENU_CAMPAIGN);
        for (int f = 0; f < 2; f++) {
            if (words && (f == 1) == sel)
                place(&m->overlay, m->item_quad[i][f], cx - iw * 0.5f, cy - ih * 0.25f,
                      cx + iw * 0.5f, cy + ih * 0.75f, w, h);
            else
                hide(&m->overlay, m->item_quad[i][f]);
        }
    }
}

/* How long the camera takes between two points. Ours. */
#define CAM_GLIDE_TIME 1.4f

void hta_menu_update(hta_menu *m, float dt)
{
    if (!m || dt <= 0.0f) return;
    m->time += dt;
    if (m->cam_blend < 1.0f) {
        m->cam_blend += dt / CAM_GLIDE_TIME;
        if (m->cam_blend > 1.0f) m->cam_blend = 1.0f;
    }
}

void hta_menu_focus(hta_menu *m, const char *name)
{
    if (!m || !name) return;
    for (uint32_t i = 0; i < m->cam_count; i++) {
        if (strcmp(m->cams[i].name, name)) continue;
        if ((int32_t)i == m->cam_to) return;
        /* Leave from wherever the glide has got to, not where it began. */
        m->cam_from = m->cam_blend >= 0.5f ? m->cam_to : m->cam_from;
        m->cam_to = (int32_t)i;
        m->cam_blend = 0.0f;
        return;
    }
}

static float wrap_pi(float a)
{
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

void hta_menu_camera(const hta_menu *m, hta_camera *out, float aspect)
{
    hta_camera_init(out);
    out->aspect = aspect;
    out->znear = 0.05f;
    out->zfar = 60000.0f;   /* the sky model is 7,000 to 27,000 units out */
    if (!m || !m->cam_count) return;
    int32_t fi = m->cam_from, ti = m->cam_to;
    if (fi < 0 || fi >= (int32_t)m->cam_count) fi = m->home_cam;
    if (ti < 0 || ti >= (int32_t)m->cam_count) ti = m->home_cam;
    const hta_menu_cam *a = &m->cams[fi], *k = &m->cams[ti];
    float t = m->cam_blend;
    t = t * t * (3.0f - 2.0f * t);          /* ease in and out */
    for (int j = 0; j < 3; j++) out->pos[j] = a->pos[j] + (k->pos[j] - a->pos[j]) * t;
    float yaw = a->yaw + wrap_pi(k->yaw - a->yaw) * t;
    float pitch = a->pitch + (k->pitch - a->pitch) * t;
    float fov = a->fov + (k->fov - a->fov) * t;
    /* A slow sway about the shot, the way the Trial's shell drifts. */
    out->yaw = yaw + sinf(m->time * 0.07f) * 0.06f;
    out->pitch = pitch + sinf(m->time * 0.05f + 1.0f) * 0.03f;
    if (fov > 0.1f) out->fov_y = fov / (aspect > 0.1f ? aspect : 1.0f) * 1.2f;
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

/* ------------------------------------------------------------ the shell */

#define MP "ui\\shell\\main_menu\\multiplayer_type_select\\"
#define MS "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\"

static const struct { const char *path; uint32_t frame; } SHELL_ART[HTA_SHELL_ART_COUNT] = {
    { MP "header_multiplayer", 0 },
    { MP "join_game\\header_lan", 0 },
    { MP "join_game\\header_internet", 0 },
    { MP "direct_ip\\header_direct_ip", 0 },
    { MP "server_settings\\header_server_settings", 0 },
    { "ui\\shell\\main_menu\\gametype_select\\header_select_gametype", 0 },
    { MP "mp_options", 0 },
    { MP "mp_options", 1 },
    { "ui\\shell\\bitmaps\\option_bkds", 0 },
    { "ui\\shell\\bitmaps\\option_bkds", 1 },
    { "ui\\shell\\bitmaps\\text_button_background", 0 },
    { "ui\\shell\\bitmaps\\text_button_background", 1 },
    { "ui\\shell\\bitmaps\\arrow_sm_left", 0 },
    { "ui\\shell\\bitmaps\\arrow_sm_left", 1 },
    { "ui\\shell\\bitmaps\\arrow_sm_right", 0 },
    { "ui\\shell\\bitmaps\\arrow_sm_right", 1 },
};

/* The words, by position. GameActivity.java's T_* constants are these
 * indices; change both together. */
static const struct { const char *path; uint32_t index; } SHELL_TEXT[HTA_SHELL_TEXT_COUNT] = {
    /*  0 */ { MP "multiplayer_options", 0 },               /* JOIN GAME */
    /*  1 */ { MP "multiplayer_options", 1 },               /* CREATE GAME */
    /*  2 */ { MP "multiplayer_options", 2 },               /* INTERNET */
    /*  3 */ { MP "multiplayer_options", 3 },               /* LAN */
    /*  4 */ { MP "multiplayer_options", 4 },               /* DIRECT IP */
    /*  5 */ { MP "multiplayer_option_descriptions", 0 },   /* join, Internet */
    /*  6 */ { MP "multiplayer_option_descriptions", 1 },   /* join, LAN */
    /*  7 */ { MP "multiplayer_option_descriptions", 2 },   /* join, address */
    /*  8 */ { MP "multiplayer_option_descriptions", 3 },   /* create, Internet */
    /*  9 */ { MP "multiplayer_option_descriptions", 4 },   /* create, LAN */
    /* 10 */ { MP "server_settings\\server_settings_options", 0 },  /* SERVER NAME */
    /* 11 */ { MP "server_settings\\server_settings_options", 3 },  /* MAX PLAYERS */
    /* 12 */ { MP "server_settings\\server_settings_options", 4 },  /* START GAME */
    /* 13 */ { MP "server_settings\\server_settings_options", 5 },  /* SERVER IP */
    /* 14 */ { MP "server_settings\\cap_server_settings_options", 0 },
    /* 15 */ { MP "server_settings\\cap_server_settings_options", 7 },
    /* 16 */ { MP "direct_ip\\direct_ip_options", 0 },     /* SERVER ADDRESS */
    /* 17 */ { MP "direct_ip\\cap_direct_ip_options", 0 },
    /* 18 */ { "ui\\shell\\strings\\common_button_captions", 0 },   /* BACK */
    /* 19 */ { "ui\\shell\\strings\\common_button_captions", 1 },   /* OK */
    /* 20 */ { "ui\\shell\\strings\\common_button_captions", 2 },   /* CANCEL */
    /* 21 */ { MS "playlist_edit\\slayer_edit\\slayer_labels", 3 },   /* KILLS TO WIN: */
    /* 22 */ { MS "player_options_edit\\player_options_labels", 3 }, /* RESPAWN TIME: */
    /* 23 */ { MS "playlist_edit\\slayer_edit\\slayer_labels", 4 },   /* TEAM PLAY: */
    /* 24 */ { "ui\\default_multiplayer_game_setting_names", 26 },  /* Slayer */
    /* 25 */ { "ui\\shell\\main_menu\\mp_map_list", 9 },          /* Blood Gulch */
    /* 26 */ { "ui\\shell\\main_menu\\player_profiles_select\\difficulty_names", 0 },
    /* 27 */ { "ui\\shell\\main_menu\\player_profiles_select\\difficulty_names", 1 },
    /* 28 */ { "ui\\shell\\main_menu\\player_profiles_select\\difficulty_names", 2 },
    /* 29 */ { "ui\\shell\\main_menu\\player_profiles_select\\difficulty_names", 3 },
    /* 30 */ { MP "join_game\\join_game_buttons", 1 },     /* REFRESH */
    /* 31 */ { MP "join_game\\join_game_buttons", 4 },     /* JOIN GAME */
    /* 32 */ { MS "player_options_edit\\var_respawn_time", 0 },  /* INSTANT */
    /* 33 */ { MS "player_options_edit\\var_respawn_time", 1 },  /* 5 SECONDS */
    /* 34 */ { MS "player_options_edit\\var_respawn_time", 2 },
    /* 35 */ { MS "player_options_edit\\var_respawn_time", 3 },
    /* 36 */ { MS "playlist_edit\\ctf_edit\\var_time_limit", 0 },  /* NONE */
    /* 37 */ { MS "playlist_edit\\ctf_edit\\var_time_limit", 1 },  /* 10 MINUTES */
    /* 38 */ { MS "playlist_edit\\ctf_edit\\var_time_limit", 2 },
    /* 39 */ { MS "playlist_edit\\ctf_edit\\var_time_limit", 3 },
    /* 40 */ { MS "playlist_edit\\ctf_edit\\var_time_limit", 4 },
    /* 41 */ { MS "playlist_edit\\ctf_edit\\var_time_limit", 5 },
    /* 42 */ { MS "playlist_edit\\ctf_edit\\var_time_limit", 6 },  /* 45 MINUTES */
    /* 43 */ { "ui\\shell\\strings\\var_boolean", 0 },     /* NO */
    /* 44 */ { "ui\\shell\\strings\\var_boolean", 1 },     /* YES */
    /* 45 */ { "ui\\shell\\strings\\game_variant_descriptions", 36 }, /* Slayer, 25 kills */
    /* 46 */ { "ui\\shell\\main_menu\\main_menu_options", 2 },  /* MULTIPLAYER */
    /* 47 */ { MS "vehicle_options_edit\\vehicle_options_labels", 1 },  /* WARTHOG: */
};

bool hta_shell_load(hta_shell *sh, const hta_cache *c, const hta_resource_map *bm)
{
    if (!sh || !c) return false;
    memset(sh, 0, sizeof(*sh));
    for (int i = 0; i < HTA_SHELL_ART_COUNT; i++) {
        hta_bitmap b;
        if (!bm || !decode(c, bm, SHELL_ART[i].path, SHELL_ART[i].frame, &b)) continue;
        sh->art[i].width = b.width;
        sh->art[i].height = b.height;
        sh->art[i].rgba = b.rgba;       /* ours now */
        b.rgba = NULL;
    }
    size_t cap = 256u * HTA_SHELL_TEXT_COUNT, len = 0;
    sh->text = (char *)malloc(cap + 1u);
    if (!sh->text) return false;
    const char *last_path = NULL;
    uint32_t last_tag = 0;
    for (int i = 0; i < HTA_SHELL_TEXT_COUNT; i++) {
        char w[256] = "";
        if (SHELL_TEXT[i].path != last_path) {
            last_path = SHELL_TEXT[i].path;
            last_tag = hta_ustr_find(c, last_path);
        }
        if (last_tag) hta_ustr_get(c, last_tag, SHELL_TEXT[i].index, w, sizeof(w));
        for (char *p = w; *p; p++) if (*p == 0x1E) *p = ' ';
        size_t n = strlen(w);
        if (len + n + 1u > cap) n = cap - len - 1u;
        memcpy(sh->text + len, w, n);
        len += n;
        sh->text[len++] = 0x1E;
    }
    sh->text[len] = 0;
    sh->loaded = true;
    return true;
}

void hta_shell_free(hta_shell *sh)
{
    if (!sh) return;
    for (int i = 0; i < HTA_SHELL_ART_COUNT; i++) free(sh->art[i].rgba);
    free(sh->text);
    memset(sh, 0, sizeof(*sh));
}
