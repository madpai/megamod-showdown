/* htamatch -- a bot game of Slayer on the owner's Blood Gulch, rendered
 * offscreen from behind one of the bots. For looking at bots, their
 * animations and their weapons without a phone.
 *
 *   htamatch <bloodgulch.map> [--bots N] [--skill 0-3] [--seconds S] [--mode ffa|team|ctf]
 *            [--shots N] [--every S] [--follow UNIT] [--out prefix]
 *            [--width W] [--height H] [--vehicles] [--seed N]
 *
 * Simulates S seconds, then keeps simulating and takes a frame every
 * `--every` seconds from a camera behind and above the followed unit.
 * `--vehicles` makes the map's vehicles live for the bots to take; every
 * getting in, getting out and wreck is printed, and at the end how long
 * bots drove and how much of it their cars spent blocked. `--seed` plays
 * a different match of the same setup; judge a change on several.
 */
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/bitmap.h"
#include "asset/model.h"
#include "engine/scene_light.h"
#include "game/game.h"
#include "game/view.h"
#include "engine/contrail.h"
#include "game/nav.h"
#include "gfx/gfx.h"
#include "platform/platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f); if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n); if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *size = (size_t)n; return p;
}

static bool ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb"); if (!f) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) fwrite(rgba + i * 4, 1, 3, f);
    fclose(f); return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: htamatch <bloodgulch.map> [--bots N] [--skill 0-3] "
                        "[--seconds S] [--shots N] [--every S] [--follow UNIT] "
                        "[--out prefix] [--width W] [--height H] [--mode ffa|team|ctf] "
                        "[--vehicles] [--seed N]\n");
        return 2;
    }
    int bots = 4, skill = 2, shots = 3, follow = 0, ride = -1;
    bool vehicles = false;
    uint32_t seed = 0;
    hta_game_mode mode = HTA_MODE_SLAYER;
    float seconds = 20.0f, every = 0.5f, back = 1.6f;
    uint32_t W = 800, H = 450;
    const char *prefix = "match";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--bots") && i + 1 < argc) bots = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--skill") && i + 1 < argc) skill = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--follow") && i + 1 < argc) follow = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "--back") && i + 1 < argc) back = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--ride") && i + 1 < argc) ride = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--vehicles")) vehicles = true;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            mode = !strcmp(m, "ctf") ? HTA_MODE_CTF : !strcmp(m, "team") ? HTA_MODE_TEAM_SLAYER
                                                                            : HTA_MODE_SLAYER;
        }
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) W = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) H = (uint32_t)atoi(argv[++i]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (bots < 1 || bots > HTA_GAME_MAX_UNITS || shots < 0 || shots > 200 ||
        W < 16 || H < 16 || W > 4096 || H > 4096) return 2;

    char err[HTA_ERRLEN] = {0};
    static hta_vehicles veh;
    size_t map_size = 0, bm_size = 0;
    uint8_t *map_data = slurp(argv[1], &map_size);
    if (!map_data) { fprintf(stderr, "cannot read map\n"); return 1; }
    hta_cache cache;
    if (!hta_cache_open(&cache, map_data, map_size, err, sizeof(err))) {
        fprintf(stderr, "map: %s\n", err); return 1;
    }
    char bmpath[1024];
    snprintf(bmpath, sizeof(bmpath), "%s", argv[1]);
    char *slash = strrchr(bmpath, '/');
    if (slash) snprintf(slash + 1, sizeof(bmpath) - (size_t)(slash + 1 - bmpath), "bitmaps.map");
    uint8_t *bm_data = slurp(bmpath, &bm_size);
    hta_resource_map bm = {0};
    if (bm_data) hta_resource_open(&bm, bm_data, bm_size, err, sizeof(err));
    const hta_resource_map *bmp = bm.data ? &bm : NULL;

    hta_bsp_mesh mesh = {0}, sky = {0}, cm = {0};
    if (!hta_bsp_load_first(&cache, &mesh, err, sizeof(err)) ||
        !hta_bsp_load_textures(&cache, bmp, &mesh, err, sizeof(err))) {
        fprintf(stderr, "BSP: %s\n", err); return 1;
    }
    float bmin[3], bmax[3];
    memcpy(bmin, mesh.bounds_min, sizeof(bmin));
    memcpy(bmax, mesh.bounds_max, sizeof(bmax));
    /* With --ride the vehicles are live: drawn as parts, solid as placed
     * grids, and seated. */
    if ((ride >= 0 || vehicles) && !hta_vehicles_load(&veh, &cache, bmp, err, sizeof(err))) {
        fprintf(stderr, "vehicles: %s\n", err); return 1;
    }
    hta_scenario_add_objects_excluding(&mesh, &cache, bmp, veh.skip, sizeof(veh.skip), err, sizeof(err));
    hta_sky_load(&sky, &cache, bmp, err, sizeof(err));
    hta_collision col = {0};
    if (!hta_bsp_load_collision(&cache, &cm, err, sizeof(err)) ||
        !hta_scenario_add_collision_excluding(&cm, &cache, veh.skip, sizeof(veh.skip), err, sizeof(err)) ||
        !hta_collision_build(&col, &cm)) {
        fprintf(stderr, "collision: %s\n", err); return 1;
    }

    static hta_game game;
    static hta_game_view view;
    static hta_nav nav;
    static hta_pickups items;
    if (!hta_game_load(&game, &cache, bmp, &col, err, sizeof(err))) {
        fprintf(stderr, "game: %s\n", err); return 1;
    }
    printf("game           %s\n", err);
    game.rng ^= seed * 0x9E3779B9u;
    hta_collision_set_slope(&col, game.phys.max_slope);
    double t0 = hta_time_seconds();
    hta_nav_params prm = { game.phys.radius, game.phys.coll_stand, game.phys.max_slope, 1.0f };
    if (!hta_nav_build(&nav, &col, bmin, bmax, &prm, err, sizeof(err))) {
        fprintf(stderr, "nav: %s\n", err); return 1;
    }
    printf("nav            %s (%.0f ms)\n", err, (hta_time_seconds() - t0) * 1000.0);
    if (veh.loaded) {
        col.instances = veh.inst; col.instance_count = veh.count;
        hta_game_attach_vehicles(&game, &veh, bmp);
    }
    game.nav = &nav;
    if (hta_pickups_load(&items, &cache)) {
        hta_pickups_build(&items, &cache, bmp, err, sizeof(err));
        game.items = &items;
    }
    if (!hta_game_set_mode(&game, mode)) printf("mode           not on this map: Slayer\n");
    for (int i = 0; i < bots; i++)
        hta_game_add(&game, ride >= 0 && i < 2 ? HTA_UNIT_REMOTE : HTA_UNIT_BOT, NULL,
                     HTA_TEAM_AUTO);
    hta_game_set_skill(&game, (uint8_t)skill);
    if (!hta_game_view_load(&view, &game, bmp, (uint32_t)bots, err, sizeof(err))) {
        fprintf(stderr, "view: %s\n", err); return 1;
    }
    printf("view           %s\n", err);
    hta_game_start(&game);
    int32_t ride_car = -1;
    for (uint32_t i = 0; veh.loaded && i < veh.count; i++)
        if (veh.cars[i].placement == (uint32_t)ride) ride_car = (int32_t)i;
    if (ride_car >= 0) {
        uint32_t seats = hta_vehicles_seat_count(&veh, (uint32_t)ride_car);
        int32_t d = hta_vehicles_driver_seat(&veh, (uint32_t)ride_car);
        printf("ride           %s, %u seats: driver %s, second %s\n",
               veh.types[veh.cars[ride_car].type].name, seats,
               hta_game_seat(&game, 0, ride_car, d) ? "in" : "REFUSED",
               seats > 1 && bots > 1 && hta_game_seat(&game, 1, ride_car, (int32_t)seats - 1) ? "in" : "-");
        game.units[0].eye.yaw = veh.cars[ride_car].yaw;
    }
    hta_gfx_mesh *vgpu[HTA_VEHICLE_TYPES] = {0};
    /* Tracers and trails: each roster weapon's round, each pool's. */
    static hta_contrails trails;
    uint32_t wtrail[HTA_GAME_MAX_WEAPONS], ptrail[HTA_GAME_MAX_POOLS];
    for (uint32_t w = 0; w < game.weapon_count; w++)
        wtrail[w] = hta_contrails_for_projectile(&trails, &cache, bmp, game.weapons[w].def.projectile_id);
    for (uint32_t p = 0; p < game.pool_count; p++)
        ptrail[p] = hta_contrails_for_projectile(&trails, &cache, bmp, game.pools[p].proj_tag_id);
    printf("contrails      %s\n", hta_contrails_build(&trails, err, sizeof(err)) ? err : "none");
    hta_gfx *gfx = hta_gfx_create_offscreen(W, H, err, sizeof(err));
    if (!gfx) { fprintf(stderr, "Vulkan: %s\n", err); return 1; }
    hta_gfx_mesh *world = hta_gfx_mesh_upload(gfx, &mesh, err, sizeof(err));
    hta_gfx_mesh *skygpu = sky.index_count ? hta_gfx_mesh_upload(gfx, &sky, err, sizeof(err)) : NULL;
    hta_gfx_mesh *itemgpu = items.have_mesh ?
        hta_gfx_mesh_upload_dynamic_world(gfx, &items.mesh, err, sizeof(err)) : NULL;
    hta_gfx_mesh *body[HTA_GAME_MAX_UNITS] = {0};
    for (int i = 0; i < bots; i++)
        body[i] = hta_gfx_mesh_upload_dynamic_world(gfx, &view.actor[i].mesh, err, sizeof(err));
    hta_gfx_mesh *wgpu[HTA_GAME_MAX_WEAPONS] = {0};
    for (uint32_t w = 0; w < game.weapon_count; w++)
        if (view.have_weapon[w]) wgpu[w] = hta_gfx_mesh_upload(gfx, &view.weapon_mesh[w], err, sizeof(err));
    hta_gfx_mesh *poolgpu[HTA_GAME_MAX_POOLS] = {0};
    for (uint32_t p = 0; p < game.pool_count; p++)
        poolgpu[p] = hta_gfx_mesh_upload_dynamic_world(gfx, &game.pools[p].mesh, err, sizeof(err));
    if (!world) { fprintf(stderr, "GPU: %s\n", err); return 1; }
    hta_gfx_mesh *trailgpu = trails.loaded ?
        hta_gfx_mesh_upload_dynamic(gfx, &trails.mesh, err, sizeof(err)) : NULL;
    for (uint32_t t2 = 0; veh.loaded && t2 < veh.type_count; t2++)
        vgpu[t2] = hta_gfx_mesh_upload(gfx, &veh.types[t2].mesh, err, sizeof(err));

    hta_camera cam;
    hta_camera_init(&cam);
    cam.aspect = (float)W / (float)H;
    cam.znear = 0.02f;
    cam.zfar = (bmax[0] - bmin[0]) * 6.0f;
    hta_scene scene = {0};
    hta_scene_light_from_bsp(&mesh, scene.light_dir, scene.light_color, scene.ambient);
    scene.clear[0] = 0.42f; scene.clear[1] = 0.55f; scene.clear[2] = 0.72f;

    uint8_t *rgba = malloc((size_t)W * H * 4u);
    const float dt = 1.0f / 30.0f;
    float t = 0.0f, next_shot = seconds;
    int taken = 0, kills = 0, entries = 0;
    double sim_time = 0.0, driven = 0.0;
    uint32_t items_upload = 8;
    while (taken < shots || (shots == 0 && t < seconds)) {
        double s0 = hta_time_seconds();
        hta_pickups_update(&items, dt);
        if (ride_car >= 0) {
            game.units[0].in.move.move_forward = 1.0f;
            game.units[0].in.move.move_right = 0.3f;
            game.units[1].eye.yaw = veh.cars[ride_car].yaw + 0.8f;
        }
        hta_game_update(&game, dt);
        hta_game_view_update(&view, &game, -1, dt);
        for (uint32_t c = 0; vehicles && c < veh.count; c++) {
            int32_t ds = hta_vehicles_driver_seat(&veh, c);
            if (veh.cars[c].active && ds >= 0 && veh.cars[c].occupant[ds] >= 0) driven += dt;
        }
        sim_time += hta_time_seconds() - s0;
        t += dt;
        hta_game_event e;
        while (hta_game_pop(&game, &e)) {
            if (e.kind == HTA_EV_KILL) { kills++; printf("  %6.1f  %s\n", t, e.text); }
            if (e.kind == HTA_EV_FIRE && e.weapon >= 0 && (uint32_t)e.weapon < game.weapon_count &&
                !game.weapons[e.weapon].travels && wtrail[e.weapon] != HTA_CONT_NONE) {
                float end[3], ht = 100.0f;
                hta_collision_ray(&col, e.pos, e.dir, 100.0f, &ht, NULL, NULL);
                for (int k = 0; k < 3; k++) end[k] = e.pos[k] + e.dir[k] * ht;
                hta_contrails_tracer(&trails, wtrail[e.weapon], e.pos, end, 300.0f);
            }
            if (e.kind == HTA_EV_ANNOUNCE && e.line != HTA_LINE_NONE) printf("  %6.1f  [%s]\n", t, e.text);
            if (vehicles && e.kind == HTA_EV_ENTER && e.b >= 0) entries++;
            if (vehicles && (e.kind == HTA_EV_ENTER || e.kind == HTA_EV_EXIT) && e.b >= 0)
                printf("  %6.1f  %s %s %s seat %d at %.0f,%.0f\n", t, game.units[e.a].name,
                       e.kind == HTA_EV_ENTER ? "into" : "out of",
                       veh.types[veh.cars[e.b].type].name, e.pool, e.pos[0], e.pos[1]);
            if (vehicles && e.kind == HTA_EV_WRECK) printf("  %6.1f  wreck\n", t);
        }
        for (uint32_t p = 0; p < game.pool_count; p++) {
            if (ptrail[p] == HTA_CONT_NONE) continue;
            for (uint32_t k = 0; k < HTA_PROJ_MAX; k++)
                if (game.pools[p].live[k].alive)
                    hta_contrails_feed(&trails, ptrail[p], p * 16u + k, game.pools[p].live[k].pos,
                                       game.pools[p].live[k].age);
        }
        hta_contrails_update(&trails, &cam, dt);
        if (hta_pickups_dirty(&items)) { hta_pickups_pose(&items); items_upload = 8; }
        if (shots == 0 || t < next_shot) continue;
        next_shot += every;

        /* Behind and above the followed unit, looking where it looks. */
        const hta_unit *u = &game.units[follow % bots];
        int look_drop = -1;
        if (getenv("HTA_LOOK_DROP"))
            for (int i = 0; i < HTA_GAME_MAX_DROPS && look_drop < 0; i++)
                if (game.drops[i].live && game.drops[i].rest) look_drop = i;
        if (look_drop >= 0) {
            const hta_game_drop *dr = &game.drops[look_drop];
            cam.pos[0] = dr->pos[0] - 0.9f; cam.pos[1] = dr->pos[1] - 0.5f; cam.pos[2] = dr->pos[2] + 0.6f;
            float to[3] = { dr->pos[0]-cam.pos[0], dr->pos[1]-cam.pos[1], dr->pos[2]-cam.pos[2] };
            cam.yaw = atan2f(to[1], to[0]); cam.pitch = atan2f(to[2], hypotf(to[0], to[1]));
        } else if (ride_car >= 0) {
            /* Film the car from off its left rear quarter. */
            const hta_vehicle *c = &veh.cars[ride_car];
            float a = c->yaw + 2.4f, dist = 0.9f + c->body_radius;
            cam.pos[0] = c->pos[0] + cosf(a) * dist; cam.pos[1] = c->pos[1] + sinf(a) * dist;
            cam.pos[2] = c->pos[2] + 1.0f;
            float to[3] = { c->pos[0]-cam.pos[0], c->pos[1]-cam.pos[1], c->pos[2]+0.4f-cam.pos[2] };
            cam.yaw = atan2f(to[1], to[0]); cam.pitch = atan2f(to[2], hypotf(to[0], to[1]));
        } else {
        /* A negative --back stands in front, looking at its face. */
        float up = back < 0.0f ? 0.25f : 0.7f;
        float fx = cosf(u->eye.yaw), fy = sinf(u->eye.yaw);
        float look[3] = { u->body.pos[0], u->body.pos[1], u->body.pos[2] + 0.45f };
        float want[3] = { look[0] - fx * back, look[1] - fy * back, look[2] + up };
        float d[3] = { want[0]-look[0], want[1]-look[1], want[2]-look[2] };
        float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        float hit_t;
        for (int k = 0; k < 3; k++) d[k] /= len;
        if (hta_collision_ray(&col, look, d, len, &hit_t, NULL, NULL))
            for (int k = 0; k < 3; k++) want[k] = look[k] + d[k] * hit_t * 0.85f;
        for (int k = 0; k < 3; k++) cam.pos[k] = want[k];
        float ahead = back < 0.0f ? 0.0f : 3.0f;
        float to[3] = { look[0]-cam.pos[0] + fx * ahead, look[1]-cam.pos[1] + fy * ahead,
                        look[2]-cam.pos[2] };
        cam.yaw = atan2f(to[1], to[0]);
        cam.pitch = atan2f(to[2], hypotf(to[0], to[1]));
        }

        hta_gfx_dynamic dyn[HTA_GFX_MAX_DYNAMIC];
        memset(dyn, 0, sizeof(dyn));
        uint32_t nd = 0;
        for (int i = 0; i < bots && nd < HTA_GFX_MAX_DYNAMIC; i++) {
            if (!view.shown[i] || !body[i]) continue;
            dyn[nd].mesh = body[i];
            dyn[nd].vertices = view.actor[i].posed;
            dyn[nd].vertex_count = view.actor[i].mesh.vertex_count;
            dyn[nd].lit = true;
            if (game.teams) {
                dyn[nd].change = true;
                hta_game_team_color(game.units[i].team, dyn[nd].change_color);
            }
            nd++;
        }
        if (itemgpu && nd < HTA_GFX_MAX_DYNAMIC) {
            dyn[nd].mesh = itemgpu;
            dyn[nd].vertices = items_upload ? items.posed : NULL;
            dyn[nd].vertex_count = items.mesh.vertex_count;
            if (items_upload) items_upload--;
            nd++;
        }
        if (trailgpu && nd < HTA_GFX_MAX_DYNAMIC) {
            dyn[nd].mesh = trailgpu;
            dyn[nd].vertices = trails.mesh.vertices;
            dyn[nd].vertex_count = trails.mesh.vertex_count;
            dyn[nd].vertex_color = true;
            nd++;
        }
        for (uint32_t p = 0; p < game.pool_count && nd < HTA_GFX_MAX_DYNAMIC; p++) {
            if (!poolgpu[p]) continue;
            dyn[nd].mesh = poolgpu[p];
            dyn[nd].vertices = game.pools[p].mesh.vertices;
            dyn[nd].vertex_count = game.pools[p].mesh.vertex_count;
            nd++;
        }
        hta_game_held_weapon held[HTA_GAME_MAX_UNITS];
        uint32_t nh = hta_game_view_weapons(&view, &game, -1, held, HTA_GAME_MAX_UNITS);
        static hta_gfx_instance inst[HTA_GFX_MAX_INSTANCES];
        uint32_t ni = 0;
        static hta_vehicle_part vp[HTA_GFX_MAX_INSTANCES];
        uint32_t nv = hta_vehicles_parts(&veh, vp, HTA_GFX_MAX_INSTANCES - HTA_GAME_MAX_UNITS);
        for (uint32_t k = 0; k < nv; k++) {
            if (!vgpu[vp[k].type]) continue;
            inst[ni].mesh = vgpu[vp[k].type];
            memcpy(inst[ni].model, vp[k].model, sizeof(inst[ni].model));
            inst[ni].first_submesh = vp[k].first_submesh;
            inst[ni].submesh_count = vp[k].submesh_count;
            inst[ni].lit = true;
            ni++;
        }
        for (uint32_t k = 0; k < nh; k++) {
            if (!wgpu[held[k].weapon]) continue;
            inst[ni].mesh = wgpu[held[k].weapon];
            memcpy(inst[ni].model, held[k].model, sizeof(inst[ni].model));
            inst[ni].first_submesh = held[k].first_submesh;
            inst[ni].submesh_count = held[k].submesh_count;
            inst[ni].lit = true;
            ni++;
        }
        for (int i = 0; i < HTA_GAME_MAX_DROPS && ni < HTA_GFX_MAX_INSTANCES; i++) {
            const hta_game_drop *dr = &game.drops[i];
            if (!dr->live || !wgpu[dr->weapon]) continue;
            float cy = cosf(dr->yaw), sy = sinf(dr->yaw);
            float m[16] = { cy, sy, 0, 0,   0, 0, 1, 0,   sy, -cy, 0, 0,
                            dr->pos[0], dr->pos[1], dr->pos[2] + 0.05f, 1 };
            inst[ni].mesh = wgpu[dr->weapon];
            memcpy(inst[ni].model, m, sizeof(m));
            inst[ni].first_submesh = inst[ni].submesh_count = 0;
            inst[ni].lit = true;
            ni++;
        }
        uint32_t flag_parts = 0;
        for (int t = 0; t < 2 && game.flag_weapon >= 0; t++) {
            float fm[16];
            uint32_t first[2], count[2];
            if (!wgpu[game.flag_weapon] || !hta_game_flag_model(&game, t, fm)) continue;
            uint32_t parts = hta_game_view_flag_parts(&view, t, first, count);
            for (uint32_t p = 0; p < parts && ni < HTA_GFX_MAX_INSTANCES; p++) {
                inst[ni].mesh = wgpu[game.flag_weapon];
                memcpy(inst[ni].model, fm, sizeof(fm));
                inst[ni].first_submesh = first[p];
                inst[ni].submesh_count = count[p];
                inst[ni].lit = true;
                ni++;
                flag_parts++;
            }
        }
        hta_gfx_set_instances(gfx, inst, ni);
        if (!hta_gfx_draw(gfx, &cam, &scene, world, skygpu, NULL, dyn, nd, NULL, NULL) ||
            !hta_gfx_readback(gfx, rgba, (size_t)W * H * 4u)) {
            fprintf(stderr, "render failed\n"); break;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s_%03d.ppm", prefix, taken);
        ppm(path, rgba, W, H);
        char base[48], act[64];
        hta_game_anim(&game, follow % bots, base, sizeof(base), act, sizeof(act));
        if (game.teams)
            printf("  teams: red %d blue %d, %u flag parts drawn\n",
                   game.team_score[0], game.team_score[1], flag_parts);
        printf("  %s  t=%.1f  %s (%s) at %.1f %.1f %.1f, holding %s, %d instances\n", path, t,
               u->name, base, u->body.pos[0], u->body.pos[1], u->body.pos[2],
               hta_game_held(&game, follow % bots) ? hta_game_held(&game, follow % bots)->label : "-",
               (int)ni);
        taken++;
    }
    printf("simulated      %.1f s, %d kills, %.3f ms per tick\n", t, kills,
           sim_time * 1000.0 / (t / dt));
    if (vehicles) {
        /* How well bots drive: seconds at a wheel, and of those, how many
         * the physics spent refusing a move -- into a wall, a rock or
         * another car. */
        double held = 0.0;
        for (uint32_t c = 0; c < veh.count; c++) held += veh.cars[c].blocked;
        printf("driving        %d entries, %.0f s at a wheel, %.1f s blocked (%.1f%%)\n",
               entries, driven, held, driven > 0.0 ? 100.0 * held / driven : 0.0);
    }
    int32_t order[HTA_GAME_MAX_UNITS];
    uint32_t ns = hta_game_standings(&game, order, HTA_GAME_MAX_UNITS);
    for (uint32_t i = 0; i < ns; i++)
        printf("  %-12s %3d\n", game.units[order[i]].name, game.units[order[i]].score);

    free(rgba);
    for (int i = 0; i < bots; i++) if (body[i]) hta_gfx_mesh_free(gfx, body[i]);
    for (uint32_t w = 0; w < game.weapon_count; w++) if (wgpu[w]) hta_gfx_mesh_free(gfx, wgpu[w]);
    for (uint32_t p = 0; p < game.pool_count; p++) if (poolgpu[p]) hta_gfx_mesh_free(gfx, poolgpu[p]);
    if (itemgpu) hta_gfx_mesh_free(gfx, itemgpu);
    if (skygpu) hta_gfx_mesh_free(gfx, skygpu);
    hta_gfx_mesh_free(gfx, world);
    hta_gfx_destroy(gfx);
    hta_game_view_free(&view);
    hta_game_free(&game);
    hta_pickups_free(&items);
    hta_nav_free(&nav);
    hta_collision_free(&col);
    hta_bsp_free(&cm); hta_bsp_free(&sky); hta_bsp_free(&mesh);
    free(bm_data); free(map_data);
    return 0;
}
