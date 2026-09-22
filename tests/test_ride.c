/* Vehicles in the game: people get in, drive, shoot from them, run each
 * other over and get out -- through the game rules, the way a host runs
 * them for every player and bot. Needs the Trial's own map.
 */
#include "game/game.h"
#include "engine/vehicle.h"
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/model.h"
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

static hta_game g;
static hta_vehicles v;
static int kills, enters, exits, fires, detonations;
static int32_t last_victim = -1, last_killer = -1, last_weapon = -1;
static char last_text[96];

static void step(float seconds)
{
    const float dt = 1.0f / 60.0f;
    for (float t = 0; t < seconds; t += dt) {
        hta_game_update(&g, dt);
        hta_game_event e;
        while (hta_game_pop(&g, &e)) {
            if (e.kind == HTA_EV_KILL) {
                kills++; last_victim = e.a; last_killer = e.b; last_weapon = e.weapon;
                snprintf(last_text, sizeof(last_text), "%s", e.text);
            }
            if (e.kind == HTA_EV_ENTER) enters++;
            if (e.kind == HTA_EV_EXIT) exits++;
            if (e.kind == HTA_EV_FIRE) fires++;
            if (e.kind == HTA_EV_DETONATE) detonations++;
        }
    }
}
static void put(int32_t u, float x, float y, float z, float yaw)
{
    hta_unit *un = &g.units[u];
    hta_game_unseat(&g, u);
    un->alive = true;
    un->vitals = g.vitals_template;
    hta_vitals_reset(&un->vitals);
    float gz;
    if (hta_collision_ground(g.col, x, y, z + 1.0f, &gz)) z = gz;
    un->body.pos[0] = x; un->body.pos[1] = y; un->body.pos[2] = z;
    memset(un->body.velocity, 0, sizeof(un->body.velocity));
    un->body.on_ground = true;
    un->eye.yaw = yaw; un->eye.pitch = 0;
    un->eye.pos[0] = x; un->eye.pos[1] = y; un->eye.pos[2] = z + un->body.eye_height;
    memset(&un->in, 0, sizeof(un->in));
}
static int32_t car_at_placement(uint32_t placement)
{
    for (uint32_t i = 0; i < v.count; i++) if (v.cars[i].placement == placement) return (int32_t)i;
    return -1;
}
static void look_at(int32_t u, const float at[3])
{
    hta_unit *un = &g.units[u];
    float d[3] = { at[0]-un->eye.pos[0], at[1]-un->eye.pos[1], at[2]-un->eye.pos[2] };
    un->eye.yaw = atan2f(d[1], d[0]);
    un->eye.pitch = atan2f(d[2], hypotf(d[0], d[1]));
}

