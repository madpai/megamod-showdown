/* MEGAMOD JOIN: the PC joins a LAN match -- a phone's, or megamod-fakehost.
 * Protocol v9, as a phone joiner: the host drives our unit from CONTROL,
 * and its world (every player and bot, kills and gibs, blasts, props,
 * scores) is drawn here with the engine's effects, weather and sound.
 *
 *   megamod-join <host> [port] [--map bloodgulch.map] [--oalmap map.oalmap]
 *                [--preset P] [--weather W] [--auto SECONDS] [--shot out.ppm]
 *
 * --map is the owner's own Trial map: the Spartan everyone wears, and the
 * map check a phone host makes (its CRC, with the .oalmap's key when the
 * host plays an imported map). Without it the world is the .oalmap alone,
 * players are drawn as stand-in boxes, and the map check is skipped (a
 * host accepts a joiner that names no map).
 *
 * Keys: WASD, mouse (click to capture, Esc to release / quit), LMB fire,
 * RMB zoom, G grenade, F melee, R reload, E pick up, Q ability, 1/2
 * weapon, Space jump, C crouch, F2 preset, F3 weather.
 *
 * What a PC does not draw yet (the phone does): projectiles in flight,
 * vehicles, dropped weapons, flags, the HUD's health, and first-person
 * weapons. The title bar carries the score, ping and kill feed. */
#define _POSIX_C_SOURCE 200809L
#include "asset/bitmap.h"
#include "asset/bsp.h"
#include "asset/cache.h"
#include "asset/biped.h"
#include "asset/external_map.h"
#include "asset/model.h"
#include "engine/actor.h"
#include "engine/scene_light.h"
#include "game/net_view.h"
#include "game/world_fx_audio.h"
#include "game/world_fx_gpu.h"
#include "gfx/gfx.h"
#include "platform/audio_sdl.h"
#include "platform/desktop_sdl.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n);
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
    fclose(f);
    if (p) *size = (size_t)n;
    return p;
}

static uint32_t find_tag(const hta_cache *c, uint32_t cls, const char *part)
{
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        char path[256];
        if (!hta_cache_tag(c, i, &t) || t.primary_class != cls) continue;
        hta_cache_tag_path(c, &t, path, sizeof(path));
        if (strstr(path, part)) return t.tag_id;
    }
    return 0;
}

typedef struct {
    /* the world */
    hta_cache cache; bool have_cache;
    hta_resource_map bm; uint8_t *map_data, *bm_data;
    hta_bsp_mesh mesh, sky, coll_mesh;
    hta_external_map ext; bool have_ext;
    hta_collision col;
    hta_collision_instance merged[1024];
    hta_instance_index col_index;
    uint32_t map_crc;
    hta_spawn_point spawns[64]; uint32_t spawn_count;
    /* the Spartan, one per entity slot */
    uint32_t biped, rifle;
    hta_actor actor[HTA_NET_MAX_ENTITIES];
    hta_gfx_mesh *actor_gpu[HTA_NET_MAX_ENTITIES];
    bool actor_loaded[HTA_NET_MAX_ENTITIES], actor_dead[HTA_NET_MAX_ENTITIES];
    /* stand-ins when there is no Trial map */
    hta_bsp_mesh boxes; hta_vertex *box_verts; uint32_t box_slots;
    hta_gfx_mesh *gpu_boxes;
    /* the match */
    hta_net_client net;
    hta_net_view view;
    hta_player player;
    hta_camera cam;
    hta_scene scene;
    hta_gfx_settings video;
    hta_world_fx wfx;
    hta_audio audio;
    hta_wfx_audio wfx_audio;
    hta_gfx_mesh *gpu_world, *gpu_sky;
    uint32_t rng;
    int weather;
} join;

