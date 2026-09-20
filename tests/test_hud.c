/* The screen HUD built from Halo's own weapon HUD interface tag.
 * Needs a real cache: pass bloodgulch.map; bitmaps.map is read beside it. */
#include "engine/hud.h"
#include "asset/font.h"
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
        /* Halo scales its HUD by height; we then enlarge it for a phone. */
        float want = h.cross_px * (float)H / HTA_HUD_CANVAS_H * HTA_HUD_PHONE_SCALE;
        CHECK(fabsf(px_w - want) < 1.0f, "scaled by screen height, as Halo does");
        CHECK(HTA_HUD_PHONE_SCALE >= 1.0f && HTA_HUD_PHONE_SCALE <= 3.0f,
              "the phone enlargement is a sane multiple");
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

    printf("\n[weapon ammo block]\n");
    CHECK(h.have_ammo, "the weapon's wphi yields an ammo meter");
    if (h.have_ammo) {
        hta_hud_elem *ae = &h.elem[h.ammo_meter];
        hta_submesh *am = &h.mesh.submeshes[ae->submesh];
        printf("    pip grid %.0fx%.0f native, anchor %u, extra scale %.2f\n",
               ae->w_px, ae->h_px, ae->anchor, ae->extra_scale);
        /* Halo's assault rifle shows its magazine as a grid of pips, one per
         * round, so the sprite has to be wide enough to hold sixty. */
        CHECK(ae->w_px > 100.0f, "the pip grid is a wide sprite, not a bar");
        CHECK(ae->anchor == HTA_HUD_ANCHOR_TOP_LEFT, "anchored top left");
        CHECK(ae->extra_scale < 1.0f, "drawn at the measured weapon-HUD scale");

        /* The plate and outline come from the child HUD chain and must be
         * behind the pips. */
        CHECK(h.elem_count >= 7, "the child hud contributed its plate too");
        CHECK(ae->submesh > 0, "something is drawn before the pips");

        hta_hud_set_ammo(&h, 1.0f);
        CHECK(am->meter == 1.0f, "a full magazine fills the grid");
        hta_hud_set_ammo(&h, 0.0f);
        CHECK(am->meter == 0.0f, "an empty one empties it");
        hta_hud_set_ammo(&h, 0.5f);
        CHECK(fabsf(am->meter - 0.5f) < 1e-5f, "and half fills half of it");
        hta_hud_set_ammo(&h, 9.0f);
        CHECK(am->meter == 1.0f, "an over-full magazine clamps");
    }

    hta_hud_free(&h);
    CHECK(h.mesh.vertices == NULL, "free clears the mesh");

    /* Every playable weapon has to bring its own everything. This is what
     * proves none of the viewmodel, HUD or weapon work was ever wired to
     * the assault rifle in particular. */
    printf("\n[every playable weapon]\n");
    {
        uint32_t ids[32];
        uint32_t n = hta_weapon_list_playable(&c, ids, 32);
        printf("    %u playable weapon(s)\n", n);
        CHECK(n >= 8, "the cache has a roster, not just one gun");

        uint32_t with_hud = 0, with_cross = 0, with_ammo = 0, loaded = 0;
        uint32_t cross_sizes[32];
        uint32_t ncross = 0;
        for (uint32_t i = 0; i < n; i++) {
            hta_weapon_def wd;
            if (!hta_weapon_load_id(&c, &bm, ids[i], &wd, NULL, err, sizeof(err)))
                continue;
            loaded++;
            hta_hud wh;
            if (!hta_hud_load(&wh, &c, &bm, &wd, err, sizeof(err))) continue;
            if (wh.elem_count) with_hud++;
            if (wh.have_cross) {
                with_cross++;
                if (ncross < 32) cross_sizes[ncross++] = (uint32_t)wh.cross_px;
            }
            if (wh.have_ammo) with_ammo++;
            printf("      %-40s %2u elem  cross %-3s %3.0fpx  ammo %s\n",
                   wd.path, wh.elem_count, wh.have_cross ? "yes" : "no",
                   wh.cross_px, wh.have_ammo ? "yes" : "no");
            hta_hud_free(&wh);
        }
        CHECK(loaded == n, "every listed weapon loads");
        CHECK(with_hud == n, "and every one builds a HUD");

        /* A weapon names its own HUD interface. Finding it by matching tag
         * paths silently loses the rocket launcher, whose wphi is
         * "rocket_launcher", and the flamethrower's "flame thrower". */
        /* All of them, including the rocket launcher and flamethrower whose
         * wphi tags are named differently from their weapons. */
        CHECK(with_cross == n, "every one of them has its own crosshair");
        CHECK(with_ammo == n, "and its own ammo display");

        /* And the crosshairs are not all the same: the pistol's is small
         * and the sniper's smaller still. */
        int varied = 0;
        for (uint32_t i = 1; i < ncross; i++)
            if (cross_sizes[i] != cross_sizes[0]) varied = 1;
        CHECK(varied, "different weapons get different reticles");
        printf("    %u with a crosshair, %u with an ammo display\n",
               with_cross, with_ammo);
    }

    printf("\n[the ammo pips deplete]\n");
    {
        /* Halo does not DROP the part of a meter the fill has not reached:
         * it paints it in the element's empty colour. The assault rifle
         * needs that, because its meter minimum and maximum colours are the
         * same blue -- the only thing telling a live pip from a spent one is
         * that empty colour. */
        uint32_t ids[32];
        uint32_t n = hta_weapon_list_playable(&c, ids, 32);
        int checked = 0;
        for (uint32_t i = 0; i < n; i++) {
            hta_weapon_def wd;
            if (!hta_weapon_load_id(&c, &bm, ids[i], &wd, NULL, err, sizeof(err)))
                continue;
            if (!strstr(wd.path, "assault rifle")) continue;
            hta_hud wh;
            memset(&wh, 0, sizeof(wh));
            if (!hta_hud_load(&wh, &c, &bm, &wd, err, sizeof(err))) continue;
            CHECK(wh.ammo_meter >= 0, "the rifle has an ammo meter");
            if (wh.ammo_meter >= 0) {
                const hta_submesh *sm =
                    &wh.mesh.submeshes[wh.elem[wh.ammo_meter].submesh];
                printf("  empty colour %.2f %.2f %.2f (a %.2f)\n",
                       sm->empty[0], sm->empty[1], sm->empty[2], sm->empty[3]);
                CHECK(sm->empty[3] > 0.0f, "and an empty colour to draw spent pips in");

                /* The full and empty colours must actually differ, or the
                 * grid cannot show anything. */
                float d = 0.0f;
                for (int k = 0; k < 3; k++) {
                    float q = sm->empty[k] - wh.ammo_max[k];
                    d += q * q;
                }
                CHECK(d > 0.01f, "which is a different colour from a live pip");

                hta_hud_set_ammo(&wh, 1.0f);
                CHECK(sm->meter > 0.99f, "a full magazine fills the meter");
                hta_hud_set_ammo(&wh, 0.0f);
                CHECK(sm->meter < 0.01f, "and an empty one empties it");
                hta_hud_set_ammo(&wh, 0.5f);
                CHECK(sm->meter > 0.49f && sm->meter < 0.51f, "half is half");
                checked = 1;
            }
            hta_hud_free(&wh);
            break;
        }
        CHECK(checked, "the rifle is in the roster");
    }

    printf("\n[the rounds counter]\n");
    {
        /* Halo draws HUD numbers with the hud_globals FONT -- there is no
         * digit bitmap anywhere in the weapon's tag. And the number
         * elements are on the weapon HUD's CHILD, not on the weapon's own
         * tag, which is why they have to be looked for down the chain. */
        uint32_t font = hta_hud_font(&c);
        CHECK(font != 0, "hud_globals names a fullscreen font");
        hta_font_digits dg;
        memset(&dg, 0, sizeof(dg));
        CHECK(hta_font_digits_load(&c, font, &dg, err, sizeof(err)),
              "and its ten digits load");
        if (dg.loaded) {
            printf("  digit atlas %ux%u, ascent %d\n",
                   dg.atlas_w, dg.atlas_h, dg.ascent);
            int sane = 1;
            for (int d = 0; d < 10; d++)
                if (!dg.digit[d].w || !dg.digit[d].h ||
                    dg.digit[d].u1 <= dg.digit[d].u0) sane = 0;
            CHECK(sane, "every digit has pixels and a place in the atlas");
            /* An all-zero alpha atlas would render nothing at all. */
            uint32_t lit = 0;
            for (size_t i = 3; i < (size_t)dg.atlas_w * dg.atlas_h * 4u; i += 4)
                if (dg.rgba[i] > 128) lit++;
            printf("  %u lit texels of %u\n", lit,
                   (unsigned)(dg.atlas_w * dg.atlas_h));
            CHECK(lit > 100, "and the glyphs actually have ink in them");
        }
        hta_font_digits_free(&dg);

        uint32_t ids[32];
        uint32_t n = hta_weapon_list_playable(&c, ids, 32);
        int with_numbers = 0;
        for (uint32_t i = 0; i < n; i++) {
            hta_weapon_def wd;
            if (!hta_weapon_load_id(&c, &bm, ids[i], &wd, NULL, err, sizeof(err)))
                continue;
            hta_hud wh;
            memset(&wh, 0, sizeof(wh));
            if (!hta_hud_load(&wh, &c, &bm, &wd, err, sizeof(err))) continue;
            if (wh.number_count) {
                with_numbers++;
                if (!strstr(wd.path, "assault rifle")) { hta_hud_free(&wh); continue; }
                CHECK(wh.number_count == 3, "the rifle's counter has three digits");

                /* 240 and 036: the tag asks for leading zeros, which is how
                 * the real HUD shows them. */
                hta_hud_set_number(&wh, 240);
                hta_hud_layout(&wh, 1920, 1080);
                int shown = 0;
                for (uint32_t k = 0; k < wh.number_count; k++)
                    if (wh.elem[wh.number_elem[k]].w_px > 0.0f) shown++;
                CHECK(shown == 3, "240 draws three digits");
                hta_hud_set_number(&wh, 36);
                shown = 0;
                for (uint32_t k = 0; k < wh.number_count; k++)
                    if (wh.elem[wh.number_elem[k]].w_px > 0.0f) shown++;
                CHECK(shown == 3, "and 36 still draws three, zero-padded");

                /* Digits must not overlap or drift apart. */
                hta_hud_set_number(&wh, 888);
                float x0 = wh.elem[wh.number_elem[0]].offset[0];
                float x1 = wh.elem[wh.number_elem[1]].offset[0];
                float x2 = wh.elem[wh.number_elem[2]].offset[0];
                printf("  888 sits at x %.1f %.1f %.1f\n", x0, x1, x2);
                CHECK(x1 > x0 && x2 > x1, "digits run left to right");
                CHECK((x2 - x1) - (x1 - x0) < 0.01f &&
                      (x1 - x0) - (x2 - x1) < 0.01f,
                      "and evenly, for one repeated digit");
            }
            hta_hud_free(&wh);
        }
        CHECK(with_numbers > 0, "the roster has counters");
    }

    printf("\n[the sniper's scope]\n");
    {
        /* Everything in the weapon's HUD tag flagged "show only when
         * zoomed": the reticle ticks and the magnification label. They are
         * grouped per zoom level, and only the level in use is drawn. */
        uint32_t ids[32];
        uint32_t n = hta_weapon_list_playable(&c, ids, 32);
        int found = 0;
        for (uint32_t i = 0; i < n; i++) {
            hta_weapon_def wd;
            if (!hta_weapon_load_id(&c, &bm, ids[i], &wd, NULL, err, sizeof(err)))
                continue;
            hta_hud wh;
            memset(&wh, 0, sizeof(wh));
            if (!hta_hud_load(&wh, &c, &bm, &wd, err, sizeof(err))) continue;

            int scoped = 0, lvl1 = 0, lvl2 = 0;
            for (uint32_t k = 0; k < wh.elem_count; k++) {
                if (!wh.elem[k].zoom_level) continue;
                scoped++;
                if (wh.elem[k].zoom_level == 1) lvl1++;
                if (wh.elem[k].zoom_level == 2) lvl2++;
            }
            if (strstr(wd.path, "sniper")) {
                found = 1;
                printf("  sniper: %u elements, %d scope (%d at 2x, %d at 8x)\n",
                       wh.elem_count, scoped, lvl1, lvl2);
                CHECK(scoped > 0, "the sniper has scope furniture");
                CHECK(lvl1 > 0 && lvl2 > 0, "and a set for each of its two levels");
                CHECK(wd.zoom_levels == 2, "which is how many levels it has");

                /* Hidden means collapsed to a point, not left on screen. */
                hta_hud_set_zoom(&wh, 0);
                hta_hud_layout(&wh, 1920, 1080);
                int shown = 0;
                for (uint32_t k = 0; k < wh.elem_count; k++) {
                    if (!wh.elem[k].zoom_level) continue;
                    const hta_vertex *v = &wh.mesh.vertices[wh.elem[k].vertex];
                    if (v[0].pos[0] != v[2].pos[0] || v[0].pos[1] != v[2].pos[1])
                        shown++;
                }
                CHECK(shown == 0, "unzoomed draws none of it");

                hta_hud_set_zoom(&wh, 1);
                hta_hud_layout(&wh, 1920, 1080);
                int at1 = 0, wrong = 0;
                for (uint32_t k = 0; k < wh.elem_count; k++) {
                    if (!wh.elem[k].zoom_level) continue;
                    const hta_vertex *v = &wh.mesh.vertices[wh.elem[k].vertex];
                    bool drawn = v[0].pos[0] != v[2].pos[0] || v[0].pos[1] != v[2].pos[1];
                    if (!drawn) continue;
                    if (wh.elem[k].zoom_level == 1) at1++; else wrong++;
                }
                CHECK(at1 == lvl1, "2x draws its own set");
                CHECK(wrong == 0, "and nothing from the other level");
            } else {
                /* Only a weapon that zooms has any. */
                if (wd.zoom_levels == 0)
                    CHECK(scoped == 0, "a weapon that cannot zoom has no scope");
            }
            hta_hud_free(&wh);
        }
        CHECK(found, "the sniper is in the roster");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
