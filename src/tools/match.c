/* megamod-match: a real match on the desktop with no GPU and no player --
 * the phone's own load and start (app/match_load.h) on the phone's content
 * (app/fs.h), bots only, run headless at 60 Hz. What a dedicated server
 * and the desktop build are made of; for an agent, the quickest proof that
 * a map loads and plays.
 *
 *   megamod-match [--trial DIR] [--bundle DIR] [--world NAME] [--bots N]
 *                 [--mode slayer|team|ctf] [--skill 0-3] [--score N] [--seconds S]
 *                 [--cache DIR] [--host PORT]
 *   (or HTA_TRIAL_DIR / HTA_BUNDLE_DIR). Exit 0 if the match loaded and ran.
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
        else if (!strcmp(a, "--mode") && v) {
            mode = !strcmp(v, "ctf") ? HTA_MODE_CTF : !strcmp(v, "team") ? HTA_MODE_TEAM_SLAYER : HTA_MODE_SLAYER;
            i++;
        } else {
            fprintf(stderr, "usage: %s [--trial DIR] [--bundle DIR] [--world NAME] [--bots N] "
                            "[--mode slayer|team|ctf] [--skill 0-3] [--score N] [--seconds S] [--cache DIR] [--host PORT]\n", argv[0]);
            return 2;
        }
    }
    static hta_fs fs;
    hta_fs_init(&fs);
    hta_fs_mount_content(&fs, trial, bundle);
    hta_session *s = calloc(1, sizeof(*s));
    if (!s) return 1;
    for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++) s->peer_unit[i] = -1;
    s->me = -1;
    hta_fs_blob map, snd, bmp;
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
    if (!hta_match_start(s, cache, false)) { fprintf(stderr, "match: no playable game on this map\n"); return 1; }
    s->map_loaded = true;
    hta_match_begin(s);
    double t2 = hta_time_seconds();
    printf("match: %s, mode %d, %u units, nav %s, items %s; world %.0f ms, start %.0f ms\n",
           world[0] ? world : "bloodgulch", s->game.mode, s->game.unit_count,
           s->nav.built ? "yes" : "no", s->items.loaded ? "yes" : "no",
           (t1 - t0) * 1000.0, (t2 - t1) * 1000.0);

    const float dt = 1.0f / 60.0f;
    unsigned kills = 0, broken = 0, rounds = 0, frames = (unsigned)(seconds * 60.0);
    double sim0 = hta_time_seconds();
    for (unsigned f = 0; f < frames; f++) {
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
        for (uint32_t i = 0; i < s->outbox_count; i++)
            if (s->outbox[i].kind == HTA_EV_KILL) { kills++; printf("  %6.1f  %s\n", f * dt, s->outbox[i].text); }
        for (uint32_t i = 0; i < s->prop_outbox_count; i++)
            broken += s->prop_outbox[i].kind == HTA_PROP_EV_BROKE;
        rounds += s->round_restarted;
    }
    double sim = hta_time_seconds() - sim0;
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
        for (uint32_t i = 0; i < s->went.defs->count; i++)
            if (s->went.defs->entity[i].kind == HTA_WDEF_MOVER)
                printf("match: mover %s phase %u t %.2f\n", s->went.defs->entity[i].id, s->went.st[i].phase, s->went.st[i].t);
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
    if (host_port)
        printf("match: hosted on UDP %d: %llu joiners refused, %u peers at the end\n", host_port,
               (unsigned long long)s->host_server.stats.refused, hta_net_server_count(&s->host_server));
    /* The world's own state goes with it (X5: its asset table and the sound
     * bank's copies), so a leak-checked run sees the teardown. The rest of
     * the session lives until exit, as it always has. */
    hta_world_sounds_free(&s->world_sounds);
    hta_went_free(&s->went);
    hta_external_map_free(&s->world_ext);
    return 0;
}