static bool load_world(join *j, const char *map_path, const char *oalmap, char *err, size_t errlen)
{
    if (map_path) {
        size_t n = 0, bn = 0;
        j->map_data = slurp(map_path, &n);
        if (!j->map_data || !hta_cache_open(&j->cache, j->map_data, n, err, errlen)) {
            if (!err[0]) snprintf(err, errlen, "cannot read %s", map_path);
            return false;
        }
        j->have_cache = true;
        char bmpath[1024];
        snprintf(bmpath, sizeof(bmpath), "%s", map_path);
        char *slash = strrchr(bmpath, '/');
        snprintf(slash ? slash + 1 : bmpath, sizeof(bmpath) - (size_t)(slash ? slash + 1 - bmpath : 0), "bitmaps.map");
        j->bm_data = slurp(bmpath, &bn);
        if (j->bm_data) hta_resource_open(&j->bm, j->bm_data, bn, err, errlen);
        err[0] = 0;
        j->biped = find_tag(&j->cache, HTA_FOURCC('b','i','p','d'), "cyborg_mp");
        j->rifle = find_tag(&j->cache, HTA_FOURCC('m','o','d','2'), "weapons\\assault rifle\\assault rifle");
        hta_player_physics phys;
        if (hta_player_physics_load(&phys, &j->cache, err, errlen)) hta_player_apply_physics(&j->player, &phys);
        err[0] = 0;
        j->map_crc = j->cache.crc32;
    }
    if (oalmap) {
        if (!hta_external_map_load(oalmap, &j->ext, err, errlen)) return false;
        j->have_ext = true;
        j->mesh = j->ext.mesh;
        memset(&j->ext.mesh, 0, sizeof(j->ext.mesh));
        hta_bsp_mesh solid;
        hta_external_map_collision_view(&j->mesh, &j->ext, &solid);
        if (!hta_collision_build_cells(&j->col, &solid, HTA_COLLISION_CELLS_IMPORTED)) {
            snprintf(err, errlen, "imported map collision failed");
            return false;
        }
        for (uint32_t i = 0; i < j->ext.spawn_count && i < 64; i++) j->spawns[i] = j->ext.spawns[i];
        j->spawn_count = j->ext.spawn_count < 64 ? j->ext.spawn_count : 64;
        /* A phone host's map check: the Trial's CRC with the package's key. */
        if (j->have_cache) j->map_crc ^= j->ext.key;
    } else if (j->have_cache) {
        const hta_resource_map *bm = j->bm.data ? &j->bm : NULL;
        if (!hta_bsp_load_first(&j->cache, &j->mesh, err, errlen) ||
            !hta_bsp_load_textures(&j->cache, bm, &j->mesh, err, errlen)) return false;
        hta_scenario_add_objects(&j->mesh, &j->cache, bm, err, errlen);
        hta_sky_load(&j->sky, &j->cache, bm, err, errlen);
        err[0] = 0;
        if (!hta_bsp_load_collision(&j->cache, &j->coll_mesh, err, errlen) ||
            !hta_scenario_add_collision(&j->coll_mesh, &j->cache, err, errlen) ||
            !hta_collision_build(&j->col, &j->coll_mesh)) return false;
        j->spawn_count = hta_scenario_spawns(&j->cache, j->spawns, 64);
    } else {
        snprintf(err, errlen, "give --map (the Trial's bloodgulch.map) or --oalmap");
        return false;
    }
    hta_collision_set_slope(&j->col, j->player.phys.max_slope > 0 ? j->player.phys.max_slope : 0.7f);
    hta_scene_light_from_bsp(&j->mesh, j->scene.light_dir, j->scene.light_color, j->scene.ambient);
    j->scene.clear[0] = 0.42f; j->scene.clear[1] = 0.55f; j->scene.clear[2] = 0.72f;
    if (j->spawn_count) { hta_player_spawn(&j->player, &j->spawns[0]); j->cam.yaw = j->spawns[0].facing; }
    return true;
}

static void setup_effects(join *j)
{
    if (!hta_wfx_init(&j->wfx, &j->col, &j->video)) return;
    hta_wfx_load_map(&j->wfx, j->have_ext ? &j->ext : NULL, 0.0f);
    j->wfx.props.remote = true;                     /* the host breaks them */
    hta_wfx_choose_weather(&j->wfx, j->weather);
    /* Stand-ins: the debris box layout, textured from the effects atlas. */
    j->box_slots = HTA_NET_MAX_ENTITIES;
    hta_fx tmp;
    if (hta_fx_init(&tmp, j->box_slots, 1, 1, NULL)) {
        j->boxes = tmp.debris;
        tmp.debris.vertices = NULL; tmp.debris.indices = NULL; tmp.debris.submeshes = NULL;
        j->boxes.textures = &j->wfx.fx.atlas;
        hta_fx_free(&tmp);
    }
    j->box_verts = calloc((size_t)j->box_slots * 24u, sizeof(hta_vertex));
}

