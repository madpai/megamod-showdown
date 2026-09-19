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
           h.cross_px, h.elem[h.cross_elem].uv[0], h.elem[h.cross_elem].uv[1], h.elem[h.cross_elem].uv[2], h.elem[h.cross_elem].uv[3],
           h.mesh.submeshes[h.elem[h.cross_elem].submesh].tint[0], h.mesh.submeshes[h.elem[h.cross_elem].submesh].tint[1],
           h.mesh.submeshes[h.elem[h.cross_elem].submesh].tint[2], h.mesh.submeshes[h.elem[h.cross_elem].submesh].tint[3]);

    CHECK(h.cross_px > 8.0f && h.cross_px < 200.0f,
          "its native size is sane for a 640x480 canvas");
    /* The reticle is one sprite of a shared sheet, not the whole sheet. */
    CHECK(h.elem[h.cross_elem].uv[2] > h.elem[h.cross_elem].uv[0] && h.elem[h.cross_elem].uv[3] > h.elem[h.cross_elem].uv[1],
          "the sprite rectangle is non-empty");
    CHECK(h.elem[h.cross_elem].uv[2] - h.elem[h.cross_elem].uv[0] < 0.95f,
          "and is a sub-rectangle, not the whole sheet");

    /* Halo's HUD blue, from the tag: ColorARGBInt is blue, green, red, alpha,
     * and an alpha of 0 there means opaque. Getting the byte order wrong
     * turns the reticle orange. */
    const float *t = h.mesh.submeshes[h.elem[h.cross_elem].submesh].tint;
    CHECK(t[2] > t[0] && t[1] > t[0], "the tint is blue-dominant, not orange");
    CHECK(fabsf(t[0] - 40.0f/255.0f) < 0.02f, "red is 40");
    CHECK(fabsf(t[1] - 150.0f/255.0f) < 0.02f, "green is 150");
    CHECK(fabsf(t[2] - 255.0f/255.0f) < 0.02f, "blue is 255");
    CHECK(t[3] > 0.99f, "alpha 0 in the tag means opaque");

    CHECK(h.mesh.submeshes[h.elem[h.cross_elem].submesh].draw_mode == HTA_DRAW_ALPHA,
          "drawn alpha-blended, since the art is a white mask");
    CHECK(h.mesh.vertex_count == h.elem_count * 4 &&
          h.mesh.index_count == h.elem_count * 6, "one quad per element");

    printf("\n[layout]\n");
    {
        /* A wide phone screen. The quad must be centred and SQUARE in
         * pixels, which in clip space means a taller-than-wide rectangle. */
        const uint32_t W = 2340, H = 1080;
        hta_hud_layout(&h, W, H);
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        uint32_t cv = h.elem[h.cross_elem].vertex;
        for (uint32_t i = cv; i < cv + 4; i++) {
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
        float a0 = h.mesh.vertices[cv+1].pos[0] - h.mesh.vertices[cv].pos[0];
        float b0 = h.mesh.vertices[cv+2].pos[1] - h.mesh.vertices[cv+1].pos[1];
        CHECK(fabsf(a0 * 1080.0f - b0 * 1080.0f) < 1.0f,
              "still square on a square screen");

        /* UVs must survive layout. */
        CHECK(fabsf(h.mesh.vertices[cv].uv[0] - h.elem[h.cross_elem].uv[0]) < 1e-6f &&
              fabsf(h.mesh.vertices[cv+2].uv[0] - h.elem[h.cross_elem].uv[2]) < 1e-6f,
              "the sprite's UVs are kept");

        /* A zero-sized surface must not divide by zero or move anything. */
        float keep = h.mesh.vertices[cv].pos[0];
        hta_hud_layout(&h, 0, 0);
        CHECK(h.mesh.vertices[cv].pos[0] == keep, "a zero-sized screen is ignored");
    }

    printf("\n[unit hud]\n");
    CHECK(h.have_unit, "the multiplayer cyborg's unhi yields shield and health");
    if (h.have_unit) {
        CHECK(h.shield_meter >= 0, "there is a shield meter");
        CHECK(h.health_meter >= 0, "there is a health meter");
        printf("    %u elements; shield full %.2f %.2f %.2f -> empty %.2f %.2f %.2f\n",
               h.elem_count, h.shield_max[0], h.shield_max[1], h.shield_max[2],
               h.shield_min[0], h.shield_min[1], h.shield_min[2]);
        printf("    health full %.2f %.2f %.2f -> empty %.2f %.2f %.2f\n",
               h.health_max[0], h.health_max[1], h.health_max[2],
               h.health_min[0], h.health_min[1], h.health_min[2]);

        /* The unit HUD hangs off the top right in Halo, and its background
         * must be behind its meter or the bar is hidden. */
        CHECK(h.elem[h.shield_meter].anchor == HTA_HUD_ANCHOR_TOP_RIGHT,
              "anchored top right, as the tag says");
        CHECK(h.elem[h.shield_meter].submesh > 0,
              "the shield plate is drawn before its meter");

        /* Full shield is Halo's cyan; empty is the dark blue the tag gives. */
        CHECK(h.shield_max[2] > h.shield_max[0], "a full shield is blue, not red");
        CHECK(h.health_min[0] > h.health_min[2], "low health is red, not blue");

        hta_submesh *sh = &h.mesh.submeshes[h.elem[h.shield_meter].submesh];
        hta_submesh *he = &h.mesh.submeshes[h.elem[h.health_meter].submesh];

        hta_hud_set_shield(&h, 1.0f);
        hta_hud_set_health(&h, 1.0f);
        CHECK(sh->meter == 1.0f, "a full shield fills its bar");
        CHECK(fabsf(sh->tint[2] - h.shield_max[2]) < 1e-5f,
              "and takes the tag's full colour");

        hta_hud_set_shield(&h, 0.0f);
        CHECK(sh->meter == 0.0f, "an empty shield empties it");
        CHECK(fabsf(sh->tint[2] - h.shield_min[2]) < 1e-5f,
              "and takes the tag's empty colour");

        hta_hud_set_health(&h, 0.5f);
        float want = h.health_min[0] + (h.health_max[0] - h.health_min[0]) * 0.5f;
        CHECK(fabsf(he->tint[0] - want) < 1e-5f,
              "half health lands halfway between the two colours");
        CHECK(he->tint[3] > 0.99f, "and stays opaque");

        /* Out-of-range values must clamp, not wrap or read past the bar. */
        hta_hud_set_shield(&h, 5.0f);
        CHECK(sh->meter == 1.0f, "an over-full shield clamps");
        hta_hud_set_shield(&h, -3.0f);
        CHECK(sh->meter == 0.0f, "a negative shield clamps");

        /* The crosshair is not a meter and must not be treated as one. */
        CHECK(h.mesh.submeshes[h.elem[h.cross_elem].submesh].meter < 0.0f,
              "the crosshair is not a meter");

        /* Everything must land on screen at a phone's aspect. */
        hta_hud_layout(&h, 2340, 1080);
        int on_screen = 1;
        for (uint32_t i = 0; i < h.elem_count; i++)
            for (uint32_t k = 0; k < 4; k++) {
                const float *p = h.mesh.vertices[h.elem[i].vertex + k].pos;
                /* The shield plate is allowed to bleed a little past the
                 * corner, which is what it does in Halo. */
                if (p[0] < -1.05f || p[0] > 1.05f) on_screen = 0;
                if (p[1] < -1.05f || p[1] > 1.05f) on_screen = 0;
            }
        CHECK(on_screen, "every element lands on screen");
    }

    hta_hud_free(&h);
    CHECK(h.mesh.vertices == NULL, "free clears the mesh");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
