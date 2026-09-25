/* Renders a synthetic scene -- no game data -- through every renderer path
 * and checks the pictures numerically:
 *   - the legacy direct path draws the scene;
 *   - the composed path with every effect neutral matches it (so turning
 *     post on cannot silently change the game's look);
 *   - half resolution is close to full;
 *   - bloom brightens the neighbourhood of highlights;
 *   - fog pulls distant pixels toward the fog colour and leaves near ones;
 *   - MSAA, dynamic resolution and a settings round trip all render.
 * Needs a Vulkan device (lavapipe is enough); exits 77 (skipped) without one.
 * HTA_RENDER_OUT=dir writes a PPM per case for eyeballing. */
#include "gfx/gfx.h"
#include "engine/camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 180

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *solid(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint32_t n)
{
    uint8_t *p = malloc((size_t)n * n * 4);
    for (uint32_t i = 0; i < n * n; i++) { p[i*4] = r; p[i*4+1] = g; p[i*4+2] = b; p[i*4+3] = a; }
    return p;
}

static void quad(hta_vertex *v, uint32_t *ix, uint32_t *vc, uint32_t *icount,
                 const float a[3], const float b[3], const float c[3], const float d[3], float uvs)
{
    const float *p[4] = { a, b, c, d };
    const float uv[4][2] = { {0,0}, {uvs,0}, {uvs,uvs}, {0,uvs} };
    uint32_t base = *vc;
    for (int i = 0; i < 4; i++) {
        memset(&v[*vc], 0, sizeof(hta_vertex));
        memcpy(v[*vc].pos, p[i], 12);
        v[*vc].normal[2] = 1.0f;
        v[*vc].uv[0] = uv[i][0]; v[*vc].uv[1] = uv[i][1];
        v[*vc].lm_uv[0] = 0.5f; v[*vc].lm_uv[1] = 0.5f;
        (*vc)++;
    }
    const uint32_t t[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) ix[(*icount)++] = base + t[i];
}

/* Ground (checker), a row of pillars marching into the distance, and two
 * glowing panels whose lightmap is white: 2.0 in the shader, over 1. */
static void build_scene(hta_bsp_mesh *m)
{
    memset(m, 0, sizeof(*m));
    static hta_vertex v[1024];
    static uint32_t ix[2048];
    uint32_t vc = 0, ic = 0;
    static hta_submesh sm[3];
    for (int i = 0; i < 3; i++) hta_submesh_init(&sm[i]);

    /* ground */
    sm[0].first_index = ic;
    { float a[3]={-50,-200,0}, b[3]={600,-200,0}, c[3]={600,200,0}, d[3]={-50,200,0};
      quad(v, ix, &vc, &ic, a, b, c, d, 40.0f); }
    /* pillars every 40 units out to 560 */
    for (int k = 0; k < 14; k++) {
        float x = 40.0f + 40.0f * (float)k, y = (k & 1) ? 12.0f : -12.0f;
        float a[3]={x,y-2,0}, b[3]={x,y+2,0}, c[3]={x,y+2,14}, d[3]={x,y-2,14};
        quad(v, ix, &vc, &ic, a, b, c, d, 1.0f);
    }
    sm[0].index_count = ic - sm[0].first_index;
    sm[0].albedo_tex = 0; sm[0].draw_mode = HTA_DRAW_OPAQUE;

    /* glowing panels, near */
    sm[1].first_index = ic;
    { float a[3]={25,-6,2}, b[3]={25,-3,2}, c[3]={25,-3,6}, d[3]={25,-6,6};
      quad(v, ix, &vc, &ic, a, b, c, d, 1.0f); }
    { float a[3]={25,3,2}, b[3]={25,6,2}, c[3]={25,6,6}, d[3]={25,3,6};
      quad(v, ix, &vc, &ic, a, b, c, d, 1.0f); }
    sm[1].index_count = ic - sm[1].first_index;
    sm[1].albedo_tex = 1; sm[1].lightmap_tex = 2; sm[1].draw_mode = HTA_DRAW_OPAQUE;

    static hta_bsp_texture tex[3];
    uint8_t *checker = malloc(64 * 64 * 4);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        uint8_t c = (((x >> 3) ^ (y >> 3)) & 1) ? 150 : 90;
        uint8_t *p = checker + (y * 64 + x) * 4;
        p[0] = c; p[1] = (uint8_t)(c + 10); p[2] = (uint8_t)(c - 20); p[3] = 255;
    }
    tex[0] = (hta_bsp_texture){ .width = 64, .height = 64, .rgba = checker, .tint = 0xFFFFFF };
    tex[1] = (hta_bsp_texture){ .width = 4, .height = 4, .rgba = solid(255, 230, 180, 255, 4), .tint = 0xFFFFFF };
    tex[2] = (hta_bsp_texture){ .width = 4, .height = 4, .rgba = solid(255, 255, 255, 255, 4), .tint = 0xFFFFFF };

    m->vertices = v; m->vertex_count = vc;
    m->indices = ix; m->index_count = ic;
    m->submeshes = sm; m->submesh_count = 2;
    m->textures = tex; m->texture_count = 3;
    m->bounds_min[0] = -50; m->bounds_min[1] = -200; m->bounds_max[0] = 600; m->bounds_max[1] = 200;
    m->bounds_max[2] = 14;
}