static void upload(join *j, hta_gfx *g)
{
    char err[256];
    j->gpu_world = hta_gfx_mesh_upload(g, &j->mesh, err, sizeof(err));
    if (!j->gpu_world) fprintf(stderr, "world upload: %s\n", err);
    j->gpu_sky = j->sky.index_count ? hta_gfx_mesh_upload(g, &j->sky, err, sizeof(err)) : NULL;
    if (j->wfx.ready) hta_wfx_gpu_upload(&j->wfx, g);
    if (j->box_verts) j->gpu_boxes = hta_gfx_mesh_upload_dynamic_world(g, &j->boxes, err, sizeof(err));
}

/* The host's props that break and return hide and show their faces. */
static void prop_events(join *j)
{
    hta_prop_event pe;
    while (j->wfx.ready && hta_props_pop(&j->wfx.props, &pe)) {
        uint32_t tag = j->wfx.props.props[pe.prop].user;
        for (uint32_t i = 0; tag && j->gpu_world && j->have_ext && j->ext.submesh_breakable &&
                             i < j->mesh.submesh_count; i++)
            if (j->ext.submesh_breakable[i] == tag)
                hta_gfx_mesh_set_draw_mode(j->gpu_world, i,
                    pe.kind == HTA_PROP_EV_RESPAWNED ? j->mesh.submeshes[i].draw_mode : HTA_DRAW_SKIP);
        if (pe.kind == HTA_PROP_EV_EXPLODED)
            hta_props_blast(&j->wfx.props, pe.pos, pe.damage, pe.radius, &j->wfx.rigid, &j->wfx.fx);
    }
}

static const char *pick_clip(const hta_net_pose *p)
{
    bool crouch = (p->flags & HTA_NET_ENTITY_CROUCH) != 0;
    if (!(p->flags & HTA_NET_ENTITY_GROUNDED)) return crouch ? "crouch rifle airborne" : "stand rifle airborne";
    return crouch ? "crouch rifle idle" : "stand rifle idle";
}

/* Everyone the host lists, as Spartans or stand-in boxes. */
static uint32_t draw_entities(join *j, hta_gfx *g, double now, float dt, hta_gfx_dynamic *dyn, uint32_t n, uint32_t cap)
{
    static hta_rigid_world boxes;
    bool stand_ins = !j->biped;
    if (stand_ins) {
        if (!boxes.cap) hta_rigid_init(&boxes, HTA_NET_MAX_ENTITIES, NULL);
        hta_rigid_clear(&boxes);
    }
    for (uint32_t i = 0; i < HTA_NET_MAX_ENTITIES; i++) {
        hta_net_pose p;
        if (!hta_net_view_entity(&j->view, i, now, &p) || p.gibbed) continue;
        if (stand_ins) {
            if (!p.alive) continue;
            hta_rigid_desc d;
            memset(&d, 0, sizeof(d));
            d.shape = HTA_RIGID_BOX;
            d.material = p.kind == HTA_NET_ENTITY_BOT ? HTA_RMAT_FLESH : HTA_RMAT_WOOD;
            d.half[0] = 0.12f; d.half[1] = 0.2f; d.half[2] = 0.35f;
            memcpy(d.pos, p.pos, sizeof(d.pos)); d.pos[2] += 0.35f;
            d.rot[0] = cosf(p.yaw * 0.5f); d.rot[3] = sinf(p.yaw * 0.5f);
            hta_rigid_spawn(&boxes, &d);
            continue;
        }
        hta_actor *a = &j->actor[i];
        if (!j->actor_loaded[i]) {
            char err[256];
            const hta_resource_map *bm = j->bm.data ? &j->bm : NULL;
            if (!hta_actor_load(a, &j->cache, bm, j->biped, err, sizeof(err))) continue;
            if (j->rifle) hta_actor_hold(a, &j->cache, bm, j->rifle, "right hand", err, sizeof(err));
            j->actor_gpu[i] = hta_gfx_mesh_upload_dynamic_world(g, &a->mesh, err, sizeof(err));
            j->actor_loaded[i] = j->actor_gpu[i] != NULL;
            if (!j->actor_loaded[i]) continue;
        }
        if (!p.alive) {
            if (!j->actor_dead[i]) { hta_actor_play_death(a, &j->rng); j->actor_dead[i] = true; }
        } else {
            j->actor_dead[i] = false;
            const char *clip = pick_clip(&p);
            if (a->clip < 0 || strcmp(a->graph.anims[a->clip].name, clip)) hta_actor_play(a, clip, false);
        }
        hta_actor_update(a, dt);
        hta_actor_place(a, p.pos, p.yaw);
        if (n < cap) {
            memset(&dyn[n], 0, sizeof(dyn[n]));
            dyn[n].mesh = j->actor_gpu[i]; dyn[n].vertices = a->posed;
            dyn[n].vertex_count = a->mesh.vertex_count; dyn[n].lit = true;
            n++;
        }
    }
    if (stand_ins && j->gpu_boxes && n < cap) {
        hta_fx fake;
        memset(&fake, 0, sizeof(fake));
        fake.debris_slots = j->box_slots;
        fake.debris_verts = j->box_verts;
        hta_fx_build_debris(&fake, &boxes);
        memset(&dyn[n], 0, sizeof(dyn[n]));
        dyn[n].mesh = j->gpu_boxes; dyn[n].vertices = j->box_verts;
        dyn[n].vertex_count = j->box_slots * 24u; dyn[n].lit = true;
        n++;
    }
    return n;
}

