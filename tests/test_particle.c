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
        hta_particles_update(&p, NULL, NULL, 1.0f / 60.0f);
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
        hta_particles_update(&p, NULL, NULL, 1.0f / 60.0f);
        float moved = 0.0f;
        for (uint32_t k = 0; k < HTA_PART_MAX; k++)
            if (p.live[k].alive)
                moved += fabsf(p.live[k].pos[0]) + fabsf(p.live[k].pos[1]);
        CHECK(moved > 0.0f, "and they travel");

        float t = 0.0f;
        for (int k = 0; k < 60 * 30; k++) {
            hta_particles_update(&p, NULL, NULL, 1.0f / 60.0f);
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

    printf("\n[every particle has its own physics]\n");
    {
        /* Each `part` names a `pphy`, and the difference is the whole
         * point: a spent casing falls at full gravity and bounces off the
         * world, muzzle smoke barely falls and drifts through it, and
         * plasma residue RISES. Gravity is signed. */
        int saw_falling = 0, saw_floating = 0, saw_rising = 0, saw_collides = 0;
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err)))
                continue;
            uint32_t np = hta_effect_particle_count(&c, w.firing_fx_id);
            for (uint32_t q = 0; q < np; q++) {
                hta_effect_particle ep;
                if (!hta_effect_particle_at(&c, w.firing_fx_id, q, &ep)) continue;
                if (ep.gravity < -0.5f) saw_falling++;
                else if (ep.gravity < 0.0f) saw_floating++;
                else if (ep.gravity > 0.0f) saw_rising++;
                if (ep.collides) saw_collides++;
            }
        }
        printf("  %d falling, %d floating, %d rising, %d colliding\n",
               saw_falling, saw_floating, saw_rising, saw_collides);
        CHECK(saw_falling > 0, "something falls at full gravity");
        CHECK(saw_floating > 0, "something barely falls");
        CHECK(saw_rising > 0, "and something rises");
        CHECK(saw_collides > 0, "and something collides with the world");

        /* And it has to reach the simulation, not just the tag. */
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err)))
                continue;
            if (!strstr(w.path, "assault rifle")) continue;
            hta_particles p;
            hta_particles_init(&p);
            uint32_t ej = hta_particles_add_marker(&p, &c, &bm, w.firing_fx_id,
                                                    "primary ejection");
            if (ej == HTA_PART_NO_RECIPE) break;
            hta_particles_build(&p, err, sizeof(err));
            float o[3] = { 0, 0, 10.0f }, side[3] = { 1, 0, 0 };
            hta_particles_burst(&p, ej, o, side);
            float start_z = 0.0f;
            int found = 0;
            for (uint32_t k = 0; k < HTA_PART_MAX; k++)
                if (p.live[k].alive && !found) { start_z = p.live[k].pos[2]; found = 1; }
            for (int k = 0; k < 30; k++)
                hta_particles_update(&p, NULL, NULL, 1.0f / 60.0f);
            float end_z = start_z;
            for (uint32_t k = 0; k < HTA_PART_MAX; k++)
                if (p.live[k].alive) { end_z = p.live[k].pos[2]; break; }
            printf("  a casing falls %.3f m in half a second\n", start_z - end_z);
            CHECK(start_z - end_z > 0.05f, "brass actually falls");
            hta_particles_free(&p);
            break;
        }
    }

    printf("\n[spent brass]\n");
    {
        /* A weapon's firing effect carries its muzzle flashes AND its
         * ejected casing. The flash is the viewmodel's job, so only the
         * particles on `primary ejection` are wanted -- otherwise every
         * shot would spray a second set of flashes into the world. */
        int brass = 0, none = 0;
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err)))
                continue;
            hta_particles p;
            hta_particles_init(&p);
            uint32_t all = hta_particles_add(&p, &c, &bm, w.firing_fx_id);
            uint32_t ej = hta_particles_add_marker(&p, &c, &bm, w.firing_fx_id,
                                                   "primary ejection");
            if (ej != HTA_PART_NO_RECIPE) {
                brass++;
                CHECK(ej != all, "the ejection set is its own recipe");
                CHECK(p.recipe[ej].emit_count <= p.recipe[all].emit_count,
                      "  and a subset of the whole effect");
            } else {
                none++;
            }
            hta_particles_free(&p);
        }
        printf("  %d weapon(s) eject brass, %d do not\n", brass, none);
        /* The human weapons do; the covenant ones have no brass to throw. */
        CHECK(brass >= 4, "the human weapons eject");
        CHECK(none >= 3, "and the covenant ones do not");
    }


    printf("\n[the flamethrower's jet]\n");
    {
        /* A continuous weapon does not fire a burst: the flamethrower's
         * flame is a `pctl` particle system attached to its `spawn fire`
         * marker, emitted at a tagged rate for as long as the trigger is
         * held. Exactly one weapon in the Trial has one. */
        int systems = 0;
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err)))
                continue;
            uint32_t pctl = hta_object_attachment(&c, ids[i], "spawn fire",
                                                  HTA_FOURCC('p','c','t','l'));
            if (!pctl) continue;
            systems++;

            hta_particles p;
            hta_particles_init(&p);
            uint32_t jet = hta_particles_add_system(&p, &c, &bm, pctl, 3.0f);
            CHECK(jet != HTA_PART_NO_RECIPE, "the system builds an emitter");
            if (jet == HTA_PART_NO_RECIPE) { hta_particles_free(&p); continue; }
            CHECK(hta_particles_build(&p, err, sizeof(err)),
                  "  and geometry to draw it with");

            const hta_particle_emit *e = &p.recipe[jet].emit[0];
            printf("  %s: %.0f/s, radius %.3f..%.3f, %.2f s alive\n",
                   strrchr(w.path, '\\') + 1, (double)p.recipe[jet].rate,
                   (double)e->radius_min, (double)e->radius_max,
                   (double)e->life);
            CHECK(p.recipe[jet].rate > 1.0f, "  at a rate worth emitting");
            CHECK(e->life > 0.0f, "  with a lifespan");
            CHECK(e->radius_max >= e->radius_min, "  and a sane radius range");
            CHECK(p.type[e->type].blend == HTA_FX_BLEND_ADD,
                  "  burning, so additive");

            /* Emitting is rate-based rather than per-shot: the trigger
             * puts out about `rate` particles a second, and nothing at
             * all before any time has passed. */
            float o[3] = {0,0,0}, dir[3] = {1,0,0};
            CHECK(hta_particles_count(&p) == 0, "  nothing before the trigger");
            hta_particles_emit(&p, jet, o, dir, 0.0f);
            CHECK(hta_particles_count(&p) == 0, "  and nothing in no time at all");

            for (int k = 0; k < 30; k++) {
                hta_particles_emit(&p, jet, o, dir, 1.0f / 60.0f);
                hta_particles_update(&p, NULL, NULL, 1.0f / 60.0f);
            }
            uint32_t half = hta_particles_count(&p);
            float expect = p.recipe[jet].rate * 0.5f;
            printf("  half a second of trigger: %u alive (the tag says ~%.0f)\n",
                   half, (double)expect);
            CHECK(half > 0, "  the trigger sprays");
            CHECK((float)half <= expect + 2.0f, "  at the rate the tag asks for");

            /* And it goes where it is pointed, rather than where gravity
             * would take it. */
            float reach = 0.0f;
            for (uint32_t k = 0; k < HTA_PART_MAX; k++)
                if (p.live[k].alive && p.live[k].pos[0] > reach)
                    reach = p.live[k].pos[0];
            printf("  the jet reaches %.2f wu\n", (double)reach);
            CHECK(reach > 0.3f, "  the flame travels down the barrel line");

            /* A dropped frame must not dump a second of flame at once. */
            hta_particles_emit(&p, jet, o, dir, 10.0f);
            CHECK(hta_particles_count(&p) <= HTA_PART_PER_TYPE,
                  "  a long frame cannot flood the pool");

            hta_particles_free(&p);
        }
        printf("  %d weapon(s) spray a particle system\n", systems);
        CHECK(systems == 1, "exactly one of them: the flamethrower");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
