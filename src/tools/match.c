/* megamod-match: a real match on the desktop with no GPU and no player --
 * the phone's own load and start (app/match_load.h) on the phone's content
 * (app/fs.h), bots only, run headless at 60 Hz. What a dedicated server
 * and the desktop build are made of; for an agent, the quickest proof that
 * a map loads and plays.
 *
 *   megamod-match [--trial DIR] [--bundle DIR] [--world NAME] [--bots N]
 *                 [--mode slayer|team|ctf] [--skill 0-3] [--score N] [--seconds S]
 *                 [--cache DIR] [--host PORT] [--trace-events] [--world-state]
 *   (or HTA_TRIAL_DIR / HTA_BUNDLE_DIR). Exit 0 if the match loaded and ran.
 *
 * --world-state (X8): at the end, one line per world object -- authored ID,
 * kind, runtime index, replication channel and index (or host-only), and
 * its state ("match: object nightshift:entity/aux_power relay runtime 3
 * logical 0 active"). docs/WORLD_STATE.md.
 *
 * --trace-events: log every X7 event that has bindings, each binding's
 * conditions and what it queued ("[bind] ..."; bounded per step). A debug
 * aid: normal logs carry only drops and diagnostics.
 *
 * --host PORT: the same match is a LAN host (the shared session's host
 * half, app/host_net.h) with no player of its own, run on the wall clock
 * so joiners -- megamod-join, a phone -- can play in it. A test seam for
 * two-client, late-join and world-entity runs (docs/WORLD_ENTITIES.md);
 * the dedicated server proper is docs/DEDICATED_SERVER.md. */
#include "app/fs.h"
#include "app/match_load.h"
#include "app/session.h"
#include "app/session_tick.h"
#include "platform/platform.h"
#include "script/script.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static bool map_trial(const hta_fs *fs, const char *name, hta_fs_blob *b, char *path, size_t pathlen)
{
    char n[64];
    snprintf(n, sizeof(n), "maps/%s", name);
    if (!hta_fs_map(fs, n, b)) return false;
    snprintf(path, pathlen, "%s", n);
    return true;
}