static bool frame(join *j, hta_gfx *g, double now, float dt)
{
    /* Props solid while whole, with a broad phase over them. */
    j->col.instances = j->merged;
    j->col.instance_count = hta_props_instances(&j->wfx.props, NULL, 0, j->merged, 1024);
    hta_collision_index_instances(&j->col, &j->col_index, 0.25f);
    prop_events(j);
    hta_wfx_update(&j->wfx, dt, &j->cam);
    hta_wfx_audio_update(&j->wfx_audio, &j->wfx, &j->cam, dt, false);
    hta_gfx_settings look;
    hta_scene sc = j->scene;
    hta_wfx_frame_look(&j->wfx, &j->video, &look, sc.ambient, sc.light_color);
    hta_gfx_apply_settings(g, &look, NULL, 0);
    hta_wfx_gpu_frame(&j->wfx, g, &j->video, dt);
    hta_gfx_dynamic dyn[HTA_NET_MAX_ENTITIES + 8];
    uint32_t n = draw_entities(j, g, now, dt, dyn, 0, HTA_NET_MAX_ENTITIES + 4);
    n = hta_wfx_gpu_draw(&j->wfx, &j->cam, dyn, n, HTA_NET_MAX_ENTITIES + 8);
    hta_camera cam = j->cam;
    uint32_t w, h;
    hta_gfx_extent(g, &w, &h);
    cam.aspect = h ? (float)w / (float)h : 1.0f;
    return hta_gfx_draw(g, &cam, &sc, j->gpu_world, j->gpu_sky, NULL, dyn, n, NULL, NULL);
}

/* One frame of play: input to prediction and to the host. */
static void play(join *j, const hta_input *in, double now, float dt)
{
    hta_net_client_pump(&j->net, now);
    hta_net_view_update(&j->view, &j->net, now, &j->player, &j->cam, &j->wfx);
    hta_player_input pi = { in->move_forward, in->move_right, in->look_yaw, in->look_pitch,
                            in->jump, false, in->crouch };
    if (!j->view.me_alive && j->net.connected && j->view.me >= 0) {
        pi.move_forward = pi.move_right = 0.0f; pi.jump = pi.crouch = false;   /* dead: look only */
    }
    hta_player_update(&j->player, &j->cam, &j->col, &pi, dt);
    hta_net_view_input ni;
    memset(&ni, 0, sizeof(ni));
    ni.forward = pi.move_forward; ni.right = pi.move_right;
    ni.jump = in->jump; ni.fire = in->fire; ni.crouch = in->crouch; ni.alt = in->alt_fire;
    ni.grenade = in->key_pressed[SDL_SCANCODE_G];
    ni.melee = in->key_pressed[SDL_SCANCODE_F];
    ni.reload = in->key_pressed[SDL_SCANCODE_R];
    ni.pickup = ni.action = in->use_pressed;
    ni.ability = in->key_pressed[SDL_SCANCODE_Q];
    static uint8_t slot;
    if (in->key_pressed[SDL_SCANCODE_1]) slot = 0;
    if (in->key_pressed[SDL_SCANCODE_2]) slot = 1;
    ni.weapon_slot = slot;
    hta_net_view_send(&j->view, &j->net, now, &ni, &j->cam, &j->player, true);
}

static void scripted(double t, hta_input *in)
{
    memset(in, 0, sizeof(*in));
    in->move_forward = 1.0f;
    in->look_yaw = 0.01f;
    in->fire = fmod(t, 1.0) < 0.3;
    if (fmod(t, 2.0) < 0.02) in->key_pressed[SDL_SCANCODE_G] = true;
}

