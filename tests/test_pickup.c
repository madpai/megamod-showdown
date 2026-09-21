/* What the map leaves on the ground. Needs the Trial's own bloodgulch.map:
 * every position, weight and respawn time here is the scenario's. */
#include "engine/pickup.h"
#include "asset/cache.h"
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

static const char *KIND[] = {
    "none", "weapon", "grenade", "health", "overshield", "camo", "speed", "vision"
};

int main(int argc, char **argv)
{
    printf("pickup tests\n");
    static hta_pickups p;
    CHECK(!hta_pickups_load(&p, NULL), "no cache loads nothing");
    hta_pickups_update(&p, 1.0f);
    CHECK(hta_pickups_at(&p, NULL) == -1, "and nothing is underfoot");

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

    printf("\n[what Blood Gulch puts out]\n");
    CHECK(hta_pickups_load(&p, &c), "the placements load");
    printf("  %u placement(s)\n", p.count);
    /* The real map has 37. Fewer means something was filtered that should
     * not have been; more means the stride is wrong. */
    CHECK(p.count == 37u, "all thirty-seven of them");

    int kinds[8];
    memset(kinds, 0, sizeof(kinds));
    for (uint32_t i = 0; i < p.count; i++) {
        const hta_item_choice *ch = hta_pickups_item(&p, (int32_t)i);
        if (ch) kinds[ch->kind]++;
    }
    printf("  on the ground:");
    for (int k = 1; k < 8; k++) if (kinds[k]) printf("  %d %s", kinds[k], KIND[k]);
    printf("\n");
    CHECK(kinds[HTA_ITEM_NONE] == 0, "nothing is left unclassified");
    CHECK(kinds[HTA_ITEM_WEAPON] >= 15, "the weapons are there");
    CHECK(kinds[HTA_ITEM_GRENADE] >= 12, "and the grenades");
    CHECK(kinds[HTA_ITEM_HEALTH] == 2, "two health packs, one per base");

    printf("\n[respawn times are the tags']\n");
    {
        /* The placement's own time wins; the collection's default is the
         * fallback. The rocket launcher is written 90 s on the placement
         * over the collection's 120, and the snipers 120 over 0. */
        float longest = 0.0f, shortest = 1e9f;
        for (uint32_t i = 0; i < p.count; i++) {
            if (p.spawn[i].respawn > longest) longest = p.spawn[i].respawn;
            if (p.spawn[i].respawn < shortest) shortest = p.spawn[i].respawn;
        }
        printf("  %.0fs to %.0fs\n", (double)shortest, (double)longest);
        CHECK(shortest > 0.0f, "nothing respawns instantly");
        CHECK(longest >= 120.0f, "and the best gear makes you wait");
    }

    printf("\n[walking over things]\n");
    {
        int32_t slot = -1;
        for (uint32_t i = 0; i < p.count && slot < 0; i++) {
            const hta_item_choice *ch = hta_pickups_item(&p, (int32_t)i);
            if (ch && ch->kind == HTA_ITEM_WEAPON) slot = (int32_t)i;
        }
        CHECK(slot >= 0, "there is a weapon to stand on");
        const float *at = p.spawn[slot].position;
        float on[3] = { at[0], at[1], at[2] };
        CHECK(hta_pickups_at(&p, on) == slot, "standing on it finds it");
        CHECK(hta_pickups_at_kind(&p, on, HTA_ITEM_WEAPON) == slot,
              "  and finds it by kind");
        CHECK(hta_pickups_at_kind(&p, on, HTA_ITEM_HEALTH) != slot,
              "  but not as something it is not");

        float away[3] = { at[0] + HTA_PICKUP_REACH * 3.0f, at[1], at[2] };
        CHECK(hta_pickups_at(&p, away) != slot, "standing back does not");

        /* Taking it. */
        float respawn = p.spawn[slot].respawn;
        hta_pickups_take(&p, slot);
        CHECK(hta_pickups_at(&p, on) != slot, "once taken it is gone");
        CHECK(hta_pickups_item(&p, slot) == NULL, "  and holds nothing");

        hta_pickups_update(&p, respawn * 0.5f);
        CHECK(hta_pickups_at(&p, on) != slot, "  still gone halfway through");
        hta_pickups_update(&p, respawn * 0.5f + 0.1f);
        CHECK(hta_pickups_at(&p, on) == slot, "  and back when the tag says");
        CHECK(p.respawned == slot, "  which is reported once");
        hta_pickups_update(&p, 0.016f);
        CHECK(p.respawned == -1, "  and only once");
    }

    printf("\n[the pedestal rolls again]\n");
    {
        /* One placement in the middle of Blood Gulch is overshield or
         * active camouflage, fifty-fifty, and it draws afresh every time it
         * comes back rather than handing out the same one forever. */
        int32_t ped = -1;
        for (uint32_t i = 0; i < p.count; i++)
            if (p.spawn[i].choice_count > 1) { ped = (int32_t)i; break; }
        CHECK(ped >= 0, "there is a weighted placement");
        if (ped >= 0) {
            printf("  %u choices:", p.spawn[ped].choice_count);
            for (uint32_t k = 0; k < p.spawn[ped].choice_count; k++)
                printf("  %s", KIND[p.spawn[ped].choice[k].kind]);
            printf("\n");
            int seen[8];
            memset(seen, 0, sizeof(seen));
            for (int round = 0; round < 200; round++) {
                const hta_item_choice *ch = hta_pickups_item(&p, ped);
                if (ch) seen[ch->kind]++;
                hta_pickups_take(&p, ped);
                hta_pickups_update(&p, p.spawn[ped].respawn + 0.1f);
            }
            printf("  over 200 respawns: %d overshield, %d camouflage\n",
                   seen[HTA_ITEM_OVERSHIELD], seen[HTA_ITEM_CAMOUFLAGE]);
            CHECK(seen[HTA_ITEM_OVERSHIELD] > 40 && seen[HTA_ITEM_CAMOUFLAGE] > 40,
                  "  both come up, roughly evenly");
        }
    }

    printf("\n[the powerups know what they are]\n");
    {
        /* From the equipment's own `powerup type`, NOT its path -- the
         * overshield's model is the camouflage's and vice versa in Bungie's
         * tags, so anything keying off names hands out the wrong one. */
        for (uint32_t i = 0; i < p.count; i++) {
            for (uint32_t k = 0; k < p.spawn[i].choice_count; k++) {
                const hta_item_choice *ch = &p.spawn[i].choice[k];
                if (ch->kind != HTA_ITEM_OVERSHIELD &&
                    ch->kind != HTA_ITEM_CAMOUFLAGE) continue;
                printf("  %-22s %-10s lasts %.0fs\n",
                       strrchr(ch->path, '\\') ? strrchr(ch->path, '\\') + 1 : ch->path,
                       KIND[ch->kind], (double)ch->powerup_time);
                CHECK(ch->powerup_time > 10.0f, "  it lasts a sensible time");
                CHECK(ch->model_id != 0, "  and has something to look at");
            }
        }
    }


    printf("\n[what you spawn holding]\n");
    {
        /* Blood Gulch has NO player starting profile -- in multiplayer the
         * loadout belongs to the gametype, which does not ship inside a
         * map -- but it does carry `starting equipment`, and its first
         * block names the assault rifle and the pistol. The campaign map's
         * profile agrees down to the magazines (AR 60/240, pistol 12/72),
         * which is what says these offsets are right. */
        uint32_t w[8];
        uint32_t k = hta_scenario_starting_weapons(&c, w, 8);
        printf("  %u weapon(s), carry cap %u\n", k, HTA_CARRY_MAX);
        CHECK(k == 2u, "the map arms you with two");
        CHECK(k <= HTA_CARRY_MAX, "which is what you can carry");

        int saw_ar = 0, saw_pistol = 0;
        for (uint32_t i = 0; i < k; i++) {
            char path[96];
            hta_item_kind kind = hta_item_kind_of(&c, w[i], path, sizeof(path));
            printf("    %s\n", path);
            CHECK(kind == HTA_ITEM_WEAPON, "  and it is a weapon");
            if (strstr(path, "assault rifle")) saw_ar = 1;
            if (strstr(path, "pistol") && !strstr(path, "plasma")) saw_pistol = 1;
        }
        CHECK(saw_ar, "an assault rifle");
        CHECK(saw_pistol, "and a pistol");

        /* The second starting-equipment block is a variant, not more of the
         * first: taking it too would arm you with three. */
        CHECK(hta_scenario_starting_weapons(&c, w, 1) == 1u,
              "asking for one gives one");
        CHECK(hta_scenario_starting_weapons(&c, NULL, 8) == 0,
              "nowhere to put them is none");
        CHECK(hta_scenario_starting_weapons(NULL, w, 8) == 0,
              "no cache is none");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
