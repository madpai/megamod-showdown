/* Loading a match without a GPU (app/match_load.h). Moved from the Android
 * loop's load_map, world_nav and start_game; the code is theirs. */
#include "app/match_load.h"
#include "app/content.h"
#include "app/session.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hta_match_nav_props(hta_session *s)
{
    bool first = s->nav.props_version == 0;
    if (s->nav.built && s->wfx.ready &&
        hta_nav_sync_props(&s->nav, &s->wfx.props, s->game.phys.radius) && first && s->nav.blocked_nodes)
        hta_log("[nav] %u nodes under %u props", s->nav.blocked_nodes, s->wfx.props.count);
}

/* The imported world's walkable grid, built once and kept in writable_dir
 * like Blood Gulch's. The region the starts stand in is the map. */
static void world_nav(hta_session *s, const char *writable_dir)
{
    hta_player_physics phys;
    char err[HTA_ERRLEN];
    hta_player_physics_defaults(&phys);
    hta_player_physics_load(&phys, &s->cache, err, sizeof(err));
    hta_collision_set_slope(&s->col, phys.max_slope);
    hta_nav_params prm = { phys.radius, phys.coll_stand, phys.max_slope, 1.0f, HTA_NAV_CELL_FINE };
    uint32_t key = s->cache.crc32 ^ s->world_ext.key ^ (uint32_t)(prm.radius * 1e4f) ^ (uint32_t)(prm.cell * 1e5f) ^
                   ((uint32_t)(prm.height * 1e4f) << 8) ^ ((uint32_t)(prm.max_slope * 1e4f) << 16);
    char navpath[600] = "";
    if (writable_dir && *writable_dir)
        snprintf(navpath, sizeof(navpath), "%s/nav-%08x.bin", writable_dir, key);
    double t0 = hta_time_seconds();
    if (!(navpath[0] && hta_nav_load(&s->nav, navpath, key))) {
        if (!hta_nav_build(&s->nav, &s->col, s->mesh.bounds_min, s->mesh.bounds_max,
                           &prm, err, sizeof(err))) {
            hta_log("[world] nav failed (%s): no items, bots stand still", err);
            return;
        }
        if (navpath[0] && !hta_nav_save(&s->nav, navpath, key))
            hta_log("[world] could not keep the nav grid at %s", navpath);
    }
    if (!hta_nav_main_from_spawns(&s->nav, s->world_ext.spawns, s->world_ext.spawn_count))
        hta_log("[world] no start stands on the nav grid");
    uint32_t playable = 0;
    free(s->world_playable);
    s->world_playable = hta_nav_playable(&s->nav, s->world_ext.spawns, s->world_ext.spawn_count, &playable);
    hta_log("[world] %u of %u nav nodes reachable from a start and back", playable, s->nav.node_count);
    hta_log("[world] nav: %u nodes in %.0f ms", s->nav.node_count,
            (hta_time_seconds() - t0) * 1000.0);
}

