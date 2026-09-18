/* htaview — load a cache file, extract the first structure BSP, render it
 * offscreen with Vulkan, and write PPM images from a few camera angles.
 *
 * This is the host-side visual check. It needs no window system, so it works
 * over SSH and doubles as an automated renderer test.
 *
 *   htaview <cache.map> [--out prefix] [--width N] [--height N] [--shots N]
 */
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/bitmap.h"
#include "asset/model.h"
#include "engine/camera.h"
#include "gfx/gfx.h"
#include "engine/scene_light.h"
#include "platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *b = malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *out = (size_t)n;
    return b;
}

static bool write_ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) fwrite(&rgba[i * 4], 1, 3, f);
    fclose(f);
    return true;
}

/* fraction of pixels that differ from the clear colour — our "did anything
 * actually draw?" metric */
static double coverage(const uint8_t *rgba, uint32_t w, uint32_t h,
                       const float clear[3])
{
    uint8_t cr = (uint8_t)(clear[0] * 255.0f + 0.5f);
    uint8_t cg = (uint8_t)(clear[1] * 255.0f + 0.5f);
    uint8_t cb = (uint8_t)(clear[2] * 255.0f + 0.5f);
    uint64_t hit = 0, total = (uint64_t)w * h;
    for (uint64_t i = 0; i < total; i++) {
        int dr = (int)rgba[i*4+0] - cr, dg = (int)rgba[i*4+1] - cg, db = (int)rgba[i*4+2] - cb;
        if (dr*dr + dg*dg + db*db > 48) hit++;
    }
    return total ? (double)hit / (double)total : 0.0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cache.map> [--out prefix] [--width N] [--height N] [--shots N]\n", argv[0]);
        return 2;
    }
    const char *prefix = "bloodgulch";
    uint32_t W = 1280, H = 720, shots = 4;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--out")    && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "--width")  && i + 1 < argc) W = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) H = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shots")  && i + 1 < argc) shots = (uint32_t)atoi(argv[++i]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (W == 0 || H == 0 || W > 8192 || H > 8192) { fprintf(stderr, "bad size\n"); return 2; }
    if (shots > 64) shots = 64;

    size_t size = 0;
    uint8_t *data = slurp(argv[1], &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    char err[HTA_ERRLEN] = {0};
    hta_cache c;
    if (!hta_cache_open(&c, data, size, err, sizeof(err))) {
        fprintf(stderr, "cache: %s\n", err); free(data); return 1;
    }
    printf("map            %s (%s, engine %u)\n", c.name,
           c.is_demo_layout ? "Trial layout" : "retail layout", c.engine);

    double t0 = hta_time_seconds();
    hta_bsp_mesh mesh;
    if (!hta_bsp_load_first(&c, &mesh, err, sizeof(err))) {
        fprintf(stderr, "bsp: %s\n", err); free(data); return 1;
    }
    double t_parse = hta_time_seconds() - t0;

    printf("geometry       %u verts, %u tris, %u submeshes  (parsed in %.1f ms)\n",
           mesh.vertex_count, mesh.index_count / 3, mesh.submesh_count, t_parse * 1000.0);
    printf("bounds         (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n",
           mesh.bounds_min[0], mesh.bounds_min[1], mesh.bounds_min[2],
           mesh.bounds_max[0], mesh.bounds_max[1], mesh.bounds_max[2]);

    /* bitmaps.map lives next to the cache on Gearbox Trial */
    hta_resource_map rm;
    memset(&rm, 0, sizeof(rm));
    uint8_t *bmdata = NULL; size_t bmsz = 0;
    {
        char bmpath[1024];
        snprintf(bmpath, sizeof(bmpath), "%s", argv[1]);
        char *slash = strrchr(bmpath, '/');
        if (slash) snprintf(slash + 1, sizeof(bmpath) - (size_t)(slash + 1 - bmpath), "bitmaps.map");
        else snprintf(bmpath, sizeof(bmpath), "bitmaps.map");
        bmdata = slurp(bmpath, &bmsz);
        if (bmdata && hta_resource_open(&rm, bmdata, bmsz, err, sizeof(err)))
            printf("bitmaps.map    %s (%.1f MiB)\n", bmpath, bmsz / (1024.0 * 1024.0));
        else
            printf("bitmaps.map    not found next to cache (untextured)\n");
    }
    if (!hta_bsp_load_textures(&c, rm.data ? &rm : NULL, &mesh, err, sizeof(err)))
        printf("textures       failed: %s\n", err);
    else
        printf("textures       %u unique decoded\n", mesh.texture_count);
    if (hta_scenario_add_objects(&mesh, &c, rm.data ? &rm : NULL, err, sizeof(err)))
        printf("objects        %s  (%u verts, %u submeshes)\n", err, mesh.vertex_count, mesh.submesh_count);
    hta_bsp_mesh sky;
    memset(&sky, 0, sizeof(sky));
    int have_sky = 0;
    if (hta_sky_load(&sky, &c, rm.data ? &rm : NULL, err, sizeof(err))) {
        have_sky = 1;
        printf("sky            %u verts, %u submeshes\n", sky.vertex_count, sky.submesh_count);
    } else {
        printf("sky            %s\n", err);
    }

    t0 = hta_time_seconds();
    hta_gfx *g = hta_gfx_create_offscreen(W, H, err, sizeof(err));
    if (!g) { fprintf(stderr, "vulkan: %s\n", err); hta_bsp_free(&mesh); free(data); free(bmdata); return 1; }
    double t_gfx = hta_time_seconds() - t0;
    printf("gpu            %s (init %.1f ms)\n", hta_gfx_device_name(g), t_gfx * 1000.0);

    t0 = hta_time_seconds();
    hta_gfx_mesh *gm = hta_gfx_mesh_upload(g, &mesh, err, sizeof(err));
    hta_gfx_mesh *gs = NULL;
    if (have_sky) {
        gs = hta_gfx_mesh_upload(g, &sky, err, sizeof(err));
        if (!gs) fprintf(stderr, "sky upload: %s\n", err);
    }
    if (!gm) { fprintf(stderr, "upload: %s\n", err); hta_gfx_destroy(g); hta_bsp_free(&mesh); free(data); free(bmdata); return 1; }
    double t_upload = hta_time_seconds() - t0;
    printf("upload         %.1f ms, device memory %.2f MiB\n",
           t_upload * 1000.0, hta_gfx_device_memory_used(g) / (1024.0*1024.0));

    /* frame the whole BSP: orbit the bounding sphere */
    float ctr[3], radius = 0.0f;
    for (int k = 0; k < 3; k++) ctr[k] = 0.5f * (mesh.bounds_min[k] + mesh.bounds_max[k]);
    for (int k = 0; k < 3; k++) {
        float half = 0.5f * (mesh.bounds_max[k] - mesh.bounds_min[k]);
        if (half > radius) radius = half;
    }
    if (radius < 1e-3f) radius = 1.0f;

    hta_scene scene;
    memset(&scene, 0, sizeof(scene));
    hta_scene_light_from_bsp(&mesh, scene.light_dir, scene.light_color, scene.ambient);
    scene.clear[0] = 0.42f; scene.clear[1] = 0.55f; scene.clear[2] = 0.72f;

    uint8_t *pixels = malloc((size_t)W * H * 4);
    if (!pixels) { fprintf(stderr, "oom\n"); return 1; }

    /* warm up, then time steady-state frames */
    hta_camera cam;
    hta_camera_init(&cam);
    cam.aspect = (float)W / (float)H;
    cam.zfar   = radius * 12.0f;
    cam.znear  = radius * 0.005f > 0.01f ? radius * 0.005f : 0.01f;

    printf("\n");
    double total_render = 0.0;
    uint32_t drawn = 0;
    for (uint32_t s = 0; s < shots; s++) {
        float ang = 6.2831853f * (float)s / (float)(shots ? shots : 1);
        float dist = radius * 2.4f;
        cam.pos[0] = ctr[0] + cosf(ang) * dist;
        cam.pos[1] = ctr[1] + sinf(ang) * dist;
        cam.pos[2] = ctr[2] + radius * 0.85f;
        /* aim at the centre */
        float dx = ctr[0]-cam.pos[0], dy = ctr[1]-cam.pos[1], dz = ctr[2]-cam.pos[2];
        cam.yaw   = atan2f(dy, dx);
        cam.pitch = atan2f(dz, sqrtf(dx*dx + dy*dy));

        double r0 = hta_time_seconds();
        bool ok = hta_gfx_draw(g, &cam, &scene, gm, gs, NULL, NULL, NULL);
        double r1 = hta_time_seconds();
        if (!ok) { fprintf(stderr, "draw failed on shot %u\n", s); break; }
        total_render += (r1 - r0);
        drawn++;

        if (!hta_gfx_readback(g, pixels, (size_t)W * H * 4)) {
            fprintf(stderr, "readback failed\n"); break;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s_%02u.ppm", prefix, s);
        if (!write_ppm(path, pixels, W, H)) { fprintf(stderr, "cannot write %s\n", path); break; }
        printf("  %s  coverage %5.1f%%  (%.1f ms)\n", path,
               coverage(pixels, W, H, scene.clear) * 100.0, (r1 - r0) * 1000.0);
    }

    if (drawn) {
        printf("\nrender         %u frames, avg %.2f ms (%.0f fps equivalent, %ux%u, incl. readback)\n",
               drawn, total_render * 1000.0 / drawn, drawn / total_render, W, H);
        printf("triangles      %u per frame\n", mesh.index_count / 3);
    }

    free(pixels);
    if (gs) hta_gfx_mesh_free(g, gs);
    hta_gfx_mesh_free(g, gm);
    hta_gfx_destroy(g);
    hta_bsp_free(&mesh);
    hta_bsp_free(&sky);
    free(data);
    free(bmdata);
    return drawn ? 0 : 1;
}