static void title(join *j, hta_desktop *d, double now, const char *host, unsigned port)
{
    char t[512];
    const char *feed[1];
    uint32_t nf = hta_net_view_feed(&j->view, now, feed, 1);
    if (!j->net.connected)
        snprintf(t, sizeof(t), "Megamod LAN | connecting to %s:%u%s", host, port,
                 j->net.reject_reason == HTA_NET_REJECT_MAP ? " | REFUSED: not the host's map" :
                 j->net.reject_reason == HTA_NET_REJECT_FULL ? " | REFUSED: full" : "");
    else
        snprintf(t, sizeof(t), "Megamod LAN | player %u%s | %d - %d | %.0f ms | %s%s",
                 j->net.id, j->view.me_alive ? "" : " (dead)", j->view.team_score[0], j->view.team_score[1],
                 j->net.stats.ping_ms, hta_quality_name(j->video.preset), nf ? " | " : "");
    if (nf) strncat(t, feed[0], sizeof(t) - strlen(t) - 1);
    if (d) hta_desktop_set_title(d, t);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <host> [port] [--map bloodgulch.map] [--oalmap m.oalmap] [--preset P] "
                        "[--weather W] [--auto S] [--shot out.ppm]\n", argv[0]);
        return 2;
    }
    static join j;
    const char *host = argv[1], *map = NULL, *oal = NULL, *shot = NULL;
    unsigned port = 32270;
    double autos = 0;
    hta_gfx_settings_preset(&j.video, HTA_QUALITY_HIGH);
    j.weather = HTA_WFX_WEATHER_AUTO;
    for (int i = 2; i < argc; i++) {
        hta_quality q;
        hta_weather_kind wk;
        if (!strcmp(argv[i], "--map") && i + 1 < argc) map = argv[++i];
        else if (!strcmp(argv[i], "--oalmap") && i + 1 < argc) oal = argv[++i];
        else if (!strcmp(argv[i], "--preset") && i + 1 < argc && hta_quality_from_name(argv[i + 1], &q)) { hta_gfx_settings_preset(&j.video, q); i++; }
        else if (!strcmp(argv[i], "--weather") && i + 1 < argc && hta_weather_from_name(argv[i + 1], &wk)) { j.weather = (int)wk; i++; }
        else if (!strcmp(argv[i], "--auto") && i + 1 < argc) autos = atof(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (argv[i][0] != '-') port = (unsigned)atoi(argv[i]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (port < 1 || port > 65535) return 2;
    hta_player_init(&j.player);
    hta_camera_init(&j.cam);
    j.cam.znear = 0.02f; j.cam.zfar = 1000.0f;
    j.rng = 0x10AD;
    char err[512] = { 0 };
    if (!load_world(&j, map, oal, err, sizeof(err))) { fprintf(stderr, "map: %s\n", err); return 1; }
    hta_gfx_settings_clamp(&j.video);
    setup_effects(&j);
    if (!hta_net_client_open(&j.net, host, (uint16_t)port)) { fprintf(stderr, "bad host address %s\n", host); return 1; }
    j.net.map_crc = j.map_crc;
    hta_net_view_init(&j.view);
    printf("join: %s:%u, %s%s, map check %08x, %s\n", host, port, oal ? oal : "Blood Gulch",
           j.biped ? "" : " (stand-ins: no Trial map)", j.map_crc, hta_quality_name(j.video.preset));

    hta_desktop *d = NULL;
    hta_gfx *g = NULL;
    if (shot) {
        g = hta_gfx_create_offscreen_ex(j.video.window_width, j.video.window_height, &j.video, err, sizeof(err));
    } else {
        d = hta_desktop_open("Megamod LAN", &j.video, err, sizeof(err));
        g = d ? hta_desktop_gfx(d) : NULL;
        if (d && hta_audio_sdl_start(&j.audio, err, sizeof(err)))
            hta_wfx_audio_init(&j.wfx_audio, &j.audio, 0x10ADu);
    }
    if (!g) { fprintf(stderr, "renderer: %s\n", err); return 1; }
    upload(&j, g);
    bool captured = false;
    double t0 = SDL_GetTicks64() / 1000.0, logged = t0;
    unsigned frames = 0;
    hta_input in;
    memset(&in, 0, sizeof(in));
    for (;;) {
        double now;
        float dt;
        if (shot) {
            now = t0 + frames / 60.0; dt = 1.0f / 60.0f;
            /* Real time for the network: the host runs on the clock. */
            while (SDL_GetTicks64() / 1000.0 < now) SDL_Delay(1);
        } else {
            if (!hta_desktop_poll(d, &in, 0.0022f)) break;
            dt = hta_desktop_pace(d, j.video.fps_cap);
            now = SDL_GetTicks64() / 1000.0;
            if (in.fire_pressed && !captured && !autos) { hta_desktop_capture_mouse(d, true); captured = true; in.fire_pressed = false; }
            if (in.key_pressed[SDL_SCANCODE_ESCAPE]) {
                if (captured) { hta_desktop_capture_mouse(d, false); captured = false; } else break;
            }
            if (in.key_pressed[SDL_SCANCODE_F2]) {
                hta_quality q = (hta_quality)((j.video.preset + 1) % HTA_QUALITY_CUSTOM);
                hta_gfx_settings nv;
                hta_gfx_settings_preset(&nv, q);
                nv.window_mode = j.video.window_mode; nv.window_width = j.video.window_width; nv.window_height = j.video.window_height;
                j.video = nv;
                hta_wfx_settings(&j.wfx, &nv);
                hta_desktop_apply(d, &nv, err, sizeof(err));
            }
            if (in.key_pressed[SDL_SCANCODE_F3]) {
                j.weather = (j.weather + 2) % (HTA_WEATHER_COUNT + 1) - 1;
                hta_wfx_choose_weather(&j.wfx, j.weather);
            }
        }
        if (autos || shot) scripted(now - t0, &in);
        play(&j, &in, now, dt);
        if (!frame(&j, g, now, dt) && d) {
            uint32_t w, h;
            hta_desktop_size(d, &w, &h);
            hta_gfx_resize(g, w, h, err, sizeof(err));
        }
        frames++;
        /* The feed, as it happens. */
        static uint32_t printed_kills;
        if (j.view.kills != printed_kills) {
            printed_kills = j.view.kills;
            printf("join: %s\n", j.view.feed[0]);
        }
        if (now - logged > 1.0) {
            title(&j, d, now, host, port);
            logged = now;
        }
        double limit = autos > 0 ? autos : 0;
        if (shot && limit <= 0) limit = 5.0;
        if (limit > 0 && now - t0 >= limit) break;
    }
    unsigned seen = 0, broken = 0;
    for (uint32_t i = 0; i < HTA_NET_MAX_ENTITIES; i++) seen += j.view.ent[i].live;
    for (uint32_t i = 0; i < j.wfx.props.count; i++) broken += j.wfx.props.props[i].broken;
    printf("join: %s, player %u, %u entities live, %u kills (%u gibbed), %u effects, %u corrections, "
           "score %d-%d, %u props broken, %u sounds\n",
           j.net.connected ? "connected" : "NOT connected", j.net.id, seen,
           j.view.kills, j.view.gibs, j.view.fx, j.view.corrections, j.view.team_score[0], j.view.team_score[1],
           broken, j.wfx_audio.played);
    if (shot) {
        uint32_t w = j.video.window_width, h = j.video.window_height;
        uint8_t *px = malloc((size_t)w * h * 4);
        if (px && hta_gfx_readback(g, px, (size_t)w * h * 4)) {
            FILE *f = fopen(shot, "wb");
            if (f) {
                fprintf(f, "P6\n%u %u\n255\n", w, h);
                for (uint32_t i = 0; i < w * h; i++) fwrite(px + i * 4, 1, 3, f);
                fclose(f);
            }
        }
        free(px);
    }
    bool ok = j.net.connected && j.view.me >= 0;
    hta_net_client_close(&j.net);
    hta_audio_sdl_stop();
    hta_wfx_audio_free(&j.wfx_audio);
    for (uint32_t i = 0; i < HTA_NET_MAX_ENTITIES; i++)
        if (j.actor_loaded[i]) { hta_gfx_mesh_free(g, j.actor_gpu[i]); hta_actor_free(&j.actor[i]); }
    if (j.gpu_boxes) hta_gfx_mesh_free(g, j.gpu_boxes);
    if (j.wfx.ready) hta_wfx_gpu_free(&j.wfx, g);
    if (j.gpu_sky) hta_gfx_mesh_free(g, j.gpu_sky);
    if (j.gpu_world) hta_gfx_mesh_free(g, j.gpu_world);
    if (d) hta_desktop_close(d); else hta_gfx_destroy(g);
    return ok ? 0 : 1;
}