bool hta_match_load_world(hta_session *s, const hta_fs *fs, const char *writable_dir)
{
    if (!s->map_data) {
        snprintf(s->status, sizeof(s->status), "no Trial map");
        return false;
    }
    char err[HTA_ERRLEN];
    double t0 = hta_time_seconds();
    if (!hta_cache_open(&s->cache, s->map_data, s->map_size, err, sizeof(err))) {
        hta_log("[assets] cache rejected: %s", err);
        snprintf(s->status, sizeof(s->status), "bad cache: %s", err);
        return false;
    }
    hta_log("[assets] %s | engine %u (%s) | %u tags | %s layout",
            s->cache.name, s->cache.engine, hta_engine_name(s->cache.engine),
            s->cache.tag_count, s->cache.is_demo_layout ? "Trial" : "retail");

    if (s->world[0]) {
        if (!hta_session_load_world(s, fs, err, sizeof(err))) {
            hta_log("[world] %s: %s", s->world, err);
            snprintf(s->status, sizeof(s->status), "%s: %s", s->world, err);
            return false;
        }
    } else if (!hta_bsp_load_first(&s->cache, &s->mesh, err, sizeof(err))) {
        hta_log("[assets] BSP extraction failed: %s", err);
        snprintf(s->status, sizeof(s->status), "bsp: %s", err);
        return false;
    }
    double t1 = hta_time_seconds();
    hta_log("[assets] BSP: %u verts, %u tris, %u submeshes in %.1f ms",
            s->mesh.vertex_count, s->mesh.index_count / 3, s->mesh.submesh_count,
            (t1 - t0) * 1000.0);
    hta_log("[assets] bounds (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)",
            s->mesh.bounds_min[0], s->mesh.bounds_min[1], s->mesh.bounds_min[2],
            s->mesh.bounds_max[0], s->mesh.bounds_max[1], s->mesh.bounds_max[2]);

    /* sounds.map and bitmaps.map, when the platform mapped them. The
     * bitmaps' resource map outlives the load: swapping weapons re-decodes
     * their art. */
    if (s->sounds_data) {
        if (hta_resource_open_typed(&s->sounds_rm, s->sounds_data, s->sounds_size,
                                    HTA_RESOURCE_SOUNDS, err, sizeof(err)))
            hta_log("[assets] sounds.map %zu bytes from %s", s->sounds_size, s->sounds_path);
        else
            hta_log("[assets] sounds.map rejected: %s", err);
    } else {
        hta_log("[assets] no sounds.map -- the game will be silent. Copy it next to bloodgulch.map");
    }
    memset(&s->bitmaps_rm, 0, sizeof(s->bitmaps_rm));
    if (s->bitmaps_data) {
        if (hta_resource_open(&s->bitmaps_rm, s->bitmaps_data, s->bitmaps_size, err, sizeof(err)))
            hta_log("[assets] bitmaps.map %zu bytes from %s", s->bitmaps_size, s->bitmaps_path);
        else
            hta_log("[assets] bitmaps.map rejected: %s", err);
    } else {
        hta_log("[assets] no bitmaps.map -- world will stay untextured. Copy it next to bloodgulch.map");
    }
    if (s->world_loaded)
        hta_log("[world] %u textures from the package", s->mesh.texture_count);
    else if (!hta_bsp_load_textures(&s->cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL, &s->mesh, err, sizeof(err)))
        hta_log("[assets] texture load: %s", err);
    else
        hta_log("[assets] textures: %u unique (albedos+lightmaps)", s->mesh.texture_count);

    /* Blood Gulch's vehicles are parked in Blood Gulch; an imported map
     * has none. */
    if (!s->world_loaded && hta_vehicles_load(&s->vehicles, &s->cache,
            s->bitmaps_rm.data ? &s->bitmaps_rm : NULL, err, sizeof(err)))
        hta_log("[vehicles] %s", err);

    if (!s->world_loaded && hta_bsp_load_collision(&s->cache, &s->coll_mesh, err, sizeof(err))) {
        s->have_coll = true;
        if (hta_scenario_add_collision_excluding(&s->coll_mesh, &s->cache,
                s->vehicles.skip, HTA_VEHICLE_PLACEMENTS, err, sizeof(err)))
            hta_log("[assets] %s", err);
        if (!hta_collision_build(&s->col, &s->coll_mesh))
            hta_log("[assets] collision BSP grid failed; %s", err);
        else
            hta_log("[assets] collision BSP %u verts / %u tris, grid %ux%u",
                    s->coll_mesh.vertex_count, s->coll_mesh.index_count / 3,
                    s->col.nx, s->col.ny);
    } else if (s->world_loaded) {
        /* The package's solid triangles: its non-solid props are only drawn. */
        hta_bsp_mesh solid;
        hta_external_map_collision_view(&s->mesh, &s->world_ext, &solid);
        if (!hta_collision_build_cells(&s->col, &solid, HTA_COLLISION_CELLS_IMPORTED))
            hta_log("[world] collision grid failed to build; player will free-fly");
        else
            hta_log("[world] collision %u of %u triangles solid", solid.index_count / 3,
                    s->mesh.index_count / 3);
    } else {
        hta_log("[assets] collision BSP: %s — using render mesh", err);
        if (!hta_collision_build(&s->col, &s->mesh))
            hta_log("[assets] collision grid failed to build; player will free-fly");
        else
            hta_log("[assets] collision grid %ux%u cells (render mesh)", s->col.nx, s->col.ny);
    }

    /* An imported world needs its walkable grid now, before the items: they
     * are spread over it. */
    if (s->world_loaded) world_nav(s, writable_dir);

    /* What the map leaves lying about. Every position, facing, respawn time
     * and weighted choice is the scenario's own -- on an imported map, all
     * but the position. */
    if (hta_pickups_load(&s->items, &s->cache)) {
        if (s->world_loaded) hta_pickups_relocate(&s->items, &s->nav, s->world_playable);
        char ierr[HTA_ERRLEN];
        if (hta_pickups_build(&s->items, &s->cache,
                              s->bitmaps_rm.data ? &s->bitmaps_rm : NULL,
                              ierr, sizeof(ierr)))
            hta_log("[items] %s", ierr);
        else
            hta_log("[items] %u placement(s) but no geometry: %s",
                    s->items.count, ierr);
    }

    /* Which materials this map is actually made of. Blood Gulch is four:
     * sand, stone, thick metal and a little plastic. Knowing them turns 33
     * impact effects per weapon into four, which is what makes per-material
     * impact particles affordable at all. */
    s->map_material_count = 0;
    if (s->col.built && s->col.tri_material) {
        uint8_t seen[33];
        memset(seen, 0, sizeof(seen));
        for (uint32_t i = 0; i < s->col.tri_count; i++) {
            uint8_t m = s->col.tri_material[i];
            if (m < 33u) seen[m] = 1;
        }
        for (uint32_t m = 0; m < 33u; m++)
            if (seen[m] && s->map_material_count < 33u)
                s->map_material[s->map_material_count++] = (uint8_t)m;
        hta_log("[assets] %u material(s) in this map", s->map_material_count);
    }

    if (!s->world_loaded &&
        hta_scenario_add_objects_excluding(&s->mesh, &s->cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL,
            s->vehicles.skip, HTA_VEHICLE_PLACEMENTS, err, sizeof(err)))
        hta_log("[assets] %s  (now %u verts / %u submeshes)", err,
                s->mesh.vertex_count, s->mesh.submesh_count);
    if (!s->have_coll && !s->world_loaded)
        hta_collision_rebind(&s->col, s->mesh.vertices, s->mesh.indices);

    /* Vehicles are placed rigid grids: everything that asks the world a
     * question -- feet, bullets, grenades, cameras -- sees them where they
     * are now, and moving one costs a matrix. */
    if (s->vehicles.loaded) {
        s->col.instances = s->vehicles.inst;
        s->col.instance_count = s->vehicles.count;
    }
    s->bitmaps_ok = (s->bitmaps_rm.data != NULL);
    return true;
}

