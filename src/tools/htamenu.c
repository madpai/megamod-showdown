/* htamenu -- the Trial's main-menu scene from the owner's ui.map, rendered
 * offscreen from each of the scenario's named camera points. For looking at
 * the menu without a phone.
 *
 *   htamenu <ui.map> [--out prefix] [--width W] [--height H]
 */
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/bitmap.h"
#include "asset/model.h"
#include "game/menu.h"
#include "gfx/gfx.h"
#include "platform/platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f); if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n); if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *size = (size_t)n; return p;
}

static bool ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb"); if (!f) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) fwrite(rgba + i * 4, 1, 3, f);
    fclose(f); return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: htamenu <ui.map> [--out prefix] [--width W] [--height H] [--time S] [--select N] [--focus camera]\n"); return 2; }
    const char *prefix = "menu";
    uint32_t W = 960, H = 540;
    float at = 0.0f;
    int select = 0;
    float yaw_override = -100.0f, pitch_override = 0.0f;
    const char *focus = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) W = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) H = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--time") && i + 1 < argc) at = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--select") && i + 1 < argc) select = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--focus") && i + 1 < argc) focus = argv[++i];
        else if (!strcmp(argv[i], "--art") && i + 2 < argc) {
            /* --art title.ppm RIGHT: a game's own title art (binary P6),
             * RIGHT where the art ends as a fraction of its width. */
            const char *ap = argv[++i]; float right = strtof(argv[++i], NULL);
            FILE *af = fopen(ap, "rb"); unsigned aw = 0, ah = 0, mx = 0;
            if (af && fscanf(af, "P6 %u %u %u", &aw, &ah, &mx) == 3 && mx == 255 && aw && ah) {
                fgetc(af);
                uint8_t *rgb = malloc((size_t)aw * ah * 3), *rgba = malloc((size_t)aw * ah * 4);
                if (rgb && rgba && fread(rgb, 3, (size_t)aw * ah, af) == (size_t)aw * ah) {
                    for (size_t p = 0; p < (size_t)aw * ah; p++) {
                        rgba[p*4] = rgb[p*3]; rgba[p*4+1] = rgb[p*3+1]; rgba[p*4+2] = rgb[p*3+2]; rgba[p*4+3] = 255;
                    }
                    hta_menu_set_art(rgba, aw, ah, right);
                }
                free(rgb); free(rgba);
            }
            if (af) fclose(af);
        }
        else if (!strcmp(argv[i], "--look") && i + 2 < argc) {
            yaw_override = strtof(argv[++i], NULL); pitch_override = strtof(argv[++i], NULL);
        }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    char err[HTA_ERRLEN] = {0};
    size_t n = 0, bn = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    hta_cache c;
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) { fprintf(stderr, "%s\n", err); return 1; }
    char bmpath[1024];
    snprintf(bmpath, sizeof(bmpath), "%s", argv[1]);
    char *slash = strrchr(bmpath, '/');
    if (slash) snprintf(slash + 1, sizeof(bmpath) - (size_t)(slash + 1 - bmpath), "bitmaps.map");
    uint8_t *bdata = slurp(bmpath, &bn);
    hta_resource_map bm = {0};
    if (bdata) hta_resource_open(&bm, bdata, bn, err, sizeof(err));

    static hta_menu menu;
    if (!hta_menu_load(&menu, &c, bdata ? &bm : NULL, err, sizeof(err))) {
        fprintf(stderr, "menu: %s\n", err); return 1;
    }
    printf("menu           %s\n", err);
    /* The submenus' art and words, which the phone's overlay draws. */
    static hta_shell shell;
    hta_shell_load(&shell, &c, bdata ? &bm : NULL);
    int arts = 0, words = 0;
    for (int i = 0; i < HTA_SHELL_ART_COUNT; i++) arts += shell.art[i].rgba != NULL;
    for (const char *p = shell.text, *q = p; p && *p; p = q + 1) {
        q = strchr(p, 0x1E); if (!q) break;
        words += q > p;
    }
    printf("shell          %d/%d art, %d/%d words\n", arts, HTA_SHELL_ART_COUNT,
           words, HTA_SHELL_TEXT_COUNT);
    hta_shell_free(&shell);
    if (focus) {
        /* A submenu's shot: the glide finished, words hidden. */
        hta_menu_focus(&menu, focus);
        menu.cam_blend = 1.0f;
        menu.shell = true;
    }
    hta_gfx *g = hta_gfx_create_offscreen(W, H, err, sizeof(err));
    if (!g) { fprintf(stderr, "Vulkan: %s\n", err); return 1; }
    hta_gfx_mesh *scene = menu.scene.index_count ? hta_gfx_mesh_upload(g, &menu.scene, err, sizeof(err)) : NULL;
    hta_gfx_mesh *sky = menu.sky.index_count ? hta_gfx_mesh_upload(g, &menu.sky, err, sizeof(err)) : NULL;
    hta_gfx_mesh *ui = hta_gfx_mesh_upload_dynamic(g, &menu.overlay, err, sizeof(err));
    if (!ui) { fprintf(stderr, "overlay: %s\n", err); return 1; }
    uint8_t *rgba = malloc((size_t)W * H * 4u);
    int shots = 0;
    menu.selected = select;
    for (float t = 0.0f; t <= at + 1e-3f; t += 1.0f / 30.0f) hta_menu_update(&menu, 1.0f / 30.0f);
    for (int k = 0; k < 2; k++) {
        hta_menu_layout(&menu, W, H);
        hta_camera cam;
        hta_menu_camera(&menu, &cam, (float)W / (float)H);
        if (yaw_override > -99.0f) { cam.yaw = yaw_override; cam.pitch = pitch_override; }
        hta_scene sc = menu.light;
        hta_gfx_overlay ov = { ui, menu.overlay.vertices, menu.overlay.vertex_count,
                               menu.overlay.submeshes, menu.overlay.submesh_count };
        if (!hta_gfx_draw(g, &cam, &sc, menu.art ? NULL : scene, menu.art ? NULL : sky, NULL, NULL, 0, NULL, &ov) ||
            !hta_gfx_readback(g, rgba, (size_t)W * H * 4u)) { fprintf(stderr, "render failed\n"); return 1; }
        char path[512];
        snprintf(path, sizeof(path), "%s_%02d.ppm", prefix, shots++);
        ppm(path, rgba, W, H);
        printf("  %s  camera %.2f %.2f %.2f yaw %.2f pitch %.2f\n", path, cam.pos[0], cam.pos[1], cam.pos[2],
               cam.yaw, cam.pitch);
        for (int f = 0; f < 90; f++) hta_menu_update(&menu, 1.0f / 30.0f);
    }
    free(rgba);
    hta_gfx_mesh_free(g, ui);
    if (sky) hta_gfx_mesh_free(g, sky);
    if (scene) hta_gfx_mesh_free(g, scene);
    hta_gfx_destroy(g);
    hta_menu_free(&menu);
    free(bdata); free(data);
    return 0;
}
