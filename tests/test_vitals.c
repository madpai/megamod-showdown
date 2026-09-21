/* Health, shield and falling. Pass bloodgulch.map to check the numbers
 * against the Trial's own cyborg. */
#include "engine/vitals.h"
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/dialogue.h"
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
    printf("vitals\n");

    printf("\n[the shield takes it first]\n");
    {
        hta_vitals v;
        memset(&v, 0, sizeof(v));
        v.max_health = 75.0f; v.max_shield = 75.0f;
        v.recharge_delay = 4.0f; v.recharge_rate = 0.3f;
        v.fall_harmful_min = 4.5f; v.fall_harmful_max = 6.3f; v.fall_fatal = 10.2f;
        v.loaded = true;
        hta_vitals_reset(&v);
        CHECK(v.health == 75.0f && v.shield == 75.0f, "full on a reset");

        hta_vitals_damage(&v, 30.0f);
        CHECK(v.shield == 45.0f && v.health == 75.0f,
              "damage comes off the shield, not the body");
        CHECK(v.took_damage, "and says so");

        hta_vitals_damage(&v, 60.0f);
        CHECK(v.shield == 0.0f, "a big hit empties the shield");
        CHECK(fabsf(v.health - 60.0f) < 0.01f, "and the rest reaches the body");
        CHECK(v.shield_broke, "the break is reported once");

        hta_vitals_update(&v, 0.1f);
        CHECK(!v.shield_broke, "and not again");

        /* No recharge until the delay is up, then it comes back. */
        hta_vitals_update(&v, 3.0f);
        CHECK(v.shield == 0.0f, "the shield waits out its delay");
        hta_vitals_update(&v, 1.5f);
        CHECK(v.shield > 0.0f, "then starts to return");
        for (int i = 0; i < 200; i++) hta_vitals_update(&v, 1.0f / 60.0f);
        CHECK(fabsf(v.shield - 75.0f) < 0.01f, "and fills up");
        CHECK(fabsf(v.health - 60.0f) < 0.01f, "the body does not heal itself");

        /* Damage restarts the delay. */
        hta_vitals_damage(&v, 10.0f);
        hta_vitals_update(&v, 1.0f);
        CHECK(v.shield < 70.0f, "being hit again stops the recharge");

        hta_vitals_damage(&v, 1000.0f);
        CHECK(v.died && v.health == 0.0f, "enough damage kills");
        float before = v.shield;
        hta_vitals_damage(&v, 10.0f);
        CHECK(v.shield == before, "and the dead take no more");
    }

    printf("\n[falling]\n");
    {
        hta_vitals v;
        memset(&v, 0, sizeof(v));
        v.max_health = 75.0f; v.max_shield = 75.0f;
        v.fall_harmful_min = 4.5f; v.fall_harmful_max = 6.3f; v.fall_fatal = 10.2f;
        v.loaded = true;
        hta_vitals_reset(&v);

        CHECK(hta_vitals_land(&v, 3.0f) == 0.0f, "a short drop is free");
        CHECK(v.shield == 75.0f, "and costs nothing");

        float mid = hta_vitals_land(&v, 7.0f);
        CHECK(mid > 0.0f, "a long one hurts");
        printf("    7.0 wu/s cost %.0f\n", mid);
        CHECK(v.shield < 75.0f, "off the shield first");

        hta_vitals_reset(&v);
        float fatal = hta_vitals_land(&v, 12.0f);
        printf("    12.0 wu/s cost %.0f\n", fatal);
        CHECK(v.died, "and far enough kills outright");

        /* Harder falls must never cost less. */
        int monotone = 1;
        float prev = -1.0f;
        for (float sp = 4.0f; sp <= 11.0f; sp += 0.5f) {
            hta_vitals_reset(&v);
            float c = hta_vitals_land(&v, sp);
            if (c < prev - 0.001f) monotone = 0;
            prev = c;
        }
        CHECK(monotone, "and a harder landing never costs less");
    }

    if (argc >= 2) {
        printf("\n[the Trial's own cyborg]\n");
        size_t n = 0;
        uint8_t *data = slurp(argv[1], &n);
        hta_cache c;
        char err[HTA_ERRLEN] = {0};
        if (data && hta_cache_open(&c, data, n, err, sizeof(err))) {
            hta_vitals v;
            CHECK(hta_vitals_load(&v, &c), "the cyborg's vitals load");
            printf("    %.0f health, %.0f shield, %.1fs delay, %.0f%%/s\n",
                   v.max_health, v.max_shield, v.recharge_delay,
                   v.recharge_rate * 100.0f);
            printf("    a fall hurts past %.1f wu/s and kills at %.1f\n",
                   v.fall_harmful_min, v.fall_fatal);
            CHECK(v.max_health == 75.0f, "75 body, which is Halo's");
            CHECK(v.max_shield == 75.0f, "and 75 shield");
            CHECK(v.recharge_delay > 3.0f && v.recharge_delay < 5.0f,
                  "about four seconds before it comes back");
            /* The velocities are per TICK in the tag; read as-is they would
             * make a gentle step lethal. */
            CHECK(v.fall_harmful_min > 3.0f,
                  "the falling speeds are per tick, not per second");
            CHECK(v.fall_fatal > v.fall_harmful_min, "and fatal is the faster one");

            /* A drop that should be survivable, at Halo's gravity. */
            float g = 3.4f;
            float v3 = sqrtf(2.0f * g * 3.0f);     /* three world units */
            hta_vitals_reset(&v);
            hta_vitals_land(&v, v3);
            printf("    a three-unit drop lands at %.1f wu/s\n", v3);
            CHECK(!v.died, "a three-unit drop is survivable");
        }
    }


    printf("\n[coming back]\n");
    {
        /* Where you respawn is chosen away from where you died. Nobody else
         * is in the map, so the only thing known to be dangerous is the
         * spot that just killed you -- which also stops a fall into the
         * same hole repeating forever.
         *
         * Synthetic points, because what is being checked is the choosing. */
        hta_spawn_point sp[8];
        memset(sp, 0, sizeof(sp));
        for (int i = 0; i < 8; i++) sp[i].position[0] = (float)i * 10.0f;

        uint32_t rng = 1u;
        float near_zero[3] = { 0.0f, 0.0f, 0.0f };
        int picked_far = 0, picked_near = 0;
        for (int k = 0; k < 200; k++) {
            uint32_t i = hta_scenario_spawn_pick(sp, 8, near_zero, &rng);
            if (sp[i].position[0] >= 40.0f) picked_far++;
            if (sp[i].position[0] <= 20.0f) picked_near++;
        }
        printf("  died at x=0: %d of 200 respawns past x=40, %d at x<=20\n",
               picked_far, picked_near);
        CHECK(picked_far == 200, "never comes back next to where it died");

        /* Not always the SAME far one, or a symmetrical map sends you to
         * the identical rock every time. */
        uint32_t seen[8];
        memset(seen, 0, sizeof(seen));
        rng = 1u;
        for (int k = 0; k < 200; k++)
            seen[hta_scenario_spawn_pick(sp, 8, near_zero, &rng)]++;
        int distinct = 0;
        for (int i = 0; i < 8; i++) if (seen[i]) distinct++;
        printf("  and spreads over %d of the far points\n", distinct);
        CHECK(distinct > 1, "it varies rather than always picking one");

        /* Degenerate cases must not index out of bounds. */
        CHECK(hta_scenario_spawn_pick(NULL, 0, NULL, &rng) == 0, "no points is 0");
        CHECK(hta_scenario_spawn_pick(sp, 1, near_zero, &rng) == 0,
              "one point is that point");
        uint32_t any = hta_scenario_spawn_pick(sp, 8, NULL, &rng);
        CHECK(any < 8u, "no place to avoid still returns a real index");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