static const hta_scene kScene = {
    .light_dir = { -0.3f, 0.2f, -0.9f }, .light_color = { 0.8f, 0.8f, 0.8f },
    .ambient = { 0.3f, 0.3f, 0.3f }, .clear = { 0.42f, 0.55f, 0.72f },
};

static hta_camera camera(void)
{
    hta_camera c;
    hta_camera_init(&c);
    c.pos[0] = 0; c.pos[1] = 0; c.pos[2] = 4;
    c.yaw = 0; c.pitch = -0.08f;
    c.aspect = (float)W / (float)H;
    c.znear = 0.1f; c.zfar = 1000.0f;
    return c;
}

static bool render(const hta_gfx_settings *s, const hta_bsp_mesh *scene_mesh, uint8_t *out,
                   const char *name, float dyn_scale)
{
    char err[512] = { 0 };
    hta_gfx *g = hta_gfx_create_offscreen_ex(W, H, s, err, sizeof err);
    if (!g) { printf("create(%s): %s\n", name, err); return false; }
    hta_gfx_mesh *m = hta_gfx_mesh_upload(g, scene_mesh, err, sizeof err);
    if (!m) { printf("upload(%s): %s\n", name, err); hta_gfx_destroy(g); return false; }
    if (dyn_scale > 0.0f) hta_gfx_set_render_scale(g, dyn_scale);
    hta_camera cam = camera();
    bool ok = true;
    /* Twice: the second frame exercises the frame-to-frame dependencies. */
    for (int i = 0; i < 2 && ok; i++)
        ok = hta_gfx_draw(g, &cam, &kScene, m, NULL, NULL, NULL, 0, NULL, NULL);
    ok = ok && hta_gfx_readback(g, out, (size_t)W * H * 4);
    const char *dir = getenv("HTA_RENDER_OUT");
    if (ok && dir) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (int i = 0; i < W * H; i++) fwrite(out + i * 4, 1, 3, f);
            fclose(f);
        }
    }
    hta_gfx_mesh_free(g, m);
    hta_gfx_destroy(g);
    return ok;
}

/* Mean over a rectangle, per channel, 0..255. */
static void mean_rect(const uint8_t *img, int x0, int y0, int x1, int y1, double out[3])
{
    double s[3] = { 0, 0, 0 };
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        for (int k = 0; k < 3; k++) s[k] += img[(y * W + x) * 4 + k];
        n++;
    }
    for (int k = 0; k < 3; k++) out[k] = n ? s[k] / n : 0.0;
}

static double mean_abs_diff(const uint8_t *a, const uint8_t *b)
{
    double s = 0;
    for (int i = 0; i < W * H; i++) for (int k = 0; k < 3; k++)
        s += fabs((double)a[i*4+k] - (double)b[i*4+k]);
    return s / (W * H * 3);
}

static int max_abs_diff(const uint8_t *a, const uint8_t *b)
{
    int m = 0;
    for (int i = 0; i < W * H; i++) for (int k = 0; k < 3; k++) {
        int d = abs((int)a[i*4+k] - (int)b[i*4+k]);
        if (d > m) m = d;
    }
    return m;
}

/* Settings with the composed path forced on but every effect neutral. */
static hta_gfx_settings neutral_post(void)
{
    hta_gfx_settings s;
    hta_gfx_settings_preset(&s, HTA_QUALITY_MEDIUM);
    s.preset = HTA_QUALITY_CUSTOM;
    s.render_scale = 1.0f; s.dynamic_res = false; s.msaa = 1; s.anisotropy = 8;
    s.post = true; s.tonemap = HTA_TONEMAP_NONE; s.exposure = 1; s.contrast = 1;
    s.saturation = 1; s.warmth = 0; s.bloom = false; s.fxaa = false; s.sharpen = 0;
    s.vignette = 0; s.fog = false; s.draw_distance = 1.0f;
    return s;
}

