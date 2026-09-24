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
    {
        /* Spawn protection: nobody hurts the fresh spawn until it runs out
         * or the spawn fires first. */
        g.spawn_protect = 2.0f;
        hta_game_hurt(&g, b, a, 1000.0f, NULL);
        hta_game_update(&g, 1.0f / 30.0f);
        for (int i = 0; i < 200 && !ub->alive; i++) hta_game_update(&g, 1.0f / 30.0f);
        float sh = ub->vitals.shield;
        hta_game_hurt(&g, b, a, 10.0f, NULL);
        CHECK(ub->alive && ub->protect > 0.0f && ub->vitals.shield == sh, "a fresh spawn is protected");
        ub->fired = true;
        hta_game_update(&g, 1.0f / 30.0f);
        ub->fired = false;
        hta_game_hurt(&g, b, a, 10.0f, NULL);
        CHECK(ub->protect <= 0.0f && ub->vitals.shield < sh, "and firing gives the protection up");
        g.spawn_protect = 0.0f;
    }
    {
        /* A character's body: twice the health, no shield, hits 1.5x. */
        static hta_oal_asset brute;
        memset(&brute, 0, sizeof(brute));
        snprintf(brute.kind, sizeof(brute.kind), "character");
        brute.loaded = true;
        brute.body_health = 2.0f; brute.body_shield = 0.0f; brute.body_damage = 1.5f;
        int32_t k = hta_game_add_character(&g, &brute);
        ub->character = (int8_t)k;
        hta_game_hurt(&g, b, a, 1000.0f, NULL);
        hta_game_update(&g, 1.0f / 30.0f);           /* the death is taken here */
        for (int i = 0; i < 300 && !ub->alive; i++) hta_game_update(&g, 1.0f / 30.0f);
        CHECK(ub->alive && fabsf(ub->vitals.max_health - 2.0f * g.vitals_template.max_health) < 1e-3f &&
              ub->vitals.max_shield == 0.0f && ub->vitals.shield == 0.0f,
              "a character spawns with its own health and shield");
        ua->character = (int8_t)k;
        ub->protect = 0.0f;
        float h0 = ub->vitals.health;
        hta_game_hurt(&g, b, a, 10.0f, NULL);
        CHECK(fabsf((h0 - ub->vitals.health) - 15.0f) < 0.01f, "and hits 1.5x as hard");
        ua->riding = true;
        h0 = ub->vitals.health;
        hta_game_hurt(&g, b, a, 10.0f, NULL);
        CHECK(fabsf((h0 - ub->vitals.health) - 15.0f) < 0.01f, "(its fly penalty defaults to none)");
        ua->riding = false; ua->character = ub->character = -1;
    }
    {
        /* An ability: laser eyes, a sniper round every 5 s, never a class weapon. */
        static hta_oal_asset hero;
        memset(&hero, 0, sizeof(hero));
        snprintf(hero.kind, sizeof(hero.kind), "character");
        hero.loaded = true; hero.body_shield = -1.0f;
        snprintf(hero.ability_name, sizeof(hero.ability_name), "LASER EYES");
        snprintf(hero.ability_base, sizeof(hero.ability_base), "sniper rifle");
        hero.ability_damage = 1.4f; hero.ability_cooldown = 5.0f;
        uint32_t roster = g.weapon_count;
        int32_t k = hta_game_add_character(&g, &hero);
        int32_t aw = k >= 0 ? g.char_ability[k] : -1;
        CHECK(aw == (int32_t)roster && g.weapons[aw].hidden && !hta_game_class_weapon(&g, aw) &&
              !strcmp(g.weapons[aw].display, "LASER EYES") && fabsf(g.weapons[aw].damage_scale - 1.4f) < 1e-6f,
              "a character's ability is a hidden copy of its base weapon");
        ua->character = (int8_t)k;
        ua->ability_cool = 0.0f;
        CHECK(hta_game_ability_charge(&g, a) == 1.0f && hta_game_ability(&g, a), "a ready ability fires");
        CHECK(!hta_game_ability(&g, a) && hta_game_ability_charge(&g, a) < 0.05f, "and then cools down");
        for (int i = 0; i < 160; i++) hta_game_update(&g, 1.0f / 30.0f);
        CHECK(hta_game_ability_charge(&g, a) >= 1.0f, "and is ready again after its cooldown");
        ua->character = -1;
        CHECK(hta_game_ability_charge(&g, a) < 0.0f, "a Spartan has none");
    }
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

    printf("\n[capture the flag]\n");
    static hta_game f;
    ok = hta_game_load(&f, &c, NULL, &col, err, sizeof(err));
    printf("  %s\n", err);
    CHECK(ok && f.flags[0].present && f.flags[1].present &&
          fabsf(f.flags[0].home[0] - 95.69f) < 0.05f && fabsf(f.flags[0].home[1] + 159.45f) < 0.05f &&
          fabsf(f.flags[1].home[0] - 40.24f) < 0.05f && fabsf(f.flags[1].home[1] + 79.12f) < 0.05f,
          "both stands are read from the scenario's netgame flags, red's at red's end");
    {
        /* Red's starts crowd round red's stand: that is what says usage 0 is red. */
        float red[2] = { 0, 0 }; int nr = 0;
        for (uint32_t i = 0; i < f.spawn_count; i++)
            if (f.spawns[i].team_index == 0) { red[0] += f.spawns[i].position[0]; red[1] += f.spawns[i].position[1]; nr++; }
        CHECK(nr && hypotf(red[0] / nr - f.flags[0].home[0], red[1] / nr - f.flags[0].home[1]) < 10.0f,
              "and team 0's spawns are gathered round team 0's flag");
    }
    const hta_game_weapon *fw = f.flag_weapon >= 0 ? &f.weapons[f.flag_weapon] : NULL;
    printf("  flag: label '%s', held as '%s', melee %.0f\n", fw ? fw->label : "", fw ? fw->anim_class : "",
           fw ? fw->melee_damage : 0.0f);
    CHECK(fw && fw->model && !fw->def.projectile_id && fw->melee_jpt,
          "the flag joins the roster with a model and a swing, and nothing to fire");
    CHECK(hta_game_weapon_index(&f, fw ? fw->tag : 0) == f.flag_weapon, "and can be looked up like any weapon");
    CHECK(!hta_game_set_mode(&f, HTA_MODE_COUNT) && f.mode == HTA_MODE_SLAYER && !f.teams,
          "a mode that does not exist plays Slayer");
    CHECK(hta_game_set_mode(&f, HTA_MODE_CTF) && f.teams, "CTF is a team game");
    int32_t r = hta_game_add(&f, HTA_UNIT_REMOTE, "Red", HTA_TEAM_AUTO);
    int32_t bl = hta_game_add(&f, HTA_UNIT_REMOTE, "Blue", HTA_TEAM_AUTO);
    int32_t r2 = hta_game_add(&f, HTA_UNIT_REMOTE, "Red2", HTA_TEAM_AUTO);
    CHECK(f.units[r].team == 0 && f.units[bl].team == 1 && f.units[r2].team == 0,
          "each newcomer goes to the smaller side");
    hta_game_start(&f);
    bool said = false;
    while (hta_game_pop(&f, &e))
        if (e.kind == HTA_EV_ANNOUNCE && e.line == HTA_LINE_CTF) { said = true; printf("  \"%s\"\n", e.text); }
    CHECK(said, "the announcer says capture the flag");
    {
        bool own = true;
        for (int32_t u = 0; u < 3; u++) {
            float best = 1e9f; int team = -1;
            for (uint32_t i = 0; i < f.spawn_count; i++) {
                float d = hypotf(f.spawns[i].position[0] - f.units[u].body.pos[0],
                                 f.spawns[i].position[1] - f.units[u].body.pos[1]);
                if (d < best) { best = d; team = f.spawns[i].team_index; }
            }
            if (team != f.units[u].team) own = false;
        }
        CHECK(own, "everyone starts at their own end");
    }
    hta_unit *ur = &f.units[r], *ub2 = &f.units[bl];
    float mate_before = f.units[r2].vitals.shield;
    hta_game_hurt(&f, r2, r, 10.0f, NULL);
    CHECK(f.units[r2].vitals.shield == mate_before, "a teammate's round does nothing");

    /* Walk red onto blue's stand. */
    #define PUT(u, p) do { (u)->body.pos[0] = (p)[0]; (u)->body.pos[1] = (p)[1]; \
        (u)->body.pos[2] = (p)[2] + 0.05f; (u)->body.velocity[0] = (u)->body.velocity[1] = 0; } while (0)
    PUT(ur, f.flags[1].home);
    int took_ev = 0, fires = 0;
    for (int i = 0; i < 5; i++) {
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e)) {
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_TAKEN && e.a == r && e.b == 1 &&
                e.line == HTA_LINE_RED_HAS_FLAG) took_ev++;
        }
    }
    CHECK(ur->flag == 1 && f.flags[1].state == HTA_FLAG_CARRIED && f.flags[1].carrier == r && took_ev == 1,
          "red takes blue's flag off its stand, and red has the flag");
    CHECK(hta_game_held(&f, r) == fw, "and holds it instead of a gun");
    ur->in.move.fire = true;
    ur->in.grenade = true;
    int nades = ur->grenades;
    for (int i = 0; i < 30; i++) {
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e)) if (e.kind == HTA_EV_FIRE && e.a == r) fires++;
    }
    ur->in.move.fire = false;
    CHECK(fires == 0 && ur->grenades == nades, "with the flag you can neither shoot nor throw");
    CHECK(fabsf(f.flags[1].pos[0] - ur->body.pos[0]) < 1e-3f, "the flag goes where its carrier goes");

    /* Blue takes red's flag too: now red cannot score. */
    PUT(ub2, f.flags[0].home);
    for (int i = 0; i < 3; i++) { hta_game_update(&f, dt); while (hta_game_pop(&f, &e)) {} }
    CHECK(ub2->flag == 0, "blue takes red's");
    PUT(ur, f.flags[0].home);
    for (int i = 0; i < 5; i++) { hta_game_update(&f, dt); while (hta_game_pop(&f, &e)) {} }
    CHECK(f.team_score[0] == 0 && ur->flag == 1, "no capture while your own flag is away");

    /* Blue dies; red's flag lies where he fell, and a red player takes it home. */
    for (uint32_t i = 0; i < f.spawn_count; i++)
        if (f.spawns[i].team_index == 0 &&
            hypotf(f.spawns[i].position[0] - f.flags[0].home[0], f.spawns[i].position[1] - f.flags[0].home[1]) > 2.0f) {
            PUT(ub2, f.spawns[i].position);
            break;
        }
    hta_game_hurt(&f, bl, r, 1000.0f, NULL);
    int drops = 0, returns = 0, caps = 0;
    for (int i = 0; i < 30; i++) {
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e)) {
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_DROP && e.b == 0) drops++;
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_RETURN && e.a == r2 && e.line == HTA_LINE_RED_RETURNED) returns++;
        }
    }
    CHECK(!ub2->alive && drops == 1 && f.flags[0].state == HTA_FLAG_DROPPED && f.flags[0].rest,
          "a carrier who dies drops the flag, and it comes to rest");
    PUT(&f.units[r2], f.flags[0].pos);
    for (int i = 0; i < 5; i++) {
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e)) {
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_RETURN && e.a == r2 && e.line == HTA_LINE_RED_RETURNED) returns++;
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_CAPTURE) caps++;
        }
    }
    CHECK(returns == 1 && f.flags[0].state == HTA_FLAG_HOME, "a teammate's touch sends it home");
    /* Red is still standing on red's stand with blue's flag. */
    for (int i = 0; i < 5; i++) {
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e))
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_CAPTURE && e.a == r && e.line == HTA_LINE_RED_SCORE) {
                caps++; printf("  \"%s\"\n", e.text);
            }
    }
    char place[96];
    hta_game_place_text(&f, r, place, sizeof(place));
    printf("  \"%s\"\n", place);
    CHECK(caps == 1 && f.team_score[0] == 1 && ur->score == 1 && ur->flag < 0 &&
          f.flags[1].state == HTA_FLAG_HOME, "and with ours home, theirs on our stand is a capture");
    CHECK(strstr(place, "1") && strstr(place, "0"), "the HUD line says red leads 1 to 0");

    /* Swap puts it down; left alone it goes home by itself. */
    PUT(ur, f.flags[1].home);
    for (int i = 0; i < 3; i++) { hta_game_update(&f, dt); while (hta_game_pop(&f, &e)) {} }
    ur->in.swap = true;
    hta_game_update(&f, dt);
    CHECK(ur->flag < 0 && f.flags[1].state == HTA_FLAG_DROPPED, "the swap button puts the flag down");
    for (uint32_t i = 0; i < f.spawn_count; i++)
        if (f.spawns[i].team_index == 1 &&
            hypotf(f.spawns[i].position[0] - f.flags[1].home[0], f.spawns[i].position[1] - f.flags[1].home[1]) > 3.0f) {
            PUT(ur, f.spawns[i].position);
            break;
        }
    int timeouts_f = 0;
    for (float t = 0; t < HTA_FLAG_RESET + 1.0f; t += dt) {
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e))
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_RETURN && e.a == -1) timeouts_f++;
    }
    CHECK(timeouts_f == 1 && f.flags[1].state == HTA_FLAG_HOME, "a flag left lying goes home by itself");

    /* Two more and it is over. */
    int overs = 0;
    for (int k = 0; k < 2; k++) {
        PUT(ur, f.flags[1].home);
        for (int i = 0; i < 3; i++) { hta_game_update(&f, dt); while (hta_game_pop(&f, &e)) {} }
        PUT(ur, f.flags[0].home);
        for (int i = 0; i < 3; i++) {
            hta_game_update(&f, dt);
            while (hta_game_pop(&f, &e)) if (e.kind == HTA_EV_GAME_OVER) { overs++; printf("  \"%s\"\n", e.text); }
        }
    }
    CHECK(f.team_score[0] == 3 && f.over && f.winner_team == 0 && overs == 1,
          "three captures win it for the team");
    hta_game_free(&f);

    printf("\n[team slayer]\n");
    ok = hta_game_load(&f, &c, NULL, &col, err, sizeof(err));
    f.nav = &nav;
    if (items.loaded) { hta_pickups_reset(&items); f.items = &items; }
    CHECK(hta_game_set_mode(&f, HTA_MODE_TEAM_SLAYER) && f.teams && f.score_limit == HTA_SLAYER_SCORE_LIMIT,
          "team slayer is a team game to 25");
    f.score_limit = 15;
    for (int i = 0; i < 6; i++) hta_game_add(&f, HTA_UNIT_BOT, NULL, HTA_TEAM_AUTO);
    hta_game_set_skill(&f, 2);
    hta_game_start(&f);
    int ts_line = 0, ts_over = 0;
    for (sim = 0.0f; sim < 20.0f * 60.0f && !f.over; sim += dt) {
        if (items.loaded) hta_pickups_update(&items, dt);
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e)) {
            if (e.kind == HTA_EV_ANNOUNCE && e.line == HTA_LINE_TEAM_SLAYER) ts_line++;
            if (e.kind == HTA_EV_GAME_OVER) ts_over++;
        }
    }
    {
        int sum[2] = { 0, 0 }, betray = 0;
        for (uint32_t i = 0; i < f.unit_count; i++) {
            sum[f.units[i].team & 1] += f.units[i].kills - f.units[i].betrayals - f.units[i].suicides;
            betray += f.units[i].betrayals;
        }
        hta_game_place_text(&f, 0, place, sizeof(place));
        printf("  %.1f minutes: red %d blue %d, %d betrayals; \"%s\"\n", sim / 60.0f,
               f.team_score[0], f.team_score[1], betray, place);
        CHECK(ts_line == 1, "the announcer says team slayer");
        CHECK(sum[0] == f.team_score[0] && sum[1] == f.team_score[1],
              "a team's score is its players' kills less their suicides");
        CHECK(betray == 0, "nobody is betrayed with friendly fire off");
        CHECK(f.over && ts_over == 1 && f.winner_team >= 0 && f.team_score[f.winner_team] >= 15,
              "the first team to the limit wins");
    }
    hta_game_free(&f);

    printf("\n[a bot runs the flag]\n");
    ok = hta_game_load(&f, &c, NULL, &col, err, sizeof(err));
    f.nav = &nav;
    hta_game_set_mode(&f, HTA_MODE_CTF);
    int32_t runner = hta_game_add(&f, HTA_UNIT_BOT, NULL, HTA_TEAM_RED);
    hta_game_set_skill(&f, 1);
    hta_game_start(&f);
    double tf = now_s();
    int run_took = 0, run_caps = 0;
    for (sim = 0.0f; sim < 4.0f * 60.0f && !run_caps; sim += dt) {
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e)) {
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_TAKEN && e.a == runner) {
                run_took++; printf("  %5.1f s  takes blue's flag\n", sim);
            }
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_CAPTURE && e.a == runner) {
                run_caps++; printf("  %5.1f s  \"%s\"\n", sim, e.text);
            }
        }
    }
    CHECK(f.stand_field[0] && f.stand_field[1], "each stand's way home is worked out at the start");
    CHECK(run_took == 1 && run_caps == 1 && f.team_score[0] == 1,
          "left alone, a bot crosses the map, takes the flag and brings it home");
    printf("  (%.2f s of host time, the fields included)\n", now_s() - tf);
    hta_game_free(&f);

    printf("\n[bots play capture the flag]\n");
    ok = hta_game_load(&f, &c, NULL, &col, err, sizeof(err));
    f.nav = &nav;
    if (items.loaded) { hta_pickups_reset(&items); f.items = &items; }
    hta_game_set_mode(&f, HTA_MODE_CTF);
    f.score_limit = HTA_CTF_SCORE_LIMIT;
    for (int i = 0; i < 6; i++) hta_game_add(&f, HTA_UNIT_BOT, NULL, HTA_TEAM_AUTO);
    hta_game_set_skill(&f, 1);
    hta_game_start(&f);
    int takes = 0, fcaps = 0, frets = 0, fkills = 0, fover = 0;
    for (sim = 0.0f; sim < 30.0f * 60.0f && !f.over; sim += dt) {
        if (items.loaded) hta_pickups_update(&items, dt);
        hta_game_update(&f, dt);
        while (hta_game_pop(&f, &e)) {
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_TAKEN) takes++;
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_RETURN) frets++;
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_CAPTURE) {
                fcaps++; printf("  %4.0f s  %s scores for %s\n", sim, f.units[e.a].name, e.b ? "red" : "blue");
            }
            if (e.kind == HTA_EV_KILL) fkills++;
            if (e.kind == HTA_EV_GAME_OVER) fover++;
        }
    }
    printf("  %.1f simulated minutes: %d takes, %d returns, %d captures, %d kills; red %d blue %d\n",
           sim / 60.0f, takes, frets, fcaps, fkills, f.team_score[0], f.team_score[1]);
    CHECK(takes >= 3 && frets >= 1, "both sides go for the flags, and win them back");
    CHECK(fkills > 20, "and fight over them");
    CHECK(!f.over || (fover == 1 && f.team_score[f.winner_team] >= HTA_CTF_SCORE_LIMIT),
          "a game that ends, ends at three");
    hta_game_free(&f);

    printf("\n[a client mirrors the host's rules]\n");
    ok = hta_game_load(&f, &c, NULL, &col, err, sizeof(err));
    {
        /* A joining phone: its own player (blue) and the host's two others,
         * as WORLD would give them; the rules come as GAME. */
        int32_t me = hta_game_add(&f, HTA_UNIT_LOCAL, "Me", HTA_TEAM_BLUE);
        int32_t foe = hta_game_add(&f, HTA_UNIT_REMOTE, "Foe", HTA_TEAM_RED);
        int32_t pal = hta_game_add(&f, HTA_UNIT_REMOTE, "Pal", HTA_TEAM_BLUE);
        f.local = me;
        f.units[foe].team = 0; f.units[pal].team = 1; f.units[me].team = 1;
        hta_game_flag_state fl[2];
        memset(fl, 0, sizeof(fl));
        for (int t = 0; t < 2; t++) {
            fl[t].present = true; fl[t].state = HTA_FLAG_HOME; fl[t].carrier = -1;
            memcpy(fl[t].pos, f.flags[t].home, sizeof(fl[t].pos));
        }
        int sc[2] = { 0, 0 };
        hta_game_mirror_rules(&f, HTA_MODE_CTF, 3, sc, -1, fl);
        CHECK(f.mode == HTA_MODE_CTF && f.teams && f.score_limit == 3, "the host's CTF becomes ours");
        while (hta_game_pop(&f, &e)) {}
        fl[1].state = HTA_FLAG_CARRIED; fl[1].carrier = foe;
        hta_game_mirror_rules(&f, HTA_MODE_CTF, 3, sc, -1, fl);
        bool took = false; char said[96] = "";
        while (hta_game_pop(&f, &e))
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_TAKEN) { took = true; snprintf(said, sizeof(said), "%s", e.text); }
        printf("  taken: \"%s\"\n", said);
        CHECK(took && f.units[foe].flag == 1 && strstr(said, "enemy"),
              "the enemy taking our flag is announced in our words");
        fl[1].state = HTA_FLAG_HOME; fl[1].carrier = -1; sc[0] = 1;
        hta_game_mirror_rules(&f, HTA_MODE_CTF, 3, sc, -1, fl);
        bool scored = false;
        while (hta_game_pop(&f, &e))
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_CAPTURE && e.a == foe) scored = true;
        CHECK(scored && f.units[foe].flag < 0 && f.team_score[0] == 1,
              "home again with red's score up: a capture, and his hands are empty");
        fl[0].state = HTA_FLAG_DROPPED; fl[0].carrier = -1; fl[0].pos[0] += 5;
        hta_game_mirror_rules(&f, HTA_MODE_CTF, 3, sc, -1, fl);
        fl[0].state = HTA_FLAG_HOME; memcpy(fl[0].pos, f.flags[0].home, sizeof(fl[0].pos));
        hta_game_mirror_rules(&f, HTA_MODE_CTF, 3, sc, -1, fl);
        bool returned = false;
        while (hta_game_pop(&f, &e))
            if (e.kind == HTA_EV_FLAG && e.pool == HTA_FLAG_RETURN) returned = true;
        CHECK(returned, "home with no score change: returned");
    }
    hta_game_free(&f);

    {
        static hta_game hero_match;
        static hta_oal_asset laser_hero;
        memset(&laser_hero, 0, sizeof(laser_hero));
        snprintf(laser_hero.kind, sizeof(laser_hero.kind), "character");
        laser_hero.loaded = true;
        laser_hero.body_shield = -1.0f;
        laser_hero.unique_limit = 1;
        laser_hero.ability_beam = true;
        laser_hero.ability_damage = 300.0f;
        laser_hero.ability_cooldown = 8.0f;
        snprintf(laser_hero.ability_base, sizeof(laser_hero.ability_base), "sniper rifle");
        CHECK(hta_game_load(&hero_match, &c, NULL, &col, err, sizeof(err)), "hero match loads");
        int32_t hero = hta_game_add_character(&hero_match, &laser_hero);
        int32_t player = hta_game_add(&hero_match, HTA_UNIT_LOCAL, "Hero", 0);
        int32_t bot = hta_game_add(&hero_match, HTA_UNIT_BOT, "Bot", 0);
        int32_t rival = hta_game_add(&hero_match, HTA_UNIT_REMOTE, "Rival", 0);
        CHECK(hta_game_assign_character(&hero_match, bot, hero) &&
              hta_game_assign_character(&hero_match, player, hero) &&
              hero_match.units[bot].character == -1,
              "a player claiming a unique hero displaces a bot");
        CHECK(!hta_game_assign_character(&hero_match, rival, hero),
              "another player cannot claim that hero");
        hero_match.allow_duplicate_heroes = true;
        CHECK(hta_game_assign_character(&hero_match, rival, hero),
              "custom rules can allow duplicate heroes");
        hero_match.allow_duplicate_heroes = false;
        hero_match.units[rival].character = -1;
        hta_game_start(&hero_match);
        hero_match.col = NULL; /* no world wall along this synthetic firing line */
        hta_unit *shooter = &hero_match.units[player];
        shooter->eye.pos[0] = shooter->eye.pos[1] = 0.0f;
        shooter->eye.pos[2] = 0.4f;
        shooter->eye.yaw = shooter->eye.pitch = 0.0f;
        for (int k = 0; k < 2; k++) {
            int unit = k ? rival : bot;
            hero_match.units[unit].body.pos[0] = k ? 4.0f : 2.0f;
            hero_match.units[unit].body.pos[1] = 0.0f;
            hero_match.units[unit].body.pos[2] = 0.0f;
        }
        shooter->ability_cool = 0.0f;
        CHECK(hta_game_ability(&hero_match, player) &&
              hero_match.units[bot].vitals.health <= 0.0f &&
              hero_match.units[rival].vitals.health <= 0.0f,
              "one laser beam burns through two opponents");
        CHECK(!hta_game_ability(&hero_match, player), "the beam enters cooldown");
        hta_game_free(&hero_match);
    }

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
