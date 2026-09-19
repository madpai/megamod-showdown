/* Magazine state. The synthetic half runs anywhere; pass bloodgulch.map to
 * also check the numbers against the Trial's own assault rifle tag. */
#include "engine/ammo.h"
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

static hta_weapon_def fake(int mag, int reserve_max, int initial,
                           int per_shot, int per_reload, float reload)
{
    hta_weapon_def w;
    memset(&w, 0, sizeof(w));
    w.rounds_loaded_max = mag;
    w.rounds_reserve_max = reserve_max;
    w.rounds_initial = initial;
    w.rounds_per_shot = per_shot;
    w.rounds_reloaded = per_reload;
    w.reload_time = reload;
    return w;
}

int main(int argc, char **argv)
{
    printf("ammo\n");

    printf("\n[spawn]\n");
    {
        /* Halo's "rounds total initial" includes the loaded magazine. */
        hta_weapon_def w = fake(60, 180, 240, 1, 60, 3.4f);
        hta_ammo a;
        hta_ammo_init(&a, &w);
        CHECK(a.loaded == 60, "spawns with a full magazine");
        CHECK(a.reserve == 180, "and the rest of the initial total in reserve");

        /* Less initial ammo than a magazine holds must not go negative. */
        hta_weapon_def small = fake(60, 180, 25, 1, 60, 3.4f);
        hta_ammo b;
        hta_ammo_init(&b, &small);
        CHECK(b.loaded == 25 && b.reserve == 0,
              "an initial total under one magazine loads what there is");

        /* A tag with no rounds-per-reload must still be able to reload. */
        hta_weapon_def norel = fake(60, 180, 240, 1, 0, 3.4f);
        hta_ammo cc;
        hta_ammo_init(&cc, &norel);
        CHECK(cc.per_reload == 60, "a zero rounds-reloaded falls back to a full magazine");
    }

    printf("\n[firing]\n");
    {
        hta_weapon_def w = fake(3, 180, 6, 1, 3, 1.0f);
        hta_ammo a;
        hta_ammo_init(&a, &w);
        CHECK(a.loaded == 3 && a.reserve == 3, "3 loaded, 3 spare");

        CHECK(hta_ammo_shoot(&a) && a.loaded == 2, "a shot spends a round");
        CHECK(a.spent && !a.dry, "and reports itself as spent");
        hta_ammo_shoot(&a);
        hta_ammo_shoot(&a);
        CHECK(a.loaded == 0, "the magazine empties");

        CHECK(!hta_ammo_shoot(&a), "firing empty does not fire");
        CHECK(a.dry && !a.spent, "it reports dry instead");
        CHECK(a.loaded == 0, "and does not go negative");
    }

    printf("\n[reloading]\n");
    {
        hta_weapon_def w = fake(10, 100, 25, 1, 10, 2.0f);
        hta_ammo a;
        hta_ammo_init(&a, &w);
        CHECK(a.loaded == 10 && a.reserve == 15, "10 loaded, 15 spare");

        CHECK(!hta_ammo_reload(&a), "reloading a full magazine does nothing");

        for (int i = 0; i < 6; i++) hta_ammo_shoot(&a);
        CHECK(a.loaded == 4, "six shots later");
        CHECK(hta_ammo_reload(&a) && a.reload_began, "a partial magazine reloads");
        CHECK(a.phase == HTA_AMMO_RELOADING, "and enters the reloading phase");
        CHECK(!hta_ammo_reload(&a), "a second reload while reloading is refused");

        /* Nothing may fire mid-reload, and the count must not change yet. */
        CHECK(!hta_ammo_shoot(&a), "firing mid-reload is refused");
        CHECK(!a.dry, "and that is not a dry fire");
        CHECK(a.loaded == 4, "the magazine does not fill early");

        hta_ammo_update(&a, 1.0f);
        CHECK(a.phase == HTA_AMMO_RELOADING, "still reloading halfway through");
        CHECK(fabsf(hta_ammo_reload_progress(&a) - 0.5f) < 0.01f,
              "progress reads half");
        CHECK(!a.reload_done, "and has not reported completion");

        hta_ammo_update(&a, 1.01f);
        CHECK(a.phase == HTA_AMMO_READY, "the reload completes");
        CHECK(a.reload_done, "and says so exactly once");
        CHECK(a.loaded == 10, "the magazine is full again");
        CHECK(a.reserve == 9, "and the reserve paid for exactly the 6 rounds used");

        hta_ammo_update(&a, 0.1f);
        CHECK(!a.reload_done, "the completion event does not repeat");
        CHECK(hta_ammo_shoot(&a), "firing works again afterwards");
    }

    printf("\n[running dry]\n");
    {
        hta_weapon_def w = fake(5, 100, 8, 1, 5, 1.0f);
        hta_ammo a;
        hta_ammo_init(&a, &w);   /* 5 loaded, 3 spare */

        for (int i = 0; i < 5; i++) hta_ammo_shoot(&a);
        CHECK(hta_ammo_reload(&a), "reload with a partial reserve starts");
        hta_ammo_update(&a, 1.1f);
        CHECK(a.loaded == 3 && a.reserve == 0,
              "a short reserve loads what is left, not a full magazine");

        for (int i = 0; i < 3; i++) hta_ammo_shoot(&a);
        CHECK(a.loaded == 0, "and then there is nothing");
        CHECK(!hta_ammo_reload(&a), "reloading with an empty reserve is refused");
        CHECK(!hta_ammo_shoot(&a) && a.dry, "the weapon just clicks");
    }

    printf("\n[multi-round shots]\n");
    {
        /* A shotgun-style weapon spending more than one round per pull. */
        hta_weapon_def w = fake(6, 60, 12, 2, 6, 1.0f);
        hta_ammo a;
        hta_ammo_init(&a, &w);
        CHECK(hta_ammo_shoot(&a) && a.loaded == 4, "a shot spends rounds-per-shot");
        hta_ammo_shoot(&a);
        CHECK(a.loaded == 2, "twice");
        hta_ammo_shoot(&a);
        CHECK(a.loaded == 0, "three times empties it");
        CHECK(!hta_ammo_shoot(&a) && a.dry, "and a fourth is dry");

        /* Fewer rounds left than one shot needs is still dry, not a partial. */
        hta_ammo b;
        hta_ammo_init(&b, &w);
        b.loaded = 1;
        CHECK(!hta_ammo_shoot(&b) && b.dry, "one round left cannot pay for a 2-round shot");
        CHECK(b.loaded == 1, "and the round is not consumed");
    }

    if (argc < 2) {
        printf("\n  skip: no map path (pass bloodgulch.map for the tag check)\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    printf("\n[the Trial's own assault rifle]\n");
    {
        size_t sz = 0;
        uint8_t *data = slurp(argv[1], &sz);
        CHECK(data != NULL, "map reads");
        if (!data) return 1;
        hta_cache c;
        char err[HTA_ERRLEN] = {0};
        CHECK(hta_cache_open(&c, data, sz, err, sizeof(err)), "cache opens");
        hta_weapon_def w;
        memset(&w, 0, sizeof(w));
        CHECK(hta_weapon_load_default(&c, NULL, &w, NULL, err, sizeof(err)),
              "weapon loads");
        hta_ammo a;
        hta_ammo_init(&a, &w);
        printf("    %s: %d loaded / %d reserve, %d per reload, %.2f s, chamber %.2f s\n",
               w.path, a.loaded, a.reserve, a.per_reload, a.reload_time, a.chamber_time);
        CHECK(a.mag_max == 60, "the AR magazine is 60 rounds");
        CHECK(a.loaded == 60, "you spawn with it full");
        CHECK(a.reserve == 180, "and 180 spare, which is Halo's 60+180");
        CHECK(a.reload_time > 2.0f && a.reload_time < 5.0f,
              "the tagged reload time is plausible");

        /* Empty it and reload with real numbers. */
        int shots = 0;
        while (hta_ammo_shoot(&a)) shots++;
        CHECK(shots == 60, "sixty shots empty the magazine");
        CHECK(hta_ammo_reload(&a), "and then it reloads");
        hta_ammo_update(&a, a.reload_time + 0.01f);
        CHECK(a.loaded == 60 && a.reserve == 120, "60 back, 120 left");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
