/* megamod-match: a real match on the desktop with no GPU and no player --
 * the phone's own load and start (app/match_load.h) on the phone's content
 * (app/fs.h), bots only, run headless at 60 Hz. What a dedicated server
 * and the desktop build are made of; for an agent, the quickest proof that
 * a map loads and plays.
 *
 *   megamod-match [--trial DIR] [--bundle DIR] [--world NAME] [--bots N]
 *                 [--mode slayer|team|ctf] [--skill 0-3] [--seconds S]
 *                 [--cache DIR]
 *   (or HTA_TRIAL_DIR / HTA_BUNDLE_DIR). Exit 0 if the match loaded and ran. */
#include "app/fs.h"
#include "app/match_load.h"
#include "app/session.h"
#include "platform/platform.h"
#include <stdio.h>
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
    int bots = 7, mode = HTA_MODE_SLAYER, skill = 1;
    double seconds = 60.0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--trial") && v) { trial = v; i++; }
        else if (!strcmp(a, "--bundle") && v) { bundle = v; i++; }
        else if (!strcmp(a, "--world") && v) { world = strcmp(v, "bloodgulch") ? v : ""; i++; }
        else if (!strcmp(a, "--bots") && v) { bots = atoi(v); i++; }
        else if (!strcmp(a, "--skill") && v) { skill = atoi(v); i++; }
        else if (!strcmp(a, "--seconds") && v) { seconds = atof(v); i++; }
        else if (!strcmp(a, "--cache") && v) { cache = v; i++; }
        else if (!strcmp(a, "--mode") && v) {
            mode = !strcmp(v, "ctf") ? HTA_MODE_CTF : !strcmp(v, "team") ? HTA_MODE_TEAM_SLAYER : HTA_MODE_SLAYER;
            i++;
        } else {
            fprintf(stderr, "usage: %s [--trial DIR] [--bundle DIR] [--world NAME] [--bots N] "
                            "[--mode slayer|team|ctf] [--skill 0-3] [--seconds S] [--cache DIR]\n", argv[0]);
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
    s->score_limit = 25; s->time_limit_min = 0; s->respawn_delay = 5.0f;
    s->vehicle_roster = HTA_VROSTER_ALL;

    double t0 = hta_time_seconds();
    if (!hta_match_load_world(s, &fs, cache)) { fprintf(stderr, "match: %s\n", s->status); return 1; }
    double t1 = hta_time_seconds();
    if (!hta_match_start(s, cache, false)) { fprintf(stderr, "match: no playable game on this map\n"); return 1; }
    hta_match_begin(s);
    double t2 = hta_time_seconds();
    printf("match: %s, mode %d, %u units, nav %s, items %s; world %.0f ms, start %.0f ms\n",
           world[0] ? world : "bloodgulch", s->game.mode, s->game.unit_count,
           s->nav.built ? "yes" : "no", s->items.loaded ? "yes" : "no",
           (t1 - t0) * 1000.0, (t2 - t1) * 1000.0);

    const float dt = 1.0f / 60.0f;
    unsigned kills = 0, frames = (unsigned)(seconds * 60.0);
    double sim0 = hta_time_seconds();
    for (unsigned f = 0; f < frames; f++) {
        hta_pickups_update(&s->items, dt);
        hta_game_update(&s->game, dt);
        hta_game_event e;
        while (hta_game_pop(&s->game, &e))
            if (e.kind == HTA_EV_KILL) { kills++; printf("  %6.1f  %s\n", f * dt, e.text); }
    }
    double sim = hta_time_seconds() - sim0;
    uint32_t alive = 0;
    for (uint32_t i = 0; i < s->game.unit_count; i++) alive += s->game.units[i].alive;
    printf("match: %.0f s simulated in %.2f s (%.2f ms/frame), %u kills, %u of %u alive\n",
           seconds, sim, frames ? sim * 1000.0 / frames : 0.0, kills, alive, s->game.unit_count);
    return 0;
}
