/* Somebody to shoot at. Needs the Trial's own map: the body, its health,
 * its shape and what a round does to it are all the cyborg's tags.
 */
#include "engine/bot.h"
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
    printf("bot tests\n");
    static hta_bot b;
    char err[HTA_ERRLEN] = {0};
    CHECK(!hta_bot_load(&b, NULL, NULL, 0, err, sizeof(err)), "no cache loads nothing");
    hta_bot_free(&b);
    hta_bot_free(NULL);
    CHECK(1, "freeing nothing is safe");

    if (argc < 2) {
        printf("\n  skip: pass bloodgulch.map\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }
    hta_cache c;
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) {
        printf("  FAIL: %s\n", err); return 1;
    }
    char bmp[1024];
    snprintf(bmp, sizeof(bmp), "%s", argv[1]);
    char *slash = strrchr(bmp, '/');
    if (slash) snprintf(slash + 1, sizeof(bmp) - (size_t)(slash + 1 - bmp), "bitmaps.map");
    size_t bsz = 0;
    uint8_t *bdata = slurp(bmp, &bsz);
    hta_resource_map bm;
    int have_bitmaps = bdata && hta_resource_open(&bm, bdata, bsz, err, sizeof(err));

    uint32_t bip = 0;
    for (uint32_t i = 0; i < c.tag_count && !bip; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(&c, i, &t)) continue;
        if (t.primary_class != HTA_FOURCC('b','i','p','d')) continue;
        char p[256];
        hta_cache_tag_path(&c, &t, p, sizeof(p));
        if (strstr(p, "cyborg_mp")) bip = t.tag_id;
    }

    printf("\n[a body]\n");
    bool ok = hta_bot_load(&b, &c, have_bitmaps ? &bm : NULL, bip, err, sizeof(err));
    printf("  %s\n", err);
    CHECK(ok, "it loads");
    if (!ok) { printf("\n%d checks, %d failures\n", checks, failures); return 1; }

    /* The same numbers the player runs on: a target that took a different
     * amount of killing would make every weapon feel wrong. */
    CHECK(b.vitals.max_health > 0.0f && b.vitals.max_shield > 0.0f,
          "with the player's own health and shield");
    CHECK(b.radius > 0.05f && b.radius < 0.5f, "and the biped's own width");
    CHECK(b.height > 0.4f && b.height < 1.2f, "and its height");

    float at[3] = { 100.0f, -150.0f, 1.0f };
    hta_bot_spawn(&b, at, 0.0f);
    hta_bot_update(&b, 1.0f / 60.0f);
    printf("  standing as '%s'\n", b.actor.graph.anims[b.actor.clip].name);
    /* Plain "idle" finds `B-driver unarmed idle` -- a body sitting in a
     * Banshee, hanging in the air. */
    CHECK(strstr(b.actor.graph.anims[b.actor.clip].name, "stand") != NULL,
          "  on its feet, not sitting in a vehicle");

    printf("\n[something in its hands]\n");
    {
        /* `stand rifle idle` poses the hands to HOLD a rifle. Without one
         * the body reads as a man standing with his arms out, which is what
         * it looked like on device. The weapon is not skinned -- it is a
         * rigid model riding the `right hand` marker. */
        uint32_t ar = 0;
        for (uint32_t i = 0; i < c.tag_count && !ar; i++) {
            hta_tag_entry t;
            if (!hta_cache_tag(&c, i, &t)) continue;
            if (t.primary_class != HTA_FOURCC('m','o','d','2')) continue;
            char p[192];
            hta_cache_tag_path(&c, &t, p, sizeof(p));
            if (strcmp(p, "weapons\\assault rifle\\assault rifle") == 0)
                ar = t.tag_id;
        }
        CHECK(ar != 0, "the map has a third-person assault rifle");
        uint32_t before = b.actor.mesh.vertex_count;
        bool armed = hta_bot_arm(&b, &c, have_bitmaps ? &bm : NULL, ar,
                                 err, sizeof(err));
        printf("  %s\n", err);
        CHECK(armed, "it takes the rifle");
        CHECK(b.actor.mesh.vertex_count > before, "  and the geometry arrives");

        hta_bot_spawn(&b, at, 0.0f);
        hta_bot_update(&b, 1.0f / 60.0f);

        /* Where the gun ended up, against the hand it hangs off. Getting
         * the pre-multiply wrong leaves it at the body's feet or out in
         * the map, and both look exactly like "armed" from a vertex count. */
        int32_t hand = -1;
        for (uint32_t i = 0; i < b.actor.graph.node_count; i++)
            if (strcmp(b.actor.graph.nodes[i].name, "bip01 r hand") == 0)
                hand = (int32_t)i;
        CHECK(hand >= 0, "  the body has a right hand");

        float gun[3] = {0,0,0}, hnd[3] = {0,0,0};
        uint32_t gn = 0, hn = 0;
        for (uint32_t v = 0; v < b.actor.mesh.vertex_count; v++) {
            const hta_skin_vertex *sk = &b.actor.skin[v];
            int held = (v >= before);
            float *acc = held ? gun : hnd;
            if (!held && !(sk->node[0] == (uint16_t)hand && sk->weight[0] > 0.5f))
                continue;
            for (int k = 0; k < 3; k++) acc[k] += b.actor.posed[v].pos[k];
            if (held) gn++; else hn++;
        }
        if (gn) for (int k = 0; k < 3; k++) gun[k] /= (float)gn;
        if (hn) for (int k = 0; k < 3; k++) hnd[k] /= (float)hn;
        float apart = sqrtf((gun[0]-hnd[0])*(gun[0]-hnd[0]) +
                            (gun[1]-hnd[1])*(gun[1]-hnd[1]) +
                            (gun[2]-hnd[2])*(gun[2]-hnd[2]));
        printf("  gun centres %.2f wu from the hand, at z %.2f (feet %.2f)\n",
               (double)apart, (double)gun[2], (double)at[2]);
        CHECK(apart < 0.25f, "  and it is IN the hand, not across the map");
        CHECK(gun[2] > at[2] + 0.2f, "  held up, not lying at its feet");
    }

    printf("\n[shooting at it]\n");
    {
        float ctr[3];
        hta_bot_centre(&b, ctr);
        float dir[3] = { 1.0f, 0.0f, 0.0f };
        float t = 0.0f, hit[3];

        float from[3] = { at[0] - 10.0f, at[1], ctr[2] };
        CHECK(hta_bot_ray(&b, from, dir, 50.0f, &t, hit), "a shot at its chest hits");
        printf("  at %.2f wu, entering (%.2f %.2f %.2f)\n",
               (double)t, (double)hit[0], (double)hit[1], (double)hit[2]);
        CHECK(fabsf(t - (10.0f - b.radius)) < 0.05f, "  at the front of it");

        float wide[3] = { at[0] - 10.0f, at[1] + 2.0f, ctr[2] };
        CHECK(!hta_bot_ray(&b, wide, dir, 50.0f, &t, hit), "two units wide misses");
        float over[3] = { at[0] - 10.0f, at[1], at[2] + 3.0f };
        CHECK(!hta_bot_ray(&b, over, dir, 50.0f, &t, hit), "over its head misses");
        float under[3] = { at[0] - 10.0f, at[1], at[2] - 1.0f };
        CHECK(!hta_bot_ray(&b, under, dir, 50.0f, &t, hit), "under its feet misses");
        CHECK(!hta_bot_ray(&b, from, dir, 1.0f, &t, hit), "and a short ray falls short");
    }

    printf("\n[what each weapon does to it]\n");
    {
        /* Every number is the tag's: `impact damage` on the projectile,
         * times that damage effect's multiplier for the material it lands
         * on. The shape of the result is Halo's -- a shotgun is weak
         * against shields and strong against bodies, and a plasma bolt is
         * the other way round. */
        uint32_t ids[32];
        uint32_t cnt = hta_weapon_list_playable(&c, ids, 32);
        int strong_vs_shield = 0, strong_vs_armour = 0, armed = 0;
        for (uint32_t i = 0; i < cnt; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err)))
                continue;
            uint32_t jpt = hta_projectile_impact_damage(&c, w.projectile_id);
            if (!jpt) continue;
            float vs = hta_damage_vs(&c, jpt, HTA_MATERIAL_CYBORG_SHIELD);
            float va = hta_damage_vs(&c, jpt, HTA_MATERIAL_CYBORG_ARMOR);
            printf("  %-16s %6.1f vs shield, %6.1f vs armour\n",
                   strrchr(w.path, '\\') + 1, (double)vs, (double)va);
            CHECK(vs > 0.0f && va > 0.0f, "  it does damage both ways");
            armed++;
            if (vs > va) strong_vs_shield++;
            if (va > vs) strong_vs_armour++;
        }
        CHECK(armed >= 8, "every weapon has a damage tag");
        CHECK(strong_vs_shield > 0, "something is better against shields");
        CHECK(strong_vs_armour > 0, "and something against bodies");
    }

    printf("\n[killing it]\n");
    {
        hta_bot_spawn(&b, at, 0.0f);
        float total = b.vitals.max_health + b.vitals.max_shield;
        int hits = 0;
        float hp[3] = { at[0], at[1], at[2] + 0.3f };
        while (b.state == HTA_BOT_ALIVE && hits < 500) {
            hta_bot_damage(&b, 10.0f, hp);
            hits++;
        }
        printf("  %d hits of 10 against %.0f health and shield\n",
               hits, (double)total);
        CHECK(hits == (int)(total / 10.0f), "it takes exactly what it has");
        CHECK(b.died, "  and says so once");
        CHECK(b.state == HTA_BOT_DYING, "  then goes down");
        printf("  dying as '%s'\n", b.actor.graph.anims[b.actor.clip].name);
        CHECK(strstr(b.actor.graph.anims[b.actor.clip].name, "kill") != NULL,
              "  playing one of Halo's kill clips");

        float ctr[3];
        hta_bot_centre(&b, ctr);
        float from[3] = { at[0] - 10.0f, at[1], ctr[2] };
        float dir[3] = { 1.0f, 0.0f, 0.0f }, t = 0.0f;
        CHECK(!hta_bot_ray(&b, from, dir, 50.0f, &t, NULL),
              "  a corpse stops no bullets");
        hta_bot_damage(&b, 1000.0f, NULL);
        CHECK(b.state == HTA_BOT_DYING, "  and cannot be killed twice");

        float elapsed = 0.0f;
        int frames = 0;
        while (b.state != HTA_BOT_ALIVE && frames < 4000) {
            hta_bot_update(&b, 1.0f / 60.0f);
            elapsed += 1.0f / 60.0f;
            frames++;
        }
        printf("  back up after %.2f s\n", (double)elapsed);
        CHECK(b.state == HTA_BOT_ALIVE, "it comes back");
        CHECK(elapsed > 4.0f && elapsed < 12.0f, "  after a while, not at once");
        CHECK(b.vitals.health == b.vitals.max_health, "  whole again");
        CHECK(hta_bot_ray(&b, from, dir, 50.0f, &t, NULL), "  and solid again");
    }

    printf("\n[it notices]\n");
    {
        /* Being shot and not going down should look like something. Halo
         * has `s-ping` for it: nine frames, three variants so a burst does
         * not look like a metronome. */
        hta_bot_spawn(&b, at, 0.0f);
        hta_bot_update(&b, 1.0f / 60.0f);
        const char *idle = b.actor.graph.anims[b.actor.clip].name;
        CHECK(b.flinch == 0.0f, "standing still, not flinching");

        hta_bot_damage(&b, 10.0f, NULL);
        printf("  hit -> '%s' for %.2f s\n",
               b.actor.graph.anims[b.actor.clip].name, (double)b.flinch);
        CHECK(b.state == HTA_BOT_ALIVE, "it survives a scratch");
        CHECK(b.flinch > 0.0f, "  and reacts");
        CHECK(strstr(b.actor.graph.anims[b.actor.clip].name, "ping") != NULL,
              "  with one of Halo's ping clips");

        /* Three variants, so repeated hits are not identical. */
        int distinct = 0;
        const char *seen[4] = { NULL, NULL, NULL, NULL };
        for (int k = 0; k < 40; k++) {
            b.vitals.health = b.vitals.max_health;
            b.vitals.shield = b.vitals.max_shield;
            hta_bot_damage(&b, 1.0f, NULL);
            const char *nm = b.actor.graph.anims[b.actor.clip].name;
            int have = 0;
            for (int q = 0; q < distinct; q++) if (seen[q] == nm) have = 1;
            if (!have && distinct < 4) seen[distinct++] = nm;
        }
        printf("  %d distinct reactions over 40 hits\n", distinct);
        CHECK(distinct > 1, "  and they vary");

        /* It hands the body back to standing rather than freezing. */
        float t = 0.0f;
        while (b.flinch > 0.0f && t < 3.0f) {
            hta_bot_update(&b, 1.0f / 60.0f);
            t += 1.0f / 60.0f;
        }
        printf("  back to '%s' after %.2f s\n",
               b.actor.graph.anims[b.actor.clip].name, (double)t);
        CHECK(strcmp(b.actor.graph.anims[b.actor.clip].name, idle) == 0,
              "  then stands up straight again");
        CHECK(t < 1.0f, "  quickly -- it is a flinch, not a stagger");
    }

    printf("\n[walking up to it]\n");
    {
        hta_bot_spawn(&b, at, 0.0f);
        float close[3] = { at[0] + 0.3f, at[1], at[2] };
        float far_off[3] = { at[0] + 4.0f, at[1], at[2] };
        CHECK(hta_bot_near(&b, close, 0.5f), "arm's reach counts");
        CHECK(!hta_bot_near(&b, far_off, 0.5f), "across the room does not");
        /* Melee does not care about height much: you can hit someone
         * standing on a step. */
        float high[3] = { at[0] + 0.3f, at[1], at[2] + 0.6f };
        CHECK(hta_bot_near(&b, high, 0.5f), "and alongside the body, not just its feet");

        float melee = hta_biped_melee_damage(&c, bip);
        printf("  a cyborg's melee does %.0f\n", (double)melee);
        CHECK(melee > 0.0f, "the biped has a melee damage tag");
        hta_bot_damage(&b, melee, NULL);
        CHECK(b.state != HTA_BOT_ALIVE, "  which is enough to kill outright");
    }

    hta_bot_free(&b);
    CHECK(!b.loaded, "free clears it");
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
