/* The screen HUD built from Halo's own weapon HUD interface tag.
 * Needs a real cache: pass bloodgulch.map; bitmaps.map is read beside it. */
#include "engine/hud.h"
#include "asset/cache.h"
#include "asset/weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

int main(int argc, char **argv)
{
    printf("hud\n");
    if (argc < 2) {
        printf("  skip: no map path (pass bloodgulch.map)\n");
        return 0;
    }
    size_t sz = 0;
    uint8_t *data = slurp(argv[1], &sz);
    CHECK(data != NULL, "map reads");
    if (!data) return 1;
    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    CHECK(hta_cache_open(&c, data, sz, err, sizeof(err)), "cache opens");

    char bmp[1024];
    snprintf(bmp, sizeof(bmp), "%s", argv[1]);
    char *slash = strrchr(bmp, '/');
    if (slash) snprintf(slash + 1, sizeof(bmp) - (size_t)(slash + 1 - bmp), "bitmaps.map");
    else snprintf(bmp, sizeof(bmp), "bitmaps.map");
    size_t bsz = 0;
    uint8_t *bdata = slurp(bmp, &bsz);
    hta_resource_map bm;
    memset(&bm, 0, sizeof(bm));
    int have_bitmaps = bdata && hta_resource_open(&bm, bdata, bsz, err, sizeof(err));
    if (!have_bitmaps) {
        printf("  skip: needs bitmaps.map for the reticle\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    hta_weapon_def w;
    memset(&w, 0, sizeof(w));
    CHECK(hta_weapon_load_default(&c, NULL, &w, NULL, err, sizeof(err)), "weapon loads");

    hta_hud h;
    CHECK(hta_hud_load(&h, &c, &bm, &w, err, sizeof(err)), "hud loads");

    printf("\n[crosshair]\n");
    CHECK(h.have_cross, "the assault rifle's wphi yields an aiming reticle");
    if (!h.have_cross) {
        printf("    (%s)\n", err);
        printf("\n%d checks, %d failures\n", checks, failures);
        return 1;
    }
    printf("    %.0f px native, uv %.4f,%.4f .. %.4f,%.4f, tint %.2f %.2f %.2f a %.2f\n",
           h.cross_px, h.cross_uv[0], h.cross_uv[1], h.cross_uv[2], h.cross_uv[3],
           h.mesh.submeshes[0].tint[0], h.mesh.submeshes[0].tint[1],
           h.mesh.submeshes[0].tint[2], h.mesh.submeshes[0].tint[3]);

    CHECK(h.cross_px > 8.0f && h.cross_px < 200.0f,
          "its native size is sane for a 640x480 canvas");
    /* The reticle is one sprite of a shared sheet, not the whole sheet. */
    CHECK(h.cross_uv[2] > h.cross_uv[0] && h.cross_uv[3] > h.cross_uv[1],
          "the sprite rectangle is non-empty");
    CHECK(h.cross_uv[2] - h.cross_uv[0] < 0.95f,
          "and is a sub-rectangle, not the whole sheet");

    /* Halo's HUD blue, from the tag: ColorARGBInt is blue, green, red, alpha,
     * and an alpha of 0 there means opaque. Getting the byte order wrong
     * turns the reticle orange. */
    const float *t = h.mesh.submeshes[0].tint;
    CHECK(t[2] > t[0] && t[1] > t[0], "the tint is blue-dominant, not orange");
    CHECK(fabsf(t[0] - 40.0f/255.0f) < 0.02f, "red is 40");
    CHECK(fabsf(t[1] - 150.0f/255.0f) < 0.02f, "green is 150");
    CHECK(fabsf(t[2] - 255.0f/255.0f) < 0.02f, "blue is 255");
    CHECK(t[3] > 0.99f, "alpha 0 in the tag means opaque");

    CHECK(h.mesh.submeshes[0].draw_mode == HTA_DRAW_ALPHA,
          "drawn alpha-blended, since the art is a white mask");
    CHECK(h.mesh.vertex_count == 4 && h.mesh.index_count == 6, "one quad");

    printf("\n[layout]\n");
    {
        /* A wide phone screen. The quad must be centred and SQUARE in
         * pixels, which in clip space means a taller-than-wide rectangle. */
        const uint32_t W = 2340, H = 1080;
        hta_hud_layout(&h, W, H);
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (uint32_t i = 0; i < 4; i++) {
            const float *p = h.mesh.vertices[i].pos;
            if (p[0] < minx) minx = p[0];
            if (p[0] > maxx) maxx = p[0];
            if (p[1] < miny) miny = p[1];
            if (p[1] > maxy) maxy = p[1];
            if (p[2] != 0.0f) { printf("  FAIL: z is not 0\n"); failures++; }
        }
        checks++;
        printf("    clip x %.4f..%.4f  y %.4f..%.4f\n", minx, maxx, miny, maxy);
        CHECK(fabsf((minx + maxx) * 0.5f) < 1e-4f, "horizontally centred");
        CHECK(fabsf((miny + maxy) * 0.5f) < 1e-4f, "vertically centred");

        float px_w = (maxx - minx) * 0.5f * (float)W;
        float px_h = (maxy - miny) * 0.5f * (float)H;
        printf("    on screen: %.1f x %.1f px\n", px_w, px_h);
        CHECK(fabsf(px_w - px_h) < 1.0f, "square on screen, not stretched by aspect");
        /* Halo scales its HUD by height. */
        float want = h.cross_px * (float)H / HTA_HUD_CANVAS_H;
        CHECK(fabsf(px_w - want) < 1.0f, "scaled by screen height, as Halo does");
        CHECK(px_w > 4.0f && px_w < (float)H * 0.5f, "and is a believable size");

        /* The same reticle on a different screen must stay square and the
         * same apparent size relative to height. */
        hta_hud_layout(&h, 1080, 1080);
        float a0 = h.mesh.vertices[1].pos[0] - h.mesh.vertices[0].pos[0];
        float b0 = h.mesh.vertices[2].pos[1] - h.mesh.vertices[1].pos[1];
        CHECK(fabsf(a0 * 1080.0f - b0 * 1080.0f) < 1.0f,
              "still square on a square screen");

        /* UVs must survive layout. */
        CHECK(fabsf(h.mesh.vertices[0].uv[0] - h.cross_uv[0]) < 1e-6f &&
              fabsf(h.mesh.vertices[2].uv[0] - h.cross_uv[2]) < 1e-6f,
              "the sprite's UVs are kept");

        /* A zero-sized surface must not divide by zero or move anything. */
        float keep = h.mesh.vertices[0].pos[0];
        hta_hud_layout(&h, 0, 0);
        CHECK(h.mesh.vertices[0].pos[0] == keep, "a zero-sized screen is ignored");
    }

    hta_hud_free(&h);
    CHECK(h.mesh.vertices == NULL, "free clears the mesh");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
