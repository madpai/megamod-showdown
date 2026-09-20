/* The smoke and fire an effect throws out. Needs the Trial's own map:
 * every number here is a tag's. */
#include "engine/particle.h"
#include "engine/projectile.h"
#include "asset/cache.h"
#include "asset/weapon.h"
#include "asset/effect.h"
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
    printf("particles\n");

    printf("\n[nothing loaded]\n");
    {
        hta_particles p;
        hta_particles_init(&p);
        CHECK(!p.loaded, "an empty system is not loaded");
        float o[3] = {0,0,0}, d[3] = {0,0,1};
        hta_particles_burst(&p, 0, o, d);
        hta_particles_update(&p, NULL, 1.0f / 60.0f);
        CHECK(hta_particles_count(&p) == 0, "bursting one changes nothing");
        hta_particles_free(&p);
        hta_particles_free(&p);
        CHECK(1, "freeing twice is safe");
    }

    if (argc < 2) {
        printf("\n  skip: pass bloodgulch.map\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }
    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) {
        printf("  FAIL: %s\n", err); return 1;
    }
    char bmp[512];
    snprintf(bmp, sizeof(bmp), "%s", argv[1]);
    char *slash = strrchr(bmp, '/');
    if (slash) snprintf(slash + 1, sizeof(bmp) - (size_t)(slash + 1 - bmp), "bitmaps.map");
    size_t bsz = 0;
    uint8_t *bdata = slurp(bmp, &bsz);
    hta_resource_map bm;
    int have_bitmaps = bdata && hta_resource_open(&bm, bdata, bsz, err, sizeof(err));
    if (!have_bitmaps) {
        printf("  skip: needs bitmaps.map beside the cache\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    uint32_t ids[32];
    uint32_t count = hta_weapon_list_playable(&c, ids, 32);

    printf("\n[the rocket's explosion]\n");
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        if (!strstr(w.path, "rocket")) continue;

        hta_projectiles proj;
        hta_projectiles_init(&proj);
        CHECK(hta_projectiles_equip(&proj, &c, &bm, &w, err, sizeof(err)),
              "the rocket loads");
        CHECK(proj.det_effect != 0, "and names a detonation effect");

        hta_particles p;
        hta_particles_init(&p);
        uint32_t det = hta_particles_add(&p, &c, &bm, proj.det_effect);
        CHECK(det != HTA_PART_NO_RECIPE, "whose particles load");
        CHECK(hta_particles_build(&p, err, sizeof(err)), "and the mesh builds");
        printf("    %u type(s), %u slots\n", p.type_count,
               p.mesh.vertex_count / 4u);
        CHECK(p.type_count >= 2, "a flare and a smoke plume at least");
        CHECK(p.recipe_count == 1, "one recipe for one effect");
        CHECK(p.mesh.submesh_count == p.type_count, "one submesh per type");
        CHECK(p.mesh.vertex_count == p.type_count * HTA_PART_PER_TYPE * 4u,
              "and a fixed slot per particle");

        /* Art with no alpha channel cannot be alpha-blended: it would draw
         * as a square of its own black background. The rocket's lens flare
         * is exactly that, and has to come out additive. */
        int no_alpha_is_add = 1;
        for (uint32_t t = 0; t < p.type_count; t++) {
            if (p.type[t].blend == HTA_FX_BLEND_ADD) continue;
            const hta_bsp_texture *bt = &p.mesh.textures[p.type[t].tex];
            int has_alpha = 0;
            for (size_t q = 0; bt->rgba && q < (size_t)bt->width * bt->height; q++)
                if (bt->rgba[q * 4u + 3u] < 250u) { has_alpha = 1; break; }
            if (!has_alpha) no_alpha_is_add = 0;
        }
        CHECK(no_alpha_is_add, "nothing without alpha is left alpha-blended");

        float o[3] = { 0, 0, 0 }, up[3] = { 0, 0, 1 };
        hta_particles_burst(&p, det, o, up);
        uint32_t alive = hta_particles_count(&p);
        printf("    burst -> %u alive\n", alive);
        CHECK(alive > 0, "a burst throws particles");
        CHECK(alive <= p.type_count * HTA_PART_PER_TYPE, "and never past its slots");

        /* They must move, and they must all die. */
        hta_particles_update(&p, NULL, 1.0f / 60.0f);
        float moved = 0.0f;
        for (uint32_t k = 0; k < HTA_PART_MAX; k++)
            if (p.live[k].alive)
                moved += fabsf(p.live[k].pos[0]) + fabsf(p.live[k].pos[1]);
        CHECK(moved > 0.0f, "and they travel");

        float t = 0.0f;
        for (int k = 0; k < 60 * 30; k++) {
            hta_particles_update(&p, NULL, 1.0f / 60.0f);
            t += 1.0f / 60.0f;
            if (!hta_particles_count(&p)) break;
        }
        printf("    all gone after %.2f s\n", t);
        CHECK(hta_particles_count(&p) == 0, "every particle ages out");
        CHECK(t < 10.0f, "and none outlives its tagged life by much");

        /* Bursting far more than there is room for must not overflow. */
        for (int k = 0; k < 40; k++) hta_particles_burst(&p, det, o, up);
        CHECK(hta_particles_count(&p) <= p.type_count * HTA_PART_PER_TYPE,
              "repeated bursts stay inside the slots");

        hta_particles_free(&p);
        hta_projectiles_free(&proj);
        break;
    }

    printf("\n[impacts share their art]\n");
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        if (!strstr(w.path, "assault rifle")) continue;

        /* Blood Gulch is made of four materials. Adding an impact effect
         * per material is what makes bullets throw dust and sparks, and
         * they share their particle art heavily -- which is the point of
         * keeping types apart from recipes. */
        const uint8_t mats[4] = { 1u, 2u, 7u, 27u };   /* sand, stone, metal, plastic */
        hta_particles p;
        hta_particles_init(&p);
        uint32_t recipes = 0;
        for (int m = 0; m < 4; m++) {
            uint32_t fx = hta_projectile_response_effect(&c, w.projectile_id, mats[m]);
            if (!fx) continue;
            if (hta_particles_add(&p, &c, &bm, fx) != HTA_PART_NO_RECIPE) recipes++;
        }
        CHECK(hta_particles_build(&p, err, sizeof(err)), "the impact set builds");
        printf("    %u recipe(s) over %u shared type(s)\n", recipes, p.type_count);
        CHECK(recipes >= 3, "the map's materials each get one");
        CHECK(p.type_count <= HTA_PART_TYPES, "within the type budget");
        CHECK(p.type_count < recipes * HTA_PART_EMITS,
              "and they share art rather than each bringing its own");

        /* Every recipe must actually throw something. */
        float o[3] = { 0, 0, 0 }, up[3] = { 0, 0, 1 };
        int all_throw = 1;
        for (uint32_t r = 0; r < recipes; r++) {
            for (uint32_t k = 0; k < HTA_PART_MAX; k++) p.live[k].alive = false;
            hta_particles_burst(&p, r, o, up);
            if (!hta_particles_count(&p)) all_throw = 0;
        }
        CHECK(all_throw, "and every one of them throws particles");

        /* An unknown recipe must be ignored, not read off the end. */
        for (uint32_t k = 0; k < HTA_PART_MAX; k++) p.live[k].alive = false;
        hta_particles_burst(&p, 99u, o, up);
        CHECK(hta_particles_count(&p) == 0, "an unknown recipe throws nothing");

        hta_particles_free(&p);
        break;
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
