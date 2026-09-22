/* A game of Slayer on Blood Gulch with nobody watching. Needs the Trial's
 * own map past the argument checks: the roster, the spawns, the damage and
 * the words are all the tags'. The proof is bots walking the nav grid,
 * finding each other, and killing each other until one of them wins.
 */
#include "game/game.h"
#include "game/nav.h"
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/model.h"
#include "asset/effect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static const hta_game_weapon *by_label(const hta_game *g, const char *l)
{
    for (uint32_t i = 0; i < g->weapon_count; i++)
        if (!strcmp(g->weapons[i].label, l)) return &g->weapons[i];
    return NULL;
}

int main(int argc, char **argv)
{
    printf("game tests\n");
    static hta_game g;
    char err[HTA_ERRLEN] = {0};
    CHECK(!hta_game_load(&g, NULL, NULL, NULL, err, sizeof(err)), "no cache loads nothing");
    hta_game_free(&g);
    hta_game_update(&g, 0.1f);
    CHECK(1, "an empty game updates harmlessly");

    if (argc < 2) {
        printf("\n  skip: pass bloodgulch.map\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }
    hta_cache c;
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) { printf("  FAIL: %s\n", err); return 1; }
    hta_bsp_mesh mesh = {0}, cm = {0};
    hta_collision col = {0};
    bool ok = hta_bsp_load_first(&c, &mesh, err, sizeof(err)) &&
              hta_bsp_load_collision(&c, &cm, err, sizeof(err)) &&
              hta_scenario_add_collision(&cm, &c, err, sizeof(err)) &&
              hta_collision_build(&col, &cm);
    CHECK(ok, "Blood Gulch's collision loads");
    if (!ok) return 1;

    printf("\n[the roster]\n");
    ok = hta_game_load(&g, &c, NULL, &col, err, sizeof(err));
    printf("  %s\n", err);
    CHECK(ok, "the game loads");
    if (!ok) return 1;
    hta_collision_set_slope(&col, g.phys.max_slope);
    const hta_game_weapon *ar = by_label(&g, "ar"), *hp = by_label(&g, "hp");
    const hta_game_weapon *rl = by_label(&g, "rl"), *sg = by_label(&g, "sg");
    CHECK(ar && !strcmp(ar->anim_class, "rifle"), "the AR is held as a rifle");
    CHECK(hp && !strcmp(hp->anim_class, "pistol"), "the pistol as a pistol");
    CHECK(rl && !strcmp(rl->anim_class, "missile") && rl->travels,
          "the rocket launcher as a missile, and its rounds fly");
    CHECK(rl && rl->blast_damage > 50.0f, "and a rocket's blast is a real one");
    CHECK(sg && sg->def.projectiles_per_shot > 1, "the shotgun throws pellets");
    CHECK(ar && fabsf(ar->melee_damage - 56.0f) < 0.5f, "a rifle butt does the tag's 56");
    CHECK(g.start_weapon[0] >= 0 && g.start_weapon[1] >= 0, "everybody starts with two guns");
    CHECK(g.grenade_pool >= 0, "and the frag grenade flies");

    printf("\n[the words]\n");
    int32_t a = hta_game_add(&g, HTA_UNIT_BOT, NULL, 0);
    int32_t b = hta_game_add(&g, HTA_UNIT_BOT, NULL, 1);
    printf("  bots are called '%s' and '%s'\n", g.units[a].name, g.units[b].name);
    CHECK(g.units[a].name[0] && strcmp(g.units[a].name, g.units[b].name) != 0 &&
          strncmp(g.units[a].name, "Player", 6) != 0,
          "bots take distinct names from the Trial's own list");

    printf("\n[damage]\n");
    hta_game_start(&g);
    CHECK(g.units[a].alive && g.units[b].alive, "both spawn");
    hta_game_event e;
    int spawns = 0, announces = 0;
    while (hta_game_pop(&g, &e)) {
        if (e.kind == HTA_EV_SPAWN) spawns++;
        if (e.kind == HTA_EV_ANNOUNCE && e.line == HTA_LINE_SLAYER) announces++;
    }
    CHECK(spawns == 2 && announces == 1, "two spawns and the announcer says Slayer");
    /* Stand b in front of a and shoot him point blank with the pistol. */
    hta_unit *ua = &g.units[a], *ub = &g.units[b];
    ub->body.pos[0] = ua->body.pos[0] + 3.0f;
    ub->body.pos[1] = ua->body.pos[1];
    ub->body.pos[2] = ua->body.pos[2];
    float o[3] = { ua->body.pos[0], ua->body.pos[1], ua->body.pos[2] + 0.4f };
    float d[3] = { 1, 0, 0 };
    float t;
    CHECK(hta_game_ray(&g, o, d, 10.0f, a, &t, NULL) == b && fabsf(t - (3.0f - ub->body.phys.radius)) < 0.05f,
          "a ray finds the body in front at its skin");
    CHECK(hta_game_ray(&g, o, d, 2.0f, a, &t, NULL) == -1, "and not past the range");
    float before = ub->vitals.shield;
    hta_game_hurt_jpt(&g, b, a, hp->impact_jpt, 1, NULL);
    printf("  a pistol round took %.1f of %.1f shield\n", before - ub->vitals.shield, before);
    CHECK(fabsf((before - ub->vitals.shield) - 25.0f) < 0.5f, "the pistol does its 25 to a shield");
    ub->vitals.shield = 0.0f;
    float hb = ub->vitals.health;
    hta_game_hurt_jpt(&g, b, a, hp->impact_jpt, 1, NULL);
    CHECK(fabsf((hb - ub->vitals.health) - 37.5f) < 0.5f, "and 37.5 to armour");
    g.score_limit = 0;  /* NONE: a kill must not end the match. */
    hta_game_hurt(&g, b, a, 1000.0f, NULL);
    hta_game_update(&g, 1.0f / 30.0f);
    CHECK(!ub->alive && ua->kills == 1 && ua->score == 1 && ub->deaths == 1, "a kill is scored to the killer");
    CHECK(!g.over, "zero kill limit leaves the match running");
    g.score_limit = HTA_SLAYER_SCORE_LIMIT;
    bool feed = false;
    while (hta_game_pop(&g, &e))
        if (e.kind == HTA_EV_KILL && e.a == b && e.b == a) {
            printf("  feed: %s\n", e.text);
            feed = strstr(e.text, "was killed by") != NULL;
        }
    CHECK(feed, "in the Trial's words");
    for (int i = 0; i < 200 && !ub->alive; i++) hta_game_update(&g, 1.0f / 30.0f);
    CHECK(ub->alive, "and he is back after the respawn time");
    /* Blow yourself up. */
    float ctr[3];
    hta_game_centre(&g, a, ctr);
    int before_score = ua->score;
    hta_game_blast(&g, a, ctr, 5000.0f, 1.0f, 2.0f);
    hta_game_update(&g, 1.0f / 30.0f);
    CHECK(!ua->alive && ua->suicides == 1 && ua->score == before_score - 1,
          "your own rocket is a suicide, and costs a point");
    /* From behind. */
    for (int i = 0; i < 200 && !ua->alive; i++) hta_game_update(&g, 1.0f / 30.0f);
    ub->body.pos[0] = ua->body.pos[0] - 0.5f; ub->body.pos[1] = ua->body.pos[1];
    ub->body.pos[2] = ua->body.pos[2];
    ua->eye.yaw = 0.0f; ub->eye.yaw = 0.0f; ub->eye.pitch = 0.0f;
    ub->eye.pos[0] = ub->body.pos[0]; ub->eye.pos[1] = ub->body.pos[1];
    ub->eye.pos[2] = ub->body.pos[2] + ub->body.eye_height;
    ua->vitals.shield = ua->vitals.max_shield; ua->vitals.health = ua->vitals.max_health;
    int kb = ub->kills;
    memset(&ub->in, 0, sizeof(ub->in));
    ub->in.melee = true;
    ub->swing = 0.0f;
    g.units[b].kind = HTA_UNIT_REMOTE;    /* no brain: just the swing */
    hta_game_update(&g, 1.0f / 30.0f);
    CHECK(!ua->alive && ub->kills == kb + 1, "a swing at his back kills him");
    /* The host runs a remote player's controls through the same weapon
     * simulation as a bot. A remote shot must hurt the host-owned victim. */
    ua->kind=HTA_UNIT_LOCAL; ua->alive=true;
    ua->vitals=g.vitals_template; hta_vitals_reset(&ua->vitals);
    ub->kind=HTA_UNIT_REMOTE; ub->alive=true;
    ub->body.pos[0]=ua->body.pos[0]-0.45f;
    ub->body.pos[1]=ua->body.pos[1]; ub->body.pos[2]=ua->body.pos[2];
    ub->eye.yaw=0.0f; ub->eye.pitch=0.0f;
    ub->eye.pos[0]=ub->body.pos[0]; ub->eye.pos[1]=ub->body.pos[1];
    ub->eye.pos[2]=ub->body.pos[2]+ub->body.eye_height;
    ub->slot=1; ub->cooldown=ub->swing=ub->throwing=0.0f;
    memset(&ub->in,0,sizeof(ub->in)); ub->in.move.fire=true;
    float host_shield=ua->vitals.shield;
    int remote_ammo=ub->carry[1].ammo.loaded;
    hta_game_update(&g,1.0f/30.0f);
    CHECK(ua->vitals.shield<host_shield && ub->carry[1].ammo.loaded<remote_ammo,
          "remote controls fire on host authority and spend host ammo");
    ua->kind=HTA_UNIT_BOT;
    g.units[b].kind = HTA_UNIT_BOT;

    printf("\n[a match]\n");
    static hta_nav nav;
    hta_nav_params prm = { g.phys.radius, g.phys.coll_stand, g.phys.max_slope, 1.0f };
    ok = hta_nav_build(&nav, &col, mesh.bounds_min, mesh.bounds_max, &prm, err, sizeof(err));
    CHECK(ok, "the nav grid builds");
    static hta_pickups items;
    if (hta_pickups_load(&items, &c)) g.items = &items;
    g.nav = &nav;
    for (int i = 0; i < 4; i++) hta_game_add(&g, HTA_UNIT_BOT, NULL, 0);
    hta_game_set_skill(&g, 2);
    hta_game_start(&g);
    int kills = 0, feeds = 0, sprees = 0, multis = 0, pickups = 0, blasts = 0, shots = 0;
    int swaps = 0, over = 0;
    double t0 = now_s();
    float sim = 0.0f;
    const float dt = 1.0f / 30.0f;
    while (sim < 20.0f * 60.0f && !g.over) {
        if (items.loaded) hta_pickups_update(&items, dt);
        hta_game_update(&g, dt);
        sim += dt;
        while (hta_game_pop(&g, &e)) {
            if (e.kind == HTA_EV_KILL) { kills++; if (feeds < 6) printf("  feed: %s\n", e.text); feeds++; }
            if (e.kind == HTA_EV_ANNOUNCE && (e.line == HTA_LINE_KILLING_SPREE ||
                e.line == HTA_LINE_RUNNING_RIOT)) sprees++;
            if (e.kind == HTA_EV_ANNOUNCE && e.line >= HTA_LINE_DOUBLE_KILL &&
                e.line <= HTA_LINE_KILLTACULAR) multis++;
            if (e.kind == HTA_EV_PICKUP) pickups++;
            if (e.kind == HTA_EV_DETONATE) blasts++;
            if (e.kind == HTA_EV_FIRE) shots++;
            if (e.kind == HTA_EV_SWAP) swaps++;
            if (e.kind == HTA_EV_GAME_OVER) { over++; printf("  %s -- %s wins\n", e.text, g.units[e.a].name); }
        }
    }
    double took = now_s() - t0;
    printf("  %.1f simulated minutes in %.2f s: %d kills, %d shots, %d blasts, %d pickups, "
           "%d swaps, %d sprees, %d multikills\n",
           sim / 60.0f, took, kills, shots, blasts, pickups, swaps, sprees, multis);
    int32_t order[HTA_GAME_MAX_UNITS];
    uint32_t nst = hta_game_standings(&g, order, HTA_GAME_MAX_UNITS);
    for (uint32_t i = 0; i < nst; i++) {
        const hta_unit *u = &g.units[order[i]];
        char place[96];
        hta_game_place_text(&g, order[i], place, sizeof(place));
        printf("  %-12s %3d  (%d kills, %d deaths, %d suicides, %d assists)  \"%s\"\n",
               u->name, u->score, u->kills, u->deaths, u->suicides, u->assists, place);
    }
    CHECK(kills >= 20, "bots find each other and fight");
    CHECK(shots > kills * 5, "with guns, not luck");
    CHECK(pickups > 0, "and pick things up off the map");
    CHECK(g.over && over == 1 && g.units[g.winner].score >= g.score_limit,
          "until somebody reaches the score limit");
    int sum_kills = 0, sum_deaths = 0, sum_sui = 0;
    for (uint32_t i = 0; i < g.unit_count; i++) {
        sum_kills += g.units[i].kills + g.units[i].betrayals;
        sum_deaths += g.units[i].deaths;
        sum_sui += g.units[i].suicides;
    }
    CHECK(sum_kills + sum_sui == sum_deaths, "and every death is somebody's kill or their own");
    CHECK(took < sim / 20.0f, "at more than twenty times real time on the host");

    /* A time limit ends it with whoever is ahead, however few kills. */
    g.score_limit = 1000;
    g.time_limit = 30.0f;
    hta_game_start(&g);
    int timeouts = 0;
    for (sim = 0.0f; sim < 40.0f && !g.over; sim += dt) {
        hta_game_update(&g, dt);
        while (hta_game_pop(&g, &e)) if (e.kind == HTA_EV_GAME_OVER) timeouts++;
    }
    nst = hta_game_standings(&g, order, HTA_GAME_MAX_UNITS);
    CHECK(g.over && timeouts == 1 && fabsf(g.time - 30.0f) < 0.1f && nst &&
          g.winner == order[0], "a time limit ends the game with the leader winning");

    hta_game_remove(&g,b);
    CHECK(hta_game_add(&g,HTA_UNIT_REMOTE,"Rejoined",0)==b,
          "a disconnect frees its game slot for a later player");

    hta_game_free(&g);
    hta_pickups_free(&items);
    hta_nav_free(&nav);
    hta_collision_free(&col);
    hta_bsp_free(&mesh);
    hta_bsp_free(&cm);
    free(data);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
