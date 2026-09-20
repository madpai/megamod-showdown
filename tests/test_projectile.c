/* Rounds you can watch fly. The synthetic half needs no map; pass
 * bloodgulch.map to check the numbers against the Trial's own tags. */
#include "engine/projectile.h"
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

static const char *leaf(const char *path)
{
    const char *s = strrchr(path, '\\');
    return s ? s + 1 : path;
}

int main(int argc, char **argv)
{
    printf("projectiles\n");

    printf("\n[nothing loaded]\n");
    {
        /* Most of the roster's rounds are particles. Every entry point has
         * to be a no-op rather than a crash for those. */
        hta_projectiles p;
        hta_projectiles_init(&p);
        CHECK(!p.loaded, "an unequipped launcher is not loaded");
        float o[3] = { 0, 0, 0 }, d[3] = { 1, 0, 0 };
        hta_projectiles_fire(&p, o, d);
        hta_projectiles_update(&p, NULL, 1.0f / 60.0f);
        CHECK(hta_projectiles_count(&p) == 0, "firing one changes nothing");
        CHECK(!p.detonated, "and nothing detonates");
        hta_projectiles_free(&p);
        hta_projectiles_free(&p);      /* twice must be safe */
        CHECK(1, "freeing twice is safe");
    }

    if (argc < 2) {
        printf("\n  skip: no map path (pass bloodgulch.map for the tag checks)\n");
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

    uint32_t ids[32];
    uint32_t count = hta_weapon_list_playable(&c, ids, 32);

    printf("\n[which rounds are objects]\n");
    /* Halo fires everything as a projectile, but only two of the carried
     * weapons give theirs a model. The rest are particles and stay hitscan. */
    int drawable = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        hta_projectiles p;
        hta_projectiles_init(&p);
        bool ok = hta_projectiles_equip(&p, &c, NULL, &w, err, sizeof(err));
        if (ok) {
            printf("  %-16s %u verts, %.1f -> %.1f wu/s, range %.0f, timer %.2f s\n",
                   leaf(w.path), p.verts_each, p.speed_initial, p.speed_final,
                   p.range, p.timer);
            CHECK(p.verts_each > 0, "the model has geometry");
            CHECK(p.mesh.vertex_count == p.verts_each * HTA_PROJ_MAX,
                  "one copy per slot");
            CHECK(p.mesh.textures != NULL, "and a texture table to intern into");
            drawable++;
        }
        hta_projectiles_free(&p);
    }
    CHECK(drawable == 2, "the rocket and the needle, and nothing else");

    printf("\n[the rocket flies]\n");
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        if (!strstr(w.path, "rocket")) continue;
        hta_projectiles p;
        hta_projectiles_init(&p);
        if (!hta_projectiles_equip(&p, &c, NULL, &w, err, sizeof(err))) {
            printf("  FAIL: %s\n", err); failures++; checks++; break;
        }
        /* proj+484 is world units per TICK. 0.4 at 30 ticks is 12 wu/s --
         * about 36 m/s, which is why a Halo rocket can be dodged. Reading
         * it as per-second would give a rocket you could walk past. */
        CHECK(fabsf(p.speed_initial - 12.0f) < 0.1f, "12 world units a second");
        CHECK(p.range > 100.0f, "and a long range");
        CHECK(p.timer == 0.0f, "a rocket has no self-detonation timer");

        float o[3] = { 0, 0, 0 }, d[3] = { 1, 0, 0 };
        hta_projectiles_fire(&p, o, d);
        CHECK(hta_projectiles_count(&p) == 1, "one in the air");

        for (int k = 0; k < 60; k++) hta_projectiles_update(&p, NULL, 1.0f / 60.0f);
        printf("    after a second: %.2f world units out\n", p.live[0].travelled);
        CHECK(fabsf(p.live[0].pos[0] - p.live[0].travelled) < 0.01f,
              "it went where it was pointed");
        CHECK(p.live[0].travelled > 11.0f && p.live[0].travelled < 12.5f,
              "about twelve world units in the first second");
        CHECK(fabsf(p.live[0].pos[2]) < 1e-5f, "and a rocket does not drop");

        /* The mesh has to follow it, or there is nothing to see. */
        uint32_t slot0 = 0;
        float far_from_origin = 0.0f;
        for (uint32_t v = 0; v < p.verts_each; v++) {
            float dx = p.mesh.vertices[slot0 * p.verts_each + v].pos[0];
            if (dx > far_from_origin) far_from_origin = dx;
        }
        CHECK(far_from_origin > 10.0f, "and the geometry moved with it");

        /* Idle slots must stay collapsed rather than sitting at the origin
         * as a visible lump. */
        float second[3] = { 0, 0, 0 };
        for (uint32_t v = 0; v < p.verts_each; v++)
            for (int k = 0; k < 3; k++)
                second[k] += fabsf(p.mesh.vertices[1 * p.verts_each + v].pos[k]);
        CHECK(second[0] == 0.0f && second[1] == 0.0f && second[2] == 0.0f,
              "an unused slot is collapsed to a point");

        /* Flying out of range retires it. */
        for (int k = 0; k < 60 * 30; k++) {
            hta_projectiles_update(&p, NULL, 1.0f / 60.0f);
            if (!hta_projectiles_count(&p)) break;
        }
        CHECK(hta_projectiles_count(&p) == 0, "it expires at maximum range");

        /* More rounds than slots must not overflow. */
        for (int k = 0; k < HTA_PROJ_MAX * 3; k++) hta_projectiles_fire(&p, o, d);
        CHECK(hta_projectiles_count(&p) == HTA_PROJ_MAX,
              "firing more than there are slots reuses them");
        hta_projectiles_free(&p);
        break;
    }

    printf("\n[the needle goes off by itself]\n");
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        if (!strstr(w.path, "needler")) continue;
        hta_projectiles p;
        hta_projectiles_init(&p);
        if (!hta_projectiles_equip(&p, &c, NULL, &w, err, sizeof(err))) break;
        CHECK(p.timer > 0.0f, "a needle carries a detonation timer");
        float o[3] = { 0, 0, 0 }, d[3] = { 1, 0, 0 };
        hta_projectiles_fire(&p, o, d);
        float t = 0.0f;
        for (int k = 0; k < 60 * 5; k++) {
            hta_projectiles_update(&p, NULL, 1.0f / 60.0f);
            t += 1.0f / 60.0f;
            if (!hta_projectiles_count(&p)) break;
        }
        printf("    gone after %.2f s (timer %.2f, range %.0f)\n", t, p.timer, p.range);
        CHECK(t < p.timer + 0.05f, "and goes off on it rather than flying forever");
        hta_projectiles_free(&p);
        break;
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