bool hta_match_start(hta_session *s, const char *writable_dir, bool local_player)
{
    char err[HTA_ERRLEN];
    const hta_resource_map *bm = s->bitmaps_ok ? &s->bitmaps_rm : NULL;
    if (!hta_game_load(&s->game, &s->cache, bm, &s->col, err, sizeof(err))) {
        hta_log("[game] not playable: %s", err);
        return false;
    }
    hta_log("[game] %s", err);
    /* Imported weapons join the roster before anything reads it; the
     * characters anyone may wear; and whether classes are on. */
    for (uint32_t k = 0; k < s->imp_weap_count; k++) {
        int32_t r = hta_game_add_imported_weapon(&s->game, &s->imp_weap[k]);
        hta_log("[imported] weapon %s on %s: roster %d", s->imp_weap[k].display, s->imp_weap[k].base, (int)r);
    }
    for (uint32_t k = 0; k < s->imp_char_count; k++) hta_game_add_character(&s->game, &s->imp_char[k]);
    s->game.classes = s->classes;
    s->game.allow_duplicate_heroes = s->allow_duplicate_heroes;
    if (s->world_loaded) {
        hta_game_use_external(&s->game, s->world_ext.spawns, s->world_ext.spawn_count,
                              s->nav.built ? &s->nav : NULL, s->world_playable);
        for (int t = 0; t < 2; t++)
            if (s->world_ext.has_flag[t])
                hta_game_external_flag(&s->game, t, s->world_ext.flag[t],
                                       s->nav.built ? &s->nav : NULL, s->world_playable);
        if (s->nav.built) s->game.nav = &s->nav;
        hta_log("[world] %u starts; flags %s", s->game.spawn_count,
                s->game.flags[0].present && s->game.flags[1].present ? "at the team starts" : "none");
    }
    if (s->vehicles.loaded) {
        uint32_t before = s->game.weapon_count;
        hta_game_attach_vehicles(&s->game, &s->vehicles, bm);
        /* A client draws the host's vehicles and runs none of its own. */
        s->game.simulate_vehicles = !s->net_enabled || s->net_hosting;
        hta_vehicles_roster(&s->vehicles, s->game.simulate_vehicles ? s->vehicle_roster
                                                                    : HTA_VROSTER_NONE);
        uint32_t active = 0;
        for (uint32_t i = 0; i < s->vehicles.count; i++) active += s->vehicles.cars[i].active;
        hta_log("[vehicles] %u of %u placed (roster %d); %u vehicle trigger(s), %u pools",
                active, s->vehicles.count, s->vehicle_roster, s->game.weapon_count - before,
                s->game.pool_count);
    }
    s->my_car = s->my_seat = -1;
    s->game.simulate_drops = !s->net_enabled || s->net_hosting;
    if (!hta_game_set_mode(&s->game, (hta_game_mode)s->game_mode))
        hta_log("[game] mode %d is not playable on this map: Slayer", s->game_mode);
    s->carried_flag = -1;
    s->game.score_limit = s->score_limit;
    s->game.time_limit = (float)s->time_limit_min * 60.0f;
    s->game.respawn_time = s->respawn_delay;
    int bots = s->net_enabled && !s->net_hosting ? 0 : s->bot_count;
    if (bots > 0 && !s->nav.built) {
        double t0 = hta_time_seconds();
        /* The walkable grid is the ground, not the vehicles parked on it:
         * they move, and bots walk round them as they find them. */
        hta_collision nav_col = s->col;
        nav_col.instances = NULL;
        nav_col.instance_count = 0;
        hta_nav_params prm = { s->game.phys.radius, s->game.phys.coll_stand,
                               s->game.phys.max_slope, 1.0f };
        /* Built once per map and physics, then read back from app storage. */
        char navpath[600] = "";
        uint32_t key = s->cache.crc32 ^ (uint32_t)(prm.radius * 1e4f) ^
                       ((uint32_t)(prm.height * 1e4f) << 8) ^ ((uint32_t)(prm.max_slope * 1e4f) << 16);
        if (writable_dir && *writable_dir)
            snprintf(navpath, sizeof(navpath), "%s/nav-%08x.bin", writable_dir, key);
        if (navpath[0] && hta_nav_load(&s->nav, navpath, key)) {
            s->game.nav = &s->nav;
            hta_log("[game] nav: %u nodes from %s in %.0f ms", s->nav.node_count, navpath,
                    (hta_time_seconds() - t0) * 1000.0);
        } else if (hta_nav_build(&s->nav, &nav_col, s->mesh.bounds_min, s->mesh.bounds_max,
                          &prm, err, sizeof(err))) {
            if (navpath[0] && !hta_nav_save(&s->nav, navpath, key))
                hta_log("[game] could not keep the nav grid at %s", navpath);
            s->game.nav = &s->nav;
            hta_log("[game] nav: %s in %.0f ms", err, (hta_time_seconds() - t0) * 1000.0);
        } else {
            hta_log("[game] nav failed (%s): bots will stand still", err);
        }
    }
    if (s->items.loaded) s->game.items = &s->items;
    s->me = local_player ? hta_game_add(&s->game, HTA_UNIT_LOCAL, "Player", HTA_TEAM_AUTO) : -1;
    for (int i = 0; i < bots && i < HTA_GAME_MAX_UNITS - 1; i++)
        hta_game_add(&s->game, HTA_UNIT_BOT, NULL, HTA_TEAM_AUTO);
    hta_game_set_skill(&s->game, (uint8_t)s->bot_skill);
    /* Looks: yours from Settings; bots take turns through the imported
     * bodies when Settings asks for them. */
    for (uint32_t k = 0; s->me >= 0 && k < s->game.character_count; k++)
        if (!strcmp(s->game.characters[k]->name, s->my_character))
            hta_game_assign_character(&s->game, s->me, (int32_t)k);
    if (s->bots_imported && s->game.character_count)
        for (uint32_t i = 0, n = 0; i < s->game.unit_count; i++)
            if (s->game.units[i].kind == HTA_UNIT_BOT)
                hta_game_assign_character(&s->game, (int32_t)i, (int32_t)(n++ % s->game.character_count));
    return true;
}

void hta_match_begin(hta_session *s)
{
    s->game_on = true;
    if (s->net_enabled) {
        /* Both phones must stand in the same world, not only the same Trial. */
        uint32_t crc = s->cache.crc32 ^ (s->world_loaded ? s->world_ext.key : 0u);
        s->net.map_crc=crc;
        if (s->net_hosting) s->host_server.map_crc=crc;
    }
    hta_match_nav_props(s);
    hta_game_start(&s->game);
    s->world_round = 1;
}