int main(void)
{
    static uint8_t legacy[W*H*4], neutral[W*H*4], half[W*H*4], bloom[W*H*4], nobloom[W*H*4];
    static uint8_t fog[W*H*4], img[W*H*4];
    hta_bsp_mesh scene;
    build_scene(&scene);

    char err[256];
    hta_gfx *probe = hta_gfx_create_offscreen(8, 8, err, sizeof err);
    if (!probe) { printf("no Vulkan device (%s): skipped\n", err); return 77; }
    printf("device: %s\n", hta_gfx_device_name(probe));
    hta_gfx_destroy(probe);

    /* 1. The legacy direct path draws the scene, not just the clear. */
    CHECK(render(NULL, &scene, legacy, "legacy", 0), "legacy render");
    double sky[3], ground[3];
    mean_rect(legacy, 0, 0, W, 10, sky);
    mean_rect(legacy, 0, H - 20, W, H, ground);
    CHECK(fabs(sky[2] - 0.72 * 255) < 3, "top rows are the clear colour (b=%.1f)", sky[2]);
    CHECK(fabs(ground[2] - sky[2]) > 20, "ground differs from sky");

    /* 2. Neutral composition matches it: only the half-step dither and
     * half-float rounding may differ. */
    hta_gfx_settings s = neutral_post();
    CHECK(render(&s, &scene, neutral, "neutral_post", 0), "neutral render");
    int md = max_abs_diff(legacy, neutral);
    CHECK(md <= 2, "neutral post differs from legacy by up to %d", md);

    /* 3. Half resolution upscaled stays close on average. */
    hta_gfx_settings p;
    hta_gfx_settings_preset(&p, HTA_QUALITY_POTATO);
    CHECK(render(&p, &scene, half, "potato", 0), "potato render");
    double mad = mean_abs_diff(legacy, half);
    CHECK(mad < 8.0, "potato mean difference %.2f", mad);

    /* 4. Bloom lights the air around the panels. Panels are at x=25,
     * y=+-3..6, z=2..6; the gap between them is the centre of the view. */
    s = neutral_post(); s.bloom = true; s.bloom_strength = 1.0f; s.bloom_threshold = 1.0f;
    CHECK(render(&s, &scene, bloom, "bloom", 0), "bloom render");
    s.bloom = false;
    CHECK(render(&s, &scene, nobloom, "nobloom", 0), "nobloom render");
    double b1[3], b0[3];
    mean_rect(bloom, W/2 - 6, H/2 - 30, W/2 + 6, H/2 - 5, b1);
    mean_rect(nobloom, W/2 - 6, H/2 - 30, W/2 + 6, H/2 - 5, b0);
    CHECK(b1[0] > b0[0] + 4, "bloom brightens between the panels (%.1f vs %.1f)", b1[0], b0[0]);
    double far1[3], far0[3];
    mean_rect(bloom, 0, H - 12, 40, H, far1);
    mean_rect(nobloom, 0, H - 12, 40, H, far0);
    CHECK(fabs(far1[0] - far0[0]) < 3, "bloom leaves dark ground alone (%.1f vs %.1f)", far1[0], far0[0]);

    /* 5. Fog: forward, so it works without post. Distant ground (just
     * below the horizon) moves toward the fog colour; the ground at the
     * bottom of the frame, a few units away, barely changes. */
    hta_gfx_settings f = neutral_post();
    f.post = false; f.render_scale = 1.0f; f.msaa = 1;       /* direct path */
    f.fog = true; f.fog_density = 0.01f; f.fog_start = 10.0f;
    f.fog_from_scene = false; f.fog_color[0] = 1.0f; f.fog_color[1] = 0.0f; f.fog_color[2] = 1.0f;
    CHECK(render(&f, &scene, fog, "fog", 0), "fog render");
    double hz1[3], hz0[3], nr1[3], nr0[3];
    /* The horizon: the first row, from the top, that is not all sky. */
    int hy = 0;
    for (int y = 0; y < H && !hy; y++) {
        double r[3];
        mean_rect(legacy, 0, y, W / 4, y + 1, r);   /* left edge: no pillars */
        if (fabs(r[2] - 0.72 * 255) > 10) hy = y;
    }
    CHECK(hy > 10 && hy < H - 20, "found the horizon (row %d)", hy);
    mean_rect(fog, 0, hy, W / 4, hy + 3, hz1);
    mean_rect(legacy, 0, hy, W / 4, hy + 3, hz0);
    mean_rect(fog, 0, H - 6, W, H, nr1);
    mean_rect(legacy, 0, H - 6, W, H, nr0);
    CHECK(hz1[0] - hz0[0] > 40 && hz0[1] - hz1[1] > 30, "far ground fogs magenta (r %.0f->%.0f g %.0f->%.0f)",
          hz0[0], hz1[0], hz0[1], hz1[1]);
    CHECK(fabs(nr1[1] - nr0[1]) < 6, "near ground stays (g %.0f vs %.0f)", nr1[1], nr0[1]);
    /* The sky is not fogged: it opts out. */
    double sk1[3];
    mean_rect(fog, 0, 0, W, 10, sk1);
    CHECK(fabs(sk1[1] - sky[1]) < 3, "sky unfogged");

    /* 6. The heavy paths render and stay the same picture. */
    hta_gfx_settings u;
    hta_gfx_settings_preset(&u, HTA_QUALITY_ULTRA);
    u.fog = false;
    CHECK(render(&u, &scene, img, "ultra", 0), "ultra render");
    CHECK(mean_abs_diff(legacy, img) < 20.0, "ultra is the same scene (%.2f)", mean_abs_diff(legacy, img));
    hta_gfx_settings hi;
    hta_gfx_settings_preset(&hi, HTA_QUALITY_HIGH);
    CHECK(render(&hi, &scene, img, "high", 0), "high render");
    /* MSAA x4 on neutral post: edges soften, the rest matches. */
    s = neutral_post(); s.msaa = 4;
    CHECK(render(&s, &scene, img, "msaa4", 0), "msaa render");
    CHECK(mean_abs_diff(neutral, img) < 2.0, "msaa matches except on edges (%.2f)", mean_abs_diff(neutral, img));
    /* Dynamic resolution: a 1.0 ceiling drawn at 0.6. */
    s = neutral_post(); s.dynamic_res = true;
    CHECK(render(&s, &scene, img, "dynres60", 0.6f), "dynamic resolution render");
    CHECK(mean_abs_diff(neutral, img) < 8.0, "dynamic 60%% close to full (%.2f)", mean_abs_diff(neutral, img));

    /* 7. apply_settings on a live renderer: direct -> composed -> direct. */
    {
        hta_gfx *g = hta_gfx_create_offscreen(W, H, err, sizeof err);
        hta_gfx_mesh *m = g ? hta_gfx_mesh_upload(g, &scene, err, sizeof err) : NULL;
        hta_camera cam = camera();
        CHECK(g && m, "live renderer");
        if (g && m) {
            CHECK(!hta_gfx_is_composed(g), "starts direct");
            hta_gfx_settings_preset(&u, HTA_QUALITY_ULTRA);
            CHECK(hta_gfx_apply_settings(g, &u, err, sizeof err), "apply ultra: %s", err);
            CHECK(hta_gfx_is_composed(g) && hta_gfx_msaa(g) >= 1, "composed now");
            CHECK(hta_gfx_draw(g, &cam, &kScene, m, NULL, NULL, NULL, 0, NULL, NULL), "draw after apply");
            CHECK(hta_gfx_apply_settings(g, &(hta_gfx_settings){0}, err, sizeof err) || 1, "garbage clamps");
            hta_gfx_settings back = neutral_post(); back.post = false;
            CHECK(hta_gfx_apply_settings(g, &back, err, sizeof err), "apply direct");
            CHECK(!hta_gfx_is_composed(g), "direct again");
            CHECK(hta_gfx_draw(g, &cam, &kScene, m, NULL, NULL, NULL, 0, NULL, NULL), "draw after revert");
            CHECK(hta_gfx_readback(g, img, sizeof img), "readback");
            CHECK(max_abs_diff(legacy, img) <= 1, "back to the legacy picture");
        }
        if (m) hta_gfx_mesh_free(g, m);
        if (g) hta_gfx_destroy(g);
    }

    for (int i = 0; i < 3; i++) free(scene.textures[i].rgba);
    if (failures) { printf("%d render check(s) failed\n", failures); return 1; }
    puts("gfx render OK");
    return 0;
}