int main(int argc, char **argv)
{
    const char *trial = getenv("HTA_TRIAL_DIR"), *bundle = getenv("HTA_BUNDLE_DIR");
    const char *world = "", *cache = NULL;
    int bots = 7, mode = HTA_MODE_SLAYER, skill = 1, score = 25;
    double seconds = 60.0;
    int host_port = 0;
    bool trace_events = false, world_state = false, race_smoke = false;
    const char *race_route=NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--trial") && v) { trial = v; i++; }
        else if (!strcmp(a, "--bundle") && v) { bundle = v; i++; }
        else if (!strcmp(a, "--world") && v) { world = strcmp(v, "bloodgulch") ? v : ""; i++; }
        else if (!strcmp(a, "--bots") && v) { bots = atoi(v); i++; }
        else if (!strcmp(a, "--skill") && v) { skill = atoi(v); i++; }
        else if (!strcmp(a, "--score") && v) { score = atoi(v); i++; }
        else if (!strcmp(a, "--seconds") && v) { seconds = atof(v); i++; }
        else if (!strcmp(a, "--cache") && v) { cache = v; i++; }
        else if (!strcmp(a, "--host") && v) { host_port = atoi(v); i++; }
        else if (!strcmp(a, "--trace-events")) trace_events = true;
        else if (!strcmp(a, "--world-state")) world_state = true;
        else if (!strcmp(a, "--race-smoke")) race_smoke = true;
        else if (!strcmp(a, "--race-route") && v) { race_route=v; i++; }
        else if (!strcmp(a, "--mode") && v) {
            mode = !strcmp(v, "ctf") ? HTA_MODE_CTF : !strcmp(v, "team") ? HTA_MODE_TEAM_SLAYER : HTA_MODE_SLAYER;
            i++;
        } else {
            fprintf(stderr, "usage: %s [--trial DIR] [--bundle DIR] [--world NAME] [--bots N] "
                            "[--mode slayer|team|ctf] [--skill 0-3] [--score N] [--seconds S] [--cache DIR] [--host PORT] [--trace-events] "
                            "[--world-state] [--race-smoke] [--race-route WAYPOINTS]\n", argv[0]);
            return 2;
        }
    }
    float race_points[256][3]; unsigned race_point_count=0;
    if (race_route) {
        FILE *file=fopen(race_route,"r");
        if (!file) { perror("race route"); return 2; }
        while (race_point_count<256 && fscanf(file,"%f %f %f",
               &race_points[race_point_count][0],&race_points[race_point_count][1],
               &race_points[race_point_count][2])==3) {
            const float *p=race_points[race_point_count];
            if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]) ||
                fabsf(p[0])>4096 || fabsf(p[1])>4096 || fabsf(p[2])>4096) break;
            race_point_count++;
        }
        fclose(file);
        if (race_point_count<8 || race_point_count>=256) {
            fprintf(stderr,"match: race route needs 8..255 finite waypoints\n"); return 2;
        }
    }
    static hta_fs fs;
    hta_fs_init(&fs);
    hta_fs_mount_content(&fs, trial, bundle);
    hta_session *s = calloc(1, sizeof(*s));
    if (!s) return 1;
    for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++) s->peer_unit[i] = -1;
    s->me = -1;
    hta_fs_blob map={0}, snd={0}, bmp={0};
    if (!map_trial(&fs, "bloodgulch.map", &map, s->map_path, sizeof(s->map_path))) {
        fprintf(stderr, "match: no maps/bloodgulch.map (give --trial or HTA_TRIAL_DIR)\n");
        return 1;
    }
    s->map_data = (uint8_t *)map.data; s->map_size = map.size;
    if (map_trial(&fs, "sounds.map", &snd, s->sounds_path, sizeof(s->sounds_path))) {
        s->sounds_data = (uint8_t *)snd.data; s->sounds_size = snd.size;
    }
    if (map_trial(&fs, "bitmaps.map", &bmp, s->bitmaps_path, sizeof(s->bitmaps_path))) {
        s->bitmaps_data = (uint8_t *)bmp.data; s->bitmaps_size = bmp.size;
    }
    snprintf(s->world, sizeof(s->world), "%s", world);
    s->game_mode = mode; s->bot_count = bots; s->bot_skill = skill;
    s->score_limit = score; s->time_limit_min = 0; s->respawn_delay = 5.0f;
    s->vehicle_roster = HTA_VROSTER_ALL;
    /* World effects run headless: props break, block and come back; the
     * debris is only a little physics nobody draws. */
    hta_gfx_settings_preset(&s->video, HTA_QUALITY_LOW);

    if (host_port) {
        if (host_port < 1 || host_port > 65535 || !hta_net_server_open(&s->host_server, (uint16_t)host_port)) {
            fprintf(stderr, "match: cannot host on UDP %d\n", host_port);
            return 1;
        }
        s->net_enabled = s->net_hosting = true;
        snprintf(s->host_server.info.name, sizeof(s->host_server.info.name), "megamod-match");
        snprintf(s->host_server.info.map, sizeof(s->host_server.info.map), "%s", world);
        s->host_server.info.score_limit = (uint8_t)(score > 255 ? 255 : score);
    }
    double t0 = hta_time_seconds();
    if (!hta_match_load_world(s, &fs, cache)) { fprintf(stderr, "match: %s\n", s->status); return 1; }
    double t1 = hta_time_seconds();
    if (!hta_match_start(s, cache, race_smoke || race_route)) { fprintf(stderr, "match: no playable game on this map\n"); return 1; }
    s->map_loaded = true;
    hta_match_begin(s);
    if ((race_smoke || race_route) && s->game.mode!=HTA_MODE_RACING) {
        fprintf(stderr,"match: race driving needs an authored racing world\n"); return 2;
    }
    s->went.trace = trace_events;
    double t2 = hta_time_seconds();
    printf("match: %s, mode %d, %u units, nav %s, items %s; world %.0f ms, start %.0f ms\n",
           world[0] ? world : "bloodgulch", s->game.mode, s->game.unit_count,
           s->nav.built ? "yes" : "no", s->items.loaded ? "yes" : "no",
           (t1 - t0) * 1000.0, (t2 - t1) * 1000.0);

    const float dt = 1.0f / 60.0f;
    unsigned kills = 0, broken = 0, rounds = 0, frames = (unsigned)(seconds * 60.0);
    double sim0 = hta_time_seconds();
    uint8_t last_gate=0,last_lap=0; unsigned airborne_frames=0,route_resets=0;
    bool saw_finish=false,saw_reset=false; float finish_at=0,finish_peak=0;
    unsigned finish_pad_hits=0;
    for (unsigned f = 0; f < frames; f++) {
        if (race_smoke) s->race_local_input.throttle=1;
        if (race_route) {
            hta_arcade_racer *car=&s->race_car[s->me];
            unsigned nearest=0; float best=INFINITY;
            for (unsigned i=0;i<race_point_count;i++) {
                float dx=car->pos[0]-race_points[i][0],dy=car->pos[1]-race_points[i][1];
                float d=dx*dx+dy*dy;
                if (d<best) { best=d; nearest=i; }
            }
            unsigned look=(nearest+2)%race_point_count;
            float heading=atan2f(race_points[look][1]-car->pos[1],
                                 race_points[look][0]-car->pos[0]);
            float error=remainderf(heading-car->yaw,6.28318530718f);
            float speed=hypotf(car->vel[0],car->vel[1]);
            s->race_local_input=(hta_arcade_input){
                .throttle=1,
                .steer=fmaxf(-1,fminf(1,error*1.7f)),
                .brake=fabsf(error)>0.65f && speed>24,
                .drift=fabsf(error)>0.18f && fabsf(error)<0.55f && speed>18
            };
            if (best>2500) { s->race_reset_local=true; route_resets++; }
        }
        double now = f * (double)dt;
        if (host_port) {
            /* Joiners run on the clock: so does a host. */
            now = hta_time_seconds() - sim0;
            while (now < f * (double)dt) {
                struct timespec nap = { 0, 1000000 };
                nanosleep(&nap, NULL);
                now = hta_time_seconds() - sim0;
            }
        }
        hta_pickups_update(&s->items, dt);
        hta_session_tick(s, dt, now, NULL);
        if (race_route && s->me>=0) {
            const hta_race_entry *e=&s->race.racer[s->me];
            if (s->race.phase==HTA_RACE_GO && !s->race_car[s->me].grounded) airborne_frames++;
            if (e->finished && !saw_finish) {
                saw_finish=true; finish_at=e->finish_time;
                finish_pad_hits=s->race_pad_hits[s->me];
                finish_peak=s->race_peak_speed[s->me];
            }
            if (saw_finish && s->race.phase==HTA_RACE_READY && e->lap==1)
                saw_reset=true;
            if (e->next_gate!=last_gate || e->lap!=last_lap) {
                printf("match: race route %.2f s lap %u next gate %u speed %.1f pos %.1f %.1f %.1f\n",
                       f*dt,e->lap,e->next_gate,
                       hypotf(s->race_car[s->me].vel[0],s->race_car[s->me].vel[1]),
                       s->race_car[s->me].pos[0],s->race_car[s->me].pos[1],s->race_car[s->me].pos[2]);
                last_gate=e->next_gate; last_lap=e->lap;
            }
        }
        for (uint32_t i = 0; i < s->outbox_count; i++)
            if (s->outbox[i].kind == HTA_EV_KILL) { kills++; printf("  %6.1f  %s\n", f * dt, s->outbox[i].text); }
        for (uint32_t i = 0; i < s->prop_outbox_count; i++)
            broken += s->prop_outbox[i].kind == HTA_PROP_EV_BROKE;
        rounds += s->round_restarted;
    }
    double sim = hta_time_seconds() - sim0;
    if (race_smoke) {
        const hta_arcade_racer *car=&s->race_car[s->me];
        float speed=hypotf(car->vel[0],car->vel[1]);
        printf("match: race smoke phase %u position %.2f %.2f %.2f speed %.2f peak %.2f pad hits %u gate %u lap %u\n",
               s->race.phase,car->pos[0],car->pos[1],car->pos[2],speed,
               s->race_peak_speed[s->me],
               s->race_pad_hits[s->me],s->race.racer[s->me].next_gate,s->race.racer[s->me].lap);
        if (seconds>=18 && (s->race.phase<HTA_RACE_GO ||
            s->race_pad_hits[s->me]<1 || s->race_peak_speed[s->me]<35)) {
            fprintf(stderr,"match: race smoke failed to accelerate through the first boost pad\n");
            return 1;
        }
    }
    if (race_route) {
        const hta_race_entry *e=&s->race.racer[s->me];
        printf("match: race route result lap %u gate %u finished %d round reset %d finish time %.2f airborne frames %u resets %u peak %.2f pad hits %u\n",
               e->lap,e->next_gate,saw_finish,saw_reset,finish_at,airborne_frames,route_resets,
               finish_peak,finish_pad_hits);
        if (seconds>=190 && (!saw_finish || airborne_frames<10 ||
                             finish_pad_hits<2 || route_resets>4)) return 1;
        if (seconds>=205 && !saw_reset) return 1;
    }
    uint32_t alive = 0;
    for (uint32_t i = 0; i < s->game.unit_count; i++) alive += s->game.units[i].alive;
    printf("match: %.0f s simulated in %.2f s (%.2f ms/frame), %u kills, %u of %u alive, "
           "%u props broken of %u, %u new rounds\n",
           seconds, sim, frames ? sim * 1000.0 / frames : 0.0, kills, alive, s->game.unit_count,
           broken, s->wfx.props.count, rounds);
    if (s->went.loaded) {
        const hta_went_stats *ws = &s->went.stats;
        printf("match: world entities: %u; events dispatched %llu, deferred %llu, dropped %llu "
               "(queue full %llu, chain %llu, stale %llu, input %llu), deepest queue %u; %u teleports\n",
               s->went.defs->count, (unsigned long long)ws->dispatched, (unsigned long long)ws->deferred,
               (unsigned long long)(ws->dropped_full + ws->dropped_depth + ws->dropped_stale + ws->dropped_input),
               (unsigned long long)ws->dropped_full, (unsigned long long)ws->dropped_depth,
               (unsigned long long)ws->dropped_stale, (unsigned long long)ws->dropped_input, ws->max_queue,
               s->went_teleports);
        if (s->went.defs->binding_count)
            printf("match: bindings: %u; matched %llu, skipped by a condition %llu, actions queued %llu, dropped by the cascade "
                   "budget %llu, without an actor %llu; %u damage applied\n", s->went.defs->binding_count,
                   (unsigned long long)ws->bindings_matched, (unsigned long long)ws->bindings_skipped,
                   (unsigned long long)ws->actions_queued, (unsigned long long)ws->dropped_budget,
                   (unsigned long long)ws->no_actor, s->went_hurts);
        for (uint32_t i = 0; i < s->went.defs->count; i++)
            if (s->went.defs->entity[i].kind == HTA_WDEF_MOVER)
                printf("match: mover %s phase %u t %.2f\n", s->went.defs->entity[i].id, s->went.st[i].phase, s->went.st[i].t);
            else if (s->went.defs->entity[i].kind == HTA_WDEF_RELAY)
                printf("match: relay %s %s\n", s->went.defs->entity[i].id,
                       hta_went_relay_active(&s->went, i) ? "active" : "inactive");
        /* X8: what the world costs on the wire, and (--world-state) every
         * object's identities and state. */
        uint32_t sp, fl, ho;
        hta_wrep_count(s->went.defs, &sp, &fl, &ho);
        printf("match: world state: %u runtime objects: %u spatial (movers), %u logical (relays), %u host-only\n",
               s->went.defs->count, sp, fl, ho);
        for (uint32_t i = 0; world_state && i < s->went.defs->count; i++) {
            char line[256];
            if (hta_went_describe(&s->went, i, line, sizeof(line))) printf("match: object %s\n", line);
        }
        const hta_asset_table *a = &s->world_ext.assets;
        if (a->model_count || a->sound_count)
            printf("match: assets: %u textures, %u materials, %u models, %u sounds; %u mover sounds started (%u distinct clips)\n",
                   a->texture_count, a->material_count, a->model_count, a->sound_count, s->world_sounds_heard,
                   s->world_sounds.slot_count);
    }
    if (s->script) {
        const hta_script_stats *st = hta_script_get_stats(s->script);
        printf("match: scripts: %llu callbacks, %llu requests (%llu refused), %llu errors (%llu over budget), "
               "memory %zu bytes (peak %zu)\n", (unsigned long long)st->callbacks, (unsigned long long)st->requests,
               (unsigned long long)st->refused_requests, (unsigned long long)st->errors,
               (unsigned long long)st->budget_aborts, st->memory, st->memory_peak);
        for (uint32_t i = 0; i < s->game.unit_count; i++)
            if (s->game.units[i].kind != HTA_UNIT_NONE)
                printf("match: unit %u kills %d deaths %d score %d\n", i, s->game.units[i].kills,
                       s->game.units[i].deaths, s->game.units[i].score);
        size_t left = hta_script_destroy(s->script);
        s->script = NULL;
        printf("match: Lua state closed, %zu bytes left\n", left);
    }
    if (host_port) {
        const hta_net_stats *ns = &s->host_server.stats;
        printf("match: hosted on UDP %d: %llu joiners refused, %u peers at the end\n", host_port,
               (unsigned long long)ns->refused, hta_net_server_count(&s->host_server));
        if (s->host_server.last_refusal == HTA_NET_REJECT_VERSION)
            printf("match: the last joiner refused spoke protocol v%u (this host v%u)\n",
                   s->host_server.last_refused_version, HTA_NET_VERSION);
        printf("match: WORLD_STATE sent %llu times (%llu payload bytes, largest %u, %.1f per s); %llu bytes out in all\n",
               (unsigned long long)ns->world_states, (unsigned long long)ns->world_state_bytes, ns->world_state_max,
               seconds > 0 ? (double)ns->world_states / seconds : 0.0, (unsigned long long)ns->bytes_out);
    }
    /* Free the complete headless session so LSan also covers racing's
     * loaded collision, effects, rule state and public bootstrap map. */
    if (s->net_hosting) hta_net_server_close(&s->host_server);
    hta_world_sounds_free(&s->world_sounds);
    hta_went_free(&s->went);
    hta_wfx_free(&s->wfx);
    hta_instance_index_free(&s->col_index);
    hta_game_free(&s->game);
    hta_nav_free(&s->nav);
    hta_pickups_free(&s->items);
    free(s->world_playable);
    hta_collision_free(&s->col);
    hta_vehicles_free(&s->vehicles);
    hta_bsp_free(&s->mesh);
    hta_bsp_free(&s->coll_mesh);
    hta_external_map_free(&s->world_ext);
    hta_fs_unmap(&map);
    hta_fs_unmap(&snd);
    hta_fs_unmap(&bmp);
    free(s);
    return 0;
}