int main(int argc, char **argv)
{
    printf("ride tests\n");
    if (argc < 2) { printf("  skip: pass bloodgulch.map\n0 checks, 0 failures\n"); return 0; }
    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }
    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) { printf("  FAIL: %s\n", err); return 1; }
    CHECK(hta_vehicles_load(&v, &c, NULL, err, sizeof(err)), "vehicles load");
    printf("  %s\n", err);
    static hta_bsp_mesh cm;
    static hta_collision col;
    bool ok = hta_bsp_load_collision(&c, &cm, err, sizeof(err)) &&
              hta_scenario_add_collision_excluding(&cm, &c, v.skip, sizeof(v.skip), err, sizeof(err)) &&
              hta_collision_build(&col, &cm);
    CHECK(ok, "world collision without the vehicles");
    col.instances = v.inst; col.instance_count = v.count;
    ok = hta_game_load(&g, &c, NULL, &col, err, sizeof(err));
    CHECK(ok, "the game loads");
    uint32_t base_weapons = g.weapon_count;
    CHECK(hta_game_attach_vehicles(&g, &v, NULL), "vehicles join the game");
    printf("  roster %u -> %u, pools %u\n", base_weapons, g.weapon_count, g.pool_count);

    printf("\n[vehicle guns]\n");
    int jeep_t = -1, rjeep_t = -1, tank_t = -1, ghost_t = -1, bans_t = -1;
    for (uint32_t t = 0; t < v.type_count; t++) {
        const char *nm = v.types[t].name;
        if (!strcmp(nm, "mp_warthog")) jeep_t = (int)t;
        if (!strcmp(nm, "rwarthog")) rjeep_t = (int)t;
        if (!strcmp(nm, "scorpion_mp")) tank_t = (int)t;
        if (!strcmp(nm, "ghost_mp")) ghost_t = (int)t;
        if (!strcmp(nm, "banshee_mp")) bans_t = (int)t;
    }
    CHECK(jeep_t >= 0 && rjeep_t >= 0 && tank_t >= 0 && ghost_t >= 0 && bans_t >= 0, "five types by name");
    if (jeep_t < 0 || rjeep_t < 0 || tank_t < 0 || ghost_t < 0 || bans_t < 0) return 1;
    CHECK(g.vweapon[jeep_t][0] >= 0 && g.vweapon[jeep_t][1] < 0, "Warthog: one chaingun");
    CHECK(g.vweapon[tank_t][0] >= 0 && g.vweapon[tank_t][1] >= 0, "Scorpion: cannon and machine gun");
    CHECK(g.vweapon[bans_t][0] >= 0 && g.vweapon[bans_t][1] >= 0, "Banshee: bolts and fuel rod");
    const hta_game_weapon *chain = &g.weapons[g.vweapon[jeep_t][0]];
    const hta_game_weapon *cannon = &g.weapons[g.vweapon[tank_t][0]];
    const hta_game_weapon *bolts = &g.weapons[g.vweapon[ghost_t][0]];
    const hta_game_weapon *rod = &g.weapons[g.vweapon[bans_t][1]];
    const hta_game_weapon *rockets = &g.weapons[g.vweapon[rjeep_t][0]];
    CHECK(!chain->travels && chain->vehicle, "the chaingun is hitscan");
    CHECK(fabsf(chain->def.rof_initial - 8) < .01f && fabsf(chain->def.rof - 15) < .01f,
          "the chaingun spins up from 8 to 15 rounds a second");
    CHECK(cannon->travels && cannon->blast_damage > 0 && cannon->def.single_shot,
          "the tank shell flies, explodes, and fires once per pull");
    CHECK(bolts->travels && fabsf(bolts->speed - 50) < 1, "Ghost bolts fly at 50 wu/s");
    CHECK(rod->travels && rod->blast_damage > 0, "the fuel rod flies and explodes");
    CHECK(rockets->travels && rockets->def.rounds_loaded_max == 3, "rocket Warthog: three rockets a load");

    int32_t a = hta_game_add(&g, HTA_UNIT_REMOTE, "Driver", 0);
    int32_t b = hta_game_add(&g, HTA_UNIT_REMOTE, "Gunner", 0);
    int32_t t = hta_game_add(&g, HTA_UNIT_REMOTE, "Target", 1);
    hta_game_start(&g);
    step(0.1f);

    printf("\n[getting in]\n");
    int32_t car = car_at_placement(1);
    CHECK(car >= 0, "the Warthog from the owner's screenshot");
    if (car < 0) return 1;
    put(a, 101.75f, -144.8f, .53f, 3.14f);
    int32_t seat = -1;
    CHECK(hta_game_seat_near(&g, a, &seat) == car && seat == 0, "standing at its door offers the wheel");
    g.units[a].in.action = true;
    step(0.05f);
    CHECK(g.units[a].vehicle == car && g.units[a].seat == 0 && enters == 1, "action gets the driver in");
    put(b, v.cars[car].pos[0] - 1.6f*cosf(v.cars[car].yaw), v.cars[car].pos[1] - 1.6f*sinf(v.cars[car].yaw),
        v.cars[car].pos[2], 0);
    seat = -1;
    CHECK(hta_game_seat_near(&g, b, &seat) == car && seat == 2, "behind the Warthog is the gun");
    g.units[b].in.action = true;
    step(0.05f);
    CHECK(g.units[b].vehicle == car && g.units[b].seat == 2, "the gunner climbs on");
    hta_transform root;
    CHECK(hta_game_seat_root(&g, b, &root) &&
          hypotf(root.t[0]-g.units[b].body.pos[0], root.t[1]-g.units[b].body.pos[1]) < .01f,
          "a seated body sits on its seat marker");

    printf("\n[driving]\n");
    float start[3];
    memcpy(start, v.cars[car].pos, sizeof(start));
    g.units[a].in.move.move_forward = 1;
    step(2.0f);
    float moved = hypotf(v.cars[car].pos[0]-start[0], v.cars[car].pos[1]-start[1]);
    printf("  drove %.2f wu at %.2f wu/s\n", moved, hta_vehicles_speed(&v, (uint32_t)car));
    CHECK(moved > 3, "the driver's stick drives the Warthog");
    CHECK(hta_game_seat_root(&g, b, &root) &&
          hypotf(root.t[0]-g.units[b].body.pos[0], root.t[1]-g.units[b].body.pos[1]) < .2f,
          "the gunner rides along");
    g.units[a].in.action = true;
    step(0.02f);
    CHECK(g.units[a].vehicle == car, "no getting out at speed");
    g.units[a].in.move.move_forward = 0;
    g.units[a].in.move.jump = true;
    step(1.5f);
    g.units[a].in.move.jump = false;

    printf("\n[shooting from it]\n");
    float aim_at[3];
    hta_transform world;
    hta_vehicles_world(&v, (uint32_t)car, &world);
    float ahead[3] = { 6, 0, 0 };
    hta_xf_point(aim_at, &world, ahead);
    put(t, aim_at[0], aim_at[1], aim_at[2], 0);
    float chest[3];
    hta_game_centre(&g, t, chest);
    kills = 0;
    fires = 0;
    for (int i = 0; i < 240 && kills == 0; i++) {
        look_at(b, chest);
        g.units[b].in.move.fire = true;
        step(1.0f / 60.0f);
        hta_game_centre(&g, t, chest);
    }
    g.units[b].in.move.fire = false;
    printf("  %d rounds, %s\n", fires, last_text);
    CHECK(kills == 1 && last_victim == t && last_killer == b, "the gunner kills with the chaingun");
    CHECK(last_weapon == g.vweapon[jeep_t][0], "and the kill is the chaingun's");
    CHECK(fires > 5, "at the chaingun's rate");

    printf("\n[getting out]\n");
    g.units[a].in.action = true;
    step(0.05f);
    CHECK(g.units[a].vehicle < 0 && exits >= 1, "stopped, the driver gets out");
    CHECK(v.cars[car].occupant[0] < 0 && !v.cars[car].ctl.driven, "and the wheel is free");
    CHECK(hypotf(g.units[a].body.pos[0]-v.cars[car].pos[0], g.units[a].body.pos[1]-v.cars[car].pos[1]) < 2.5f &&
          hypotf(g.units[a].body.pos[0]-v.cars[car].pos[0], g.units[a].body.pos[1]-v.cars[car].pos[1]) > .3f,
          "beside the Warthog, not in it");

    printf("\n[dying in a seat]\n");
    hta_game_hurt(&g, b, t, 1000, NULL);
    step(0.05f);
    CHECK(!g.units[b].alive && g.units[b].vehicle < 0 && v.cars[car].occupant[2] < 0,
          "a dead gunner leaves the gun");

    printf("\n[splatter]\n");
    g.units[a].in.action = true;
    step(0.05f);
    CHECK(g.units[a].vehicle == car, "back behind the wheel");
    step(g.respawn_time + 0.5f);
    hta_vehicles_world(&v, (uint32_t)car, &world);
    float road[3] = { 7, 0, 0 };
    float spot[3];
    hta_xf_point(spot, &world, road);
    put(t, spot[0], spot[1], spot[2], 0);
    kills = 0;
    g.units[a].in.move.move_forward = 1;
    for (int i = 0; i < 300 && kills == 0; i++) {
        g.units[t].in.move.move_forward = 0;
        step(1.0f / 60.0f);
    }
    g.units[a].in.move.move_forward = 0;
    printf("  %s\n", last_text);
    CHECK(kills == 1 && last_victim == t && last_killer == a, "the driver runs the target over");
    g.units[a].in.move.jump = true;
    step(2.0f);
    g.units[a].in.move.jump = false;
    g.units[a].in.action = true;
    step(0.05f);

    printf("\n[the Scorpion]\n");
    int32_t tank = car_at_placement(6);
    CHECK(tank >= 0 && v.cars[tank].kind == HTA_VK_TANK, "a Scorpion at placement 6");
    if (tank >= 0) {
        step(g.respawn_time + 0.5f);
        put(t, v.cars[tank].pos[0] + 3, v.cars[tank].pos[1], v.cars[tank].pos[2], 0);
        CHECK(!hta_game_seat(&g, t, tank, 1), "no riding a tank without a driver");
        CHECK(hta_game_seat(&g, a, tank, 0), "the driver takes the tank");
        CHECK(hta_game_enclosed(&g, a), "the tank's driver is under armour");
        float c0[3];
        hta_game_centre(&g, a, c0);
        float from[3] = { c0[0] + 5, c0[1], c0[2] }, dir[3] = { -1, 0, 0 };
        CHECK(hta_game_ray(&g, from, dir, 10, t, NULL, NULL) != a, "and bullets cannot reach him");
        CHECK(hta_game_seat(&g, t, tank, 1), "a rider can climb on once he is in");
        hta_game_unseat(&g, t);
        hta_vehicles_world(&v, (uint32_t)tank, &world);
        float far_pt[3] = { 30, 0, 2 };
        float tgt[3];
        hta_xf_point(tgt, &world, far_pt);
        hta_camera cam = g.units[a].eye;
        (void)cam;
        detonations = 0;
        fires = 0;
        for (int i = 0; i < 90; i++) {
            look_at(a, tgt);
            g.units[a].in.move.fire = i < 3 || (i > 30 && i < 33);
            step(1.0f / 60.0f);
        }
        g.units[a].in.move.fire = false;
        int shells = fires;
        step(1.0f);
        printf("  %d shell(s), %d detonation(s)\n", shells, detonations);
        CHECK(shells == 1, "one shell per pull, then the chamber");
        CHECK(detonations >= 1, "and the shell goes off down range");
        g.units[a].in.fire2 = true;
        fires = 0;
        step(1.0f);
        g.units[a].in.fire2 = false;
        CHECK(fires >= 10, "the machine gun is the second trigger");
        hta_game_unseat(&g, a);
    }

    printf("\n[the Banshee]\n");
    int32_t bans = car_at_placement(18);
    CHECK(bans >= 0 && v.cars[bans].kind == HTA_VK_FIGHTER, "a Banshee at placement 18");
    if (bans >= 0) {
        CHECK(hta_game_seat(&g, a, bans, 0), "a pilot climbs in");
        float z0 = v.cars[bans].pos[2];
        g.units[a].eye.yaw = v.cars[bans].yaw;
        g.units[a].eye.pitch = 0.5f;
        g.units[a].in.move.move_forward = 1;
        step(2.0f);
        g.units[a].in.move.move_forward = 0;
        CHECK(v.cars[bans].pos[2] > z0 + 2, "and flies up where he looks");
        detonations = 0;
        g.units[a].eye.pitch = -0.6f;
        g.units[a].in.fire2 = true;
        step(0.1f);
        g.units[a].in.fire2 = false;
        step(3.0f);
        CHECK(detonations >= 1, "the fuel rod falls and bursts");
        hta_game_unseat(&g, a);
    }

    printf("\n[teams]\n");
    g.teams = true;
    int32_t ghost = car_at_placement(2);
    if (ghost >= 0) {
        put(b, v.cars[ghost].pos[0] + 5, v.cars[ghost].pos[1], v.cars[ghost].pos[2], 0);
        g.units[b].alive = true;
        CHECK(hta_game_seat(&g, t, ghost, 0), "blue takes a Ghost");
        put(a, v.cars[ghost].pos[0] - 0.6f*cosf(v.cars[ghost].yaw), v.cars[ghost].pos[1] - 0.6f*sinf(v.cars[ghost].yaw),
            v.cars[ghost].pos[2], 0);
        CHECK(hta_game_seat_near(&g, a, NULL) != ghost, "red cannot climb onto blue's Ghost");
    }

    printf("\n[leaving]\n");
    hta_game_seat(&g, a, car, 0);
    hta_game_remove(&g, a);
    CHECK(v.cars[car].occupant[0] < 0, "a player who leaves the game leaves the seat");

    hta_game_free(&g);
    hta_vehicles_free(&v);
    hta_collision_free(&col);
    hta_bsp_free(&cm);
    free(data);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
