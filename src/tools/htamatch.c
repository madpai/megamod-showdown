/* htamatch -- a bot game of Slayer on the owner's Blood Gulch, rendered
 * offscreen from behind one of the bots. For looking at bots, their
 * animations and their weapons without a phone.
 *
 *   htamatch <bloodgulch.map> [--bots N] [--skill 0-3] [--seconds S] [--mode ffa|team|ctf]
 *            [--shots N] [--every S] [--follow UNIT] [--out prefix]
 *            [--width W] [--height H] [--vehicles] [--seed N] [--oalmap PACKAGE]
 *
 * Simulates S seconds, then keeps simulating and takes a frame every
 * `--every` seconds from a camera behind and above the followed unit.
 * `--vehicles` makes the map's vehicles live for the bots to take; every
 * getting in, getting out and wreck is printed, and at the end how long
 * bots drove and how much of it their cars spent blocked. `--seed` plays
 * a different match of the same setup; judge a change on several.
 * `--oalmap` plays on an imported map instead: its world and starts, Blood
 * Gulch's weapons spread over it, no vehicles. `--character PKG` (up to 8)
 * dresses the bots in imported bodies, in turn; `--weapon PKG` adds an
 * imported weapon to the roster; `--classes` turns custom classes on (bots
 * make up a class each life, which is how imported weapons reach them).
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
#include "game/external_world.h"
#include "asset/external_map.h"
#include "asset/oal_asset.h"
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
    const char *oalmap = NULL;
    const char *char_paths[HTA_GAME_MAX_CHARACTERS], *weap_paths[16];
    int nchar = 0, nweap = 0;
    bool classes = false;
    const char *give = NULL;
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
        else if (!strcmp(argv[i], "--oalmap") && i + 1 < argc) oalmap = argv[++i];
        else if (!strcmp(argv[i], "--character") && i + 1 < argc && nchar < HTA_GAME_MAX_CHARACTERS) char_paths[nchar++] = argv[++i];
        else if (!strcmp(argv[i], "--weapon") && i + 1 < argc && nweap < 16) weap_paths[nweap++] = argv[++i];
        else if (!strcmp(argv[i], "--classes")) classes = true;
        else if (!strcmp(argv[i], "--give") && i + 1 < argc) { give = argv[++i]; classes = true; }
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
    static hta_external_map ext;
    if (oalmap) {
        if (!hta_external_map_load(oalmap, &ext, err, sizeof(err))) {
            fprintf(stderr, "oalmap: %s\n", err); return 1;
        }
        mesh = ext.mesh;
        memset(&ext.mesh, 0, sizeof(ext.mesh));
        vehicles = false; ride = -1;
        printf("oalmap         %u triangles, %u starts\n", mesh.index_count / 3, ext.spawn_count);
    } else if (!hta_bsp_load_first(&cache, &mesh, err, sizeof(err)) ||
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
    if (!oalmap)
        hta_scenario_add_objects_excluding(&mesh, &cache, bmp, veh.skip, sizeof(veh.skip), err, sizeof(err));
    hta_sky_load(&sky, &cache, bmp, err, sizeof(err));
    hta_collision col = {0};
    if (oalmap) {
        hta_bsp_mesh solid;
        hta_external_map_collision_view(&mesh, &ext, &solid);
        if (!hta_collision_build_cells(&col, &solid, HTA_COLLISION_CELLS_IMPORTED)) { fprintf(stderr, "collision failed\n"); return 1; }
    } else if (!hta_bsp_load_collision(&cache, &cm, err, sizeof(err)) ||
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
    hta_nav_params prm = { game.phys.radius, game.phys.coll_stand, game.phys.max_slope, 1.0f,
                           oalmap ? HTA_NAV_CELL_FINE : 0.0f };
    if (!hta_nav_build(&nav, &col, bmin, bmax, &prm, err, sizeof(err))) {
        fprintf(stderr, "nav: %s\n", err); return 1;
    }
    printf("nav            %s (%.0f ms)\n", err, (hta_time_seconds() - t0) * 1000.0);
    if (veh.loaded) {
        col.instances = veh.inst; col.instance_count = veh.count;
        hta_game_attach_vehicles(&game, &veh, bmp);
    }
    game.nav = &nav;
    uint8_t *playable = NULL;
    if (oalmap) {
        if (!hta_nav_main_from_spawns(&nav, ext.spawns, ext.spawn_count))
            printf("nav            no start on the grid\n");
        uint32_t np = 0;
        playable = hta_nav_playable(&nav, ext.spawns, ext.spawn_count, &np);
        printf("playable       %u of %u nodes reachable from a start and back\n", np, nav.node_count);
        /* HTA_DEBUG_PROBE="x0 x1 y z": ground and headroom along x. */
        /* HTA_DEBUG_PROBEY="x y0 y1 z": the same along y. */
        if (getenv("HTA_DEBUG_PROBEY")) {
            float x, y0, y1, z;
            if (sscanf(getenv("HTA_DEBUG_PROBEY"), "%f %f %f %f", &x, &y0, &y1, &z) == 4)
                for (float y = y0; y <= y1 + 1e-4f; y += 0.025f) {
                    float g = -99.0f;
                    bool on = hta_collision_ground(&col, x, y, z, &g);
                    float px = x, py = y;
                    if (on) hta_collision_depenetrate(&col, &px, &py, g, prm.height, prm.radius);
                    printf("    probey y %.3f: ground %.3f pushed %.3f %.3f\n", y, g, px - x, py - y);
                }
        }
        if (getenv("HTA_DEBUG_PROBE")) {
            float x0, x1, y, z;
            if (sscanf(getenv("HTA_DEBUG_PROBE"), "%f %f %f %f", &x0, &x1, &y, &z) == 4)
                for (float x = x0; x <= x1 + 1e-4f; x += 0.05f) {
                    float g = -99.0f, t = 0.0f, hit[3], nrm[3];
                    bool on = hta_collision_ground(&col, x, y, z, &g);
                    float o[3] = { x, y, g + 0.05f }, up[3] = { 0, 0, 1 };
                    bool roof = on && hta_collision_ray(&col, o, up, 3.0f, &t, hit, nrm);
                    int layers = 0;
                    for (float from = z + 20.0f; layers < 16; layers++) {
                        float lz;
                        if (!hta_collision_ground(&col, x, y, from, &lz)) break;
                        if (fabsf(lz - g) < 1e-3f) { layers++; break; }
                        from = lz - 0.3f;
                    }
                    printf("    (floor is surface %d from the top)\n", layers);
                    float px = x, py = y;
                    if (on) hta_collision_depenetrate(&col, &px, &py, g, prm.height, prm.radius);
                    printf("    probe x %.2f: ground %s %.3f, headroom %.2f, pushed to %.2f %.2f\n", x, on ? "at" : "none", g,
                           roof ? t : 3.0f, px, py);
                }
        }
        /* HTA_DEBUG_NAV: can the first start walk to each of the others? */
        if (getenv("HTA_DEBUG_NAV")) {
            static uint32_t route[65536];
            uint32_t first = (uint32_t)atoi(getenv("HTA_DEBUG_NAV")) % ext.spawn_count;
            uint32_t from = hta_nav_nearest(&nav, ext.spawns[first].position, 1.0f);
            {
                uint32_t *next = malloc(nav.node_count * sizeof(uint32_t));
                uint32_t reach = next ? hta_nav_field(&nav, from, next) : 0;
                float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
                for (uint32_t i = 0; next && i < nav.node_count; i++) {
                    if (next[i] == HTA_NAV_NONE) continue;
                    float at[3]; hta_nav_pos(&nav, i, at);
                    for (int c = 0; c < 3; c++) { if (at[c] < lo[c]) lo[c] = at[c]; if (at[c] > hi[c]) hi[c] = at[c]; }
                    if (getenv("HTA_DEBUG_NAV_NODES")) printf("      node %.2f %.2f %.2f\n", at[0], at[1], at[2]);
                }
                printf("    %u nodes can reach start %u, within %.1f %.1f %.1f .. %.1f %.1f %.1f\n",
                       reach, first, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
                free(next);
            }
            for (uint32_t k = 1; k < ext.spawn_count; k++) {
                uint32_t to = hta_nav_nearest(&nav, ext.spawns[k].position, 1.0f);
                uint32_t len = from == HTA_NAV_NONE || to == HTA_NAV_NONE ? 0
                             : hta_nav_path(&nav, from, to, route, 65536, 2000000);
                uint32_t back = from == HTA_NAV_NONE || to == HTA_NAV_NONE ? 0
                              : hta_nav_path(&nav, to, from, route, 65536, 2000000);
                printf("    start %u -> %u (%.1f %.1f %.1f): %u nodes there, %u back\n", first, k,
                       ext.spawns[k].position[0], ext.spawns[k].position[1], ext.spawns[k].position[2], len, back);
            }
        }
        hta_game_use_external(&game, ext.spawns, ext.spawn_count, &nav, playable);
        for (int t = 0; t < 2; t++)
            if (ext.has_flag[t]) hta_game_external_flag(&game, t, ext.flag[t], &nav, playable);
        if (getenv("HTA_DEBUG_NAV"))
            for (int t = 0; t < 2; t++) {
                uint32_t *next = malloc(nav.node_count * sizeof(uint32_t));
                uint32_t stand = hta_nav_nearest(&nav, game.flags[t].home, 2.0f);
                uint32_t reach = next && stand != HTA_NAV_NONE ? hta_nav_field(&nav, stand, next) : 0;
                uint32_t from_starts = 0;
                for (uint32_t k = 0; next && k < ext.spawn_count; k++) {
                    uint32_t s0 = hta_nav_nearest(&nav, ext.spawns[k].position, 1.0f);
                    if (s0 != HTA_NAV_NONE && next[s0] != HTA_NAV_NONE) from_starts++;
                }
                printf("    flag %d: %u nodes can walk to it, %u of %u starts\n", t, reach, from_starts, ext.spawn_count);
                free(next);
            }
        printf("flags          red %.1f %.1f %.1f%s, blue %.1f %.1f %.1f%s\n",
               game.flags[0].home[0], game.flags[0].home[1], game.flags[0].home[2], ext.has_flag[0] ? " (map's)" : "",
               game.flags[1].home[0], game.flags[1].home[1], game.flags[1].home[2], ext.has_flag[1] ? " (map's)" : "");
    }
    if (hta_pickups_load(&items, &cache)) {
        if (oalmap) hta_pickups_relocate(&items, &nav, playable);
        hta_pickups_build(&items, &cache, bmp, err, sizeof(err));
        game.items = &items;
    }
    static hta_oal_asset chars[HTA_GAME_MAX_CHARACTERS], weaps[16];
    int32_t char_index[HTA_GAME_MAX_CHARACTERS];
    for (int k = 0; k < nchar; k++) {
        if (!hta_oal_load(char_paths[k], &chars[k], err, sizeof(err))) { fprintf(stderr, "character: %s\n", err); return 1; }
        char_index[k] = hta_game_add_character(&game, &chars[k]);
        printf("character      %s: %u verts, %u clips\n", chars[k].name, chars[k].models[0].mesh.vertex_count,
               chars[k].models[0].clip_count);
    }
    for (int k = 0; k < nweap; k++) {
        if (!hta_oal_load(weap_paths[k], &weaps[k], err, sizeof(err))) { fprintf(stderr, "weapon: %s\n", err); return 1; }
        int32_t wi = hta_game_add_imported_weapon(&game, &weaps[k]);
        printf("weapon         %s on %s: roster %d, %.1f rounds/s, mag %d, damage x%.2f\n", weaps[k].display,
               weaps[k].base, wi, wi >= 0 ? game.weapons[wi].def.rof : 0.0f,
               wi >= 0 ? game.weapons[wi].def.rounds_loaded_max : 0, wi >= 0 ? game.weapons[wi].damage_scale : 0.0f);
    }
    game.classes = classes;
    if (!hta_game_set_mode(&game, mode)) printf("mode           not on this map: Slayer\n");
    for (int i = 0; i < bots; i++)
        hta_game_add(&game, ride >= 0 && i < 2 ? HTA_UNIT_REMOTE : HTA_UNIT_BOT, NULL,
                     HTA_TEAM_AUTO);
    hta_game_set_skill(&game, (uint8_t)skill);
    for (int i = 0; i < bots && nchar; i++) game.units[i].character = (int8_t)char_index[i % nchar];
    if (give) {
        /* Everyone spawns with the named weapon (its display name). */
        int32_t gw = -1;
        for (uint32_t w = 0; w < game.weapon_count && gw < 0; w++)
            if (strstr(game.weapons[w].display, give)) gw = (int32_t)w;
        printf("give           %s -> roster %d\n", give, gw);
        for (int i = 0; i < bots; i++) hta_game_set_loadout(&game, i, gw, -1);
    }
    if (!hta_game_view_load(&view, &game, bmp, (uint32_t)bots, err, sizeof(err))) {
        fprintf(stderr, "view: %s\n", err); return 1;
    }
    printf("view           %s\n", err);
    if (getenv("HTA_DEBUG_WEAPONS"))
        for (uint32_t w = 0; w < game.weapon_count; w++)
            if (view.have_weapon[w]) {
                float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
                const hta_bsp_mesh *wm = &view.weapon_mesh[w];
                for (uint32_t k = 0; k < wm->vertex_count; k++)
                    for (int e = 0; e < 3; e++) {
                        if (wm->vertices[k].pos[e] < lo[e]) lo[e] = wm->vertices[k].pos[e];
                        if (wm->vertices[k].pos[e] > hi[e]) hi[e] = wm->vertices[k].pos[e];
                    }
                printf("  weapon %2u %-16s bounds %.3f %.3f %.3f .. %.3f %.3f %.3f\n", w, game.weapons[w].display,
                       lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
            }
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
        body[i] = hta_gfx_mesh_upload_dynamic_world(gfx, hta_game_view_body_mesh(&view, &game, (uint32_t)i),
                                                    err, sizeof(err));
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
    if (oalmap) {   /* no lightmaps: the explorer's even daylight */
        scene.light_dir[0] = 0.35f; scene.light_dir[1] = 0.4f; scene.light_dir[2] = 0.85f;
        for (int k = 0; k < 3; k++) { scene.light_color[k] = 1.0f; scene.ambient[k] = 0.7f; }
    }
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
        /* HTA_DEBUG_TRACE=unit: where it is, ten times a second. */
        if (getenv("HTA_DEBUG_TRACE")) {
            int tu = atoi(getenv("HTA_DEBUG_TRACE"));
            static float next_trace;
            if (t >= next_trace && tu >= 0 && tu < (int)game.unit_count) {
                next_trace = t + 0.1f;
                const hta_unit *u = &game.units[tu];
                printf("    trace %.1f %s at %.2f %.2f %.2f ground %d alive %d\n", t, u->name,
                       u->body.pos[0], u->body.pos[1], u->body.pos[2], u->body.on_ground, u->alive);
            }
        }
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
            if (e.kind == HTA_EV_FLAG) printf("  %6.1f  flag: %s\n", t, e.text);
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
        /* HTA_SIDE_CAM: look from the unit's left side instead. */
        float cam_yaw = u->eye.yaw + (getenv("HTA_SIDE_CAM") ? 1.5707963f : 0.0f);
        float fx = cosf(cam_yaw), fy = sinf(cam_yaw);
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
            dyn[nd].vertices = hta_game_view_body_vertices(&view, &game, (uint32_t)i);
            dyn[nd].vertex_count = hta_game_view_body_mesh(&view, &game, (uint32_t)i)->vertex_count;
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
            hta_game_view_weapon_space(&game, dr->weapon, m);
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
        if (getenv("HTA_DEBUG_UNITS") && taken == 0)
            for (uint32_t i = 0; i < items.count; i++)
                printf("    item %u %s at %.2f %.2f %.2f present %d\n", i,
                       items.spawn[i].choice_count ? items.spawn[i].choice[0].path : "-",
                       items.spawn[i].position[0], items.spawn[i].position[1],
                       items.spawn[i].position[2], items.slot[i].present);
        if (getenv("HTA_DEBUG_UNITS"))
            for (int i = 0; i < bots; i++)
                printf("    unit %d team %d %s at %.1f %.1f %.1f alive %d; brain: target %d item %d path %u/%u wait %.1f\n",
                       i, game.units[i].team, game.units[i].name, game.units[i].body.pos[0],
                       game.units[i].body.pos[1], game.units[i].body.pos[2], game.units[i].alive,
                       game.brains[i].target, game.brains[i].goal_item, game.brains[i].path_i,
                       game.brains[i].path_len, game.brains[i].plan_wait);
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
    free(playable);
    hta_external_map_free(&ext);
    free(bm_data); free(map_data);
    return 0;
}
