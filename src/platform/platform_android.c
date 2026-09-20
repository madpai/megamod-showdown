/* Android platform layer — the only native file that touches Android APIs.
 *
 * Responsibilities: NativeActivity lifecycle, locating the user's own Halo
 * Trial data, translating touch/gamepad into engine input, driving the
 * renderer. All game logic lives in src/engine and src/asset.
 *
 * DATA: nothing proprietary ships in this APK. SetupActivity (Java) lets the
 * user pick their own legally obtained Trial map via the system document
 * picker and copies it into the app-private external files directory:
 *   /sdcard/Android/data/net.hta.halotrial/files/
 * which needs no runtime permission and no root. Native still also searches
 * Download/ etc. if All-files access happens to be granted.
 */
#include "platform.h"
#include "../engine/engine.h"
#include "../engine/camera.h"
#include "../engine/player.h"
#include "../engine/gun.h"
#include "../engine/projectile.h"
#include "../engine/particle.h"
#include "../engine/ammo.h"
#include "../engine/hud.h"
#include "../engine/viewmodel.h"
#include "../asset/cache.h"
#include "../asset/bsp.h"
#include "../asset/bitmap.h"
#include "../asset/biped.h"
#include "../asset/weapon.h"
#include "../asset/sound.h"
#include "../asset/effect.h"
#include "../asset/model.h"
#include "../gfx/gfx.h"
#include "../engine/scene_light.h"
#include "audio_android.h"

#include <android/log.h>
#include <android/input.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#define TAG "halo-trial-android"

void hta_log(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, TAG, fmt, ap);
    va_end(ap);
}

double hta_time_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

bool hta_probe_fixed_map(uint64_t addr, size_t len)
{
    void *want = (void *)(uintptr_t)addr;
    void *got = mmap(want, len, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got == MAP_FAILED) {
        hta_log("[probe] mmap at 0x%llx FAILED", (unsigned long long)addr);
        return false;
    }
    bool exact = (got == want);
    if (exact) ((volatile unsigned char *)got)[0] = 0xAB;
    hta_log("[probe] mmap at 0x%llx -> %p (%s)", (unsigned long long)addr, got,
            exact ? "EXACT" : "MOVED");
    munmap(got, len);
    return exact;
}

/* ------------------------------------------------------------------ */

#define HTA_SND_MAX_BANK   24u
#define HTA_SND_MAX_PERMS   8u

typedef struct {
    struct android_app *app;

    /* asset state */
    char      map_path[512];
    char      bitmaps_path[512];
    char      sounds_path[512];
    uint8_t  *map_data;
    size_t    map_size;
    uint8_t  *bitmaps_data;
    size_t    bitmaps_size;
    uint8_t  *sounds_data;
    size_t    sounds_size;
    bool      map_loaded;
    char      status[256];

    hta_cache     cache;
    hta_bsp_mesh  mesh;
    hta_bsp_mesh  sky;
    hta_bsp_mesh  coll_mesh;
    hta_viewmodel vm;
    hta_collision col;

    /* Sound. One bank entry per snd! tag actually asked for, decoded once and
     * kept; Halo tags carry several permutations of the same sound and pick
     * between them, so each entry holds every permutation as its own clip. */
    hta_resource_map sounds_rm;
    hta_resource_map bitmaps_rm;
    hta_audio        audio;
    bool             audio_ok;
    struct {
        uint32_t tag_id;
        uint32_t count;
        uint32_t clip[HTA_SND_MAX_PERMS];
        int16_t *pcm[HTA_SND_MAX_PERMS];
    } bank[HTA_SND_MAX_BANK];
    uint32_t bank_count;
    uint32_t fire_snd;       /* snd! id of the weapon's gunshot, 0 if none */
    /* A weapon whose firing effect has no sound at all roars continuously
     * instead: the flamethrower. Held for as long as the trigger is. */
    uint32_t fire_loop_snd;
    float    fire_loop_gain;
    bool     fire_loop_on;
    uint32_t empty_snd;      /* the click when the magazine is out */
    uint32_t foot_snd[33];   /* per MaterialType, resolved on first use */
    uint8_t  foot_known[33];
    uint32_t impact_snd[33];
    uint8_t  impact_known[33];

    /* Every weapon in the cache a player could hold, and which one is up. */
    uint32_t weapons[24];
    uint32_t weapon_count;
    uint32_t weapon_slot;
    bool     hud_swap;
    bool     hud_zoom;
    int      zoom_level;     /* 0 = not zoomed */
    float    base_fov;
    bool     bitmaps_ok;
    uint32_t rng;

    /* Rounds you can watch fly: the rocket and the needle. */
    hta_projectiles proj;
    /* And the smoke and fire their detonation throws out. */
    hta_particles   parts;

    hta_ammo ammo;
    float    dry_cooldown;   /* stops an empty trigger clicking every frame */
    bool     hud_reload;
    bool     hud_melee;
    bool          have_mesh;
    bool          have_sky;
    bool          have_coll;
    bool          have_fp;
    hta_weapon_def weap;

    /* runtime */
    hta_gfx      *gfx;
    hta_gfx_mesh *gpu_mesh;
    hta_gfx_mesh *gpu_sky;
    hta_gfx_mesh *gpu_fx;
    hta_gfx_mesh *gpu_proj;
    hta_gfx_mesh *gpu_parts;
    hta_gfx_mesh *gpu_fp;
    hta_hud       hud;
    hta_gfx_mesh *gpu_hud;
    hta_camera    cam;
    hta_player    player;
    hta_gun       gun;
    hta_scene     scene;
    bool          has_window;
    int32_t       win_w, win_h;  /* window size the current swapchain was built for */
    double        last_time;
    uint64_t      frames;
    double        fps_accum;
    uint32_t      fps_frames;
    bool          probe_done;

    /* input */
    int32_t move_pointer, look_pointer;
    float   move_origin[2], move_cur[2];
    float   look_last[2];
    float   pad_move[2], pad_look[2];
    bool    jump_held;
    bool    fire_held;
    bool    pad_fire;
    float   pending_yaw, pending_pitch;

    /* Java HUD (GameActivity). When ready, touch move/jump/fire/look
     * come from JNI instead of hot-corners. */
    bool    hud_ready;
    float   hud_move[2];
    bool    hud_jump, hud_fire, hud_crouch;
} hta_android;

static hta_android *g_android;
static bool         g_hud_wanted;

/* ---------------------------- asset loading ---------------------------- */

static bool file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* Looks for the user's map.
 *
 * Android 11+ hides <externalDataPath> (/sdcard/Android/data/<pkg>/files) from
 * file managers, so we cannot rely on the user putting files there without adb.
 * We therefore also search ordinary, reachable locations like Download/.
 * Reading those needs "All files access", granted once in
 *   Settings -> Apps -> Halo Trial PoC -> Permissions -> Files and media.
 *
 * A hta_data.txt file in externalDataPath can override the directory entirely. */
#define HTA_MAX_SEARCH_DIRS 12
static bool find_map(hta_android *s)
{
    const char *ext = s->app->activity->externalDataPath;
    const char *intn = s->app->activity->internalDataPath;
    char dirs[HTA_MAX_SEARCH_DIRS][400];
    int ndirs = 0;

    /* optional override file: one line containing a directory path */
    if (ext) {
        char cfg[512];
        snprintf(cfg, sizeof(cfg), "%s/hta_data.txt", ext);
        FILE *f = fopen(cfg, "r");
        if (f) {
            char line[400];
            if (fgets(line, sizeof(line), f)) {
                size_t n = strlen(line);
                while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0;
                if (n) { snprintf(dirs[ndirs++], 400, "%s", line);
                         hta_log("[assets] using override directory from hta_data.txt: %s", line); }
            }
            fclose(f);
        }
    }
    /* app-private (works without any permission, but hard to write to) */
    if (ext)  { snprintf(dirs[ndirs++], 400, "%s/maps", ext); snprintf(dirs[ndirs++], 400, "%s", ext); }
    if (intn) snprintf(dirs[ndirs++], 400, "%s", intn);

    /* ordinary user-reachable locations (need All-files access) */
    static const char *public_dirs[] = {
        "/sdcard/halo-trial/maps",
        "/sdcard/halo-trial",
        "/sdcard/Download/halo-trial",
        "/sdcard/Download",
        "/sdcard/Documents",
        "/storage/emulated/0/Download",
        "/sdcard",
    };
    for (unsigned i = 0; i < sizeof(public_dirs)/sizeof(public_dirs[0]) &&
                         ndirs < HTA_MAX_SEARCH_DIRS; i++)
        snprintf(dirs[ndirs++], 400, "%s", public_dirs[i]);

    for (int i = 0; i < ndirs; i++) {
        char cand[520];
        snprintf(cand, sizeof(cand), "%s/bloodgulch.map", dirs[i]);
        if (file_exists(cand)) { snprintf(s->map_path, sizeof(s->map_path), "%s", cand); return true; }
    }
    for (int i = 0; i < ndirs; i++) {
        DIR *d = opendir(dirs[i]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n > 4 && strcasecmp(e->d_name + n - 4, ".map") == 0) {
                if (!strcasecmp(e->d_name, "bitmaps.map") ||
                    !strcasecmp(e->d_name, "sounds.map") ||
                    !strcasecmp(e->d_name, "ui.map")) continue;
                char cand[520];
                snprintf(cand, sizeof(cand), "%s/%s", dirs[i], e->d_name);
                if (file_exists(cand)) {
                    snprintf(s->map_path, sizeof(s->map_path), "%s", cand);
                    closedir(d);
                    return true;
                }
            }
        }
        closedir(d);
    }

    snprintf(s->status, sizeof(s->status), "no .map found in any search path");
    hta_log("[assets] ============================================================");
    hta_log("[assets] NO MAP FOUND. Put your own bloodgulch.map in ONE of these:");
    for (int i = 0; i < ndirs; i++) {
        DIR *probe = opendir(dirs[i]);
        hta_log("[assets]   %s  (%s)", dirs[i], probe ? "readable" : "not readable");
        if (probe) closedir(probe);
    }
    hta_log("[assets] Easiest: /sdcard/Download/bloodgulch.map, then grant");
    hta_log("[assets]   Settings > Apps > Halo Trial PoC > Permissions >");
    hta_log("[assets]   Files and media > Allow management of all files");
    hta_log("[assets] ============================================================");
    return false;
}

/* Decode every permutation of a snd! tag once, and remember it. Returns the
 * bank index, or -1. Called from the game thread only. */
static int bank_get(hta_android *s, uint32_t tag_id)
{
    if (!tag_id || tag_id == 0xFFFFFFFFu || !s->audio_ok) return -1;
    for (uint32_t i = 0; i < s->bank_count; i++)
        if (s->bank[i].tag_id == tag_id) return (int)i;
    if (s->bank_count >= HTA_SND_MAX_BANK) return -1;

    char err[HTA_ERRLEN] = {0};
    hta_sound_info info;
    if (!hta_sound_info_load(&s->cache, tag_id, &info, err, sizeof(err))) {
        hta_log("[audio] snd! 0x%08X: %s", tag_id, err);
        return -1;
    }
    uint32_t idx = s->bank_count;
    s->bank[idx].tag_id = tag_id;
    s->bank[idx].count = 0;
    uint32_t want = info.permutations;
    if (want > HTA_SND_MAX_PERMS) want = HTA_SND_MAX_PERMS;
    for (uint32_t p = 0; p < want; p++) {
        hta_pcm pcm;
        if (!hta_sound_decode(&s->cache, &s->sounds_rm, tag_id, p, &pcm,
                              err, sizeof(err))) {
            /* Ogg permutations land here; the rest of the tag still plays. */
            hta_log("[audio] snd! 0x%08X perm %u: %s", tag_id, p, err);
            continue;
        }
        uint32_t clip = hta_audio_add_clip(&s->audio, pcm.samples, pcm.frame_count,
                                           pcm.sample_rate, pcm.channels);
        if (clip == HTA_AUDIO_NO_CLIP) { hta_pcm_free(&pcm); break; }
        uint32_t k = s->bank[idx].count++;
        s->bank[idx].clip[k] = clip;
        s->bank[idx].pcm[k] = pcm.samples;   /* the mixer holds this pointer */
    }
    if (!s->bank[idx].count) return -1;
    s->bank_count++;
    hta_log("[audio] snd! 0x%08X ready: %u permutation(s)", tag_id, s->bank[idx].count);
    return (int)idx;
}

/* Halo picks between a sound's permutations rather than repeating one. */
static void play_tag(hta_android *s, uint32_t tag_id, float gain)
{
    int b = bank_get(s, tag_id);
    if (b < 0) return;
    uint32_t n = s->bank[b].count;
    s->rng = s->rng * 1664525u + 1013904223u;
    uint32_t pick = n > 1 ? (s->rng >> 16) % n : 0u;
    hta_audio_play(&s->audio, s->bank[b].clip[pick], gain);
}

/* The one continuous voice we keep; any non-zero id would do. */
#define HTA_LOOP_FIRE 1u

/* Starts or stops the weapon's continuous firing sound. Calling this every
 * frame while the trigger is held is the intended use -- the mixer leaves a
 * running loop alone rather than restarting it. */
static void fire_loop(hta_android *s, bool on)
{
    if (!s->fire_loop_snd) return;
    if (on) {
        int b = bank_get(s, s->fire_loop_snd);
        if (b < 0) return;
        hta_audio_loop(&s->audio, HTA_LOOP_FIRE, s->bank[b].clip[0],
                       s->fire_loop_gain);
        s->fire_loop_on = true;
    } else if (s->fire_loop_on) {
        hta_audio_loop_stop(&s->audio, HTA_LOOP_FIRE);
        s->fire_loop_on = false;
    }
}

/* Magnification at a zoom level. Halo spreads the tag's first and last
 * magnification evenly across however many levels the weapon has, so the
 * sniper's two become 2x and 8x and the pistol's single one is just 2x. */
static float zoom_magnification(const hta_weapon_def *w, int level)
{
    if (!w || level <= 0 || level > w->zoom_levels) return 1.0f;
    if (w->zoom_levels == 1) return w->zoom_mag[0] > 1.0f ? w->zoom_mag[0] : 1.0f;
    float t = (float)(level - 1) / (float)(w->zoom_levels - 1);
    float m = w->zoom_mag[0] + (w->zoom_mag[1] - w->zoom_mag[0]) * t;
    return m > 1.0f ? m : 1.0f;
}

/* The player writes the camera's field of view every update, so the zoom has
 * to live there rather than being poked into the camera -- doing that gave
 * exactly one zoomed frame before the next update put it back. */
static void apply_zoom(hta_android *s)
{
    hta_player_set_zoom(&s->player, zoom_magnification(&s->weap, s->zoom_level));
    /* And the scope furniture: the sniper's brackets and reticle ticks are
     * per zoom level in its HUD tag. */
    hta_hud_set_zoom(&s->hud, s->zoom_level);
}

/* Step to the next zoom level, wrapping back to none. Weapons the tag gives
 * no zoom simply have nothing to step through. */
static void cycle_zoom(hta_android *s)
{
    if (s->weap.zoom_levels <= 0) return;
    int was = s->zoom_level;
    s->zoom_level = (s->zoom_level + 1) % (s->weap.zoom_levels + 1);
    apply_zoom(s);
    uint32_t snd = s->zoom_level ? s->weap.zoom_in_snd_id : s->weap.zoom_out_snd_id;
    (void)was;
    play_tag(s, snd, 1.0f);
    hta_log("[weapon] zoom %dx", (int)zoom_magnification(&s->weap, s->zoom_level));
}

/* Put a weapon in the player's hands.
 *
 * Everything the game shows and hears about a weapon comes from its own
 * tags, so swapping means rebuilding all of it: the first-person model and
 * its animation graph, the muzzle flash and the on-gun counter that hang
 * off that model, the magazine, the rate of fire and the error cone, the
 * firing and dry-fire sounds, and the HUD's crosshair and ammo block.
 *
 * The GPU meshes are freed and re-uploaded, so this must not run while a
 * frame is in flight -- it is called from the game thread between frames.
 */
static void equip_weapon(hta_android *s, uint32_t weap_tag_id)
{
    if (!weap_tag_id) return;
    char err[HTA_ERRLEN] = {0};
    hta_resource_map *bm = s->bitmaps_ok ? &s->bitmaps_rm : NULL;

    hta_weapon_def def;
    if (!hta_weapon_load_id(&s->cache, bm, weap_tag_id, &def, NULL, err, sizeof(err))) {
        hta_log("[weapon] cannot equip 0x%08X: %s", weap_tag_id, err);
        return;
    }
    s->weap = def;

    s->gun.fire_interval = s->weap.cooldown;
    hta_gun_set_error(&s->gun, s->weap.error_angle,
                      s->weap.error_accel, s->weap.error_decel);

    /* The gunshot is not on the weapon: it hangs off the trigger's firing
     * effect, among that effect's parts. */
    s->fire_snd = hta_effect_first_sound(&s->cache, s->weap.firing_fx_id);
    if (s->fire_snd) bank_get(s, s->fire_snd);

    /* The flamethrower's firing effect has no sound in it at all: its roar
     * is a looping sound attached to the weapon OBJECT's `primary trigger`.
     * Only reach for that when the effect gives us nothing, because the
     * plasma pistol hangs its overcharge whine on the same marker. */
    fire_loop(s, false);
    s->fire_loop_snd = 0;
    s->fire_loop_gain = 1.0f;
    if (!s->fire_snd) {
        hta_loop_sound ls;
        if (hta_object_loop_sound(&s->cache, weap_tag_id, "primary trigger", &ls)) {
            s->fire_loop_snd = ls.loop ? ls.loop : ls.start;
            s->fire_loop_gain = ls.gain;
            if (s->fire_loop_snd) {
                bank_get(s, s->fire_loop_snd);
                hta_log("[weapon] continuous firing sound 0x%08X", s->fire_loop_snd);
            }
        }
    }
    s->empty_snd = hta_effect_first_sound(&s->cache, s->weap.empty_fx_id);
    if (s->empty_snd) bank_get(s, s->empty_snd);
    /* Decoding a sound takes long enough to be a visible hitch, so the zoom
     * sounds are decoded on equip rather than on the first press. */
    if (s->weap.zoom_in_snd_id)  bank_get(s, s->weap.zoom_in_snd_id);
    if (s->weap.zoom_out_snd_id) bank_get(s, s->weap.zoom_out_snd_id);
    /* Impacts are this projectile's, so forget the last weapon's. */
    memset(s->impact_known, 0, sizeof(s->impact_known));

    /* And the art its marks are drawn with. A rocket chars; a rifle leaves
     * a hole. Halo hangs the decal off the impact effect, so take the first
     * material response that names one -- the marks were a flat 1x1 square
     * until now. */
    {
        uint32_t decal = s->proj.decal_id;
        if (!decal) {
            for (uint8_t m = 0; m < 33u && !decal; m++) {
                uint32_t fx = hta_projectile_response_effect(&s->cache,
                                                             s->weap.projectile_id, m);
                if (fx) hta_effect_detonation(&s->cache, fx, NULL, NULL, &decal);
            }
        }
        uint32_t bm = hta_decal_bitmap(&s->cache, decal);
        hta_bitmap img;
        memset(&img, 0, sizeof(img));
        char derr[HTA_ERRLEN];
        if (bm && hta_bitmap_decode(&s->cache, s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                                    bm, 0, &img, derr, sizeof(derr))) {
            hta_gun_set_decal(&s->gun, img.rgba, img.width, img.height);
            hta_log("[weapon] impact decal %ux%u", img.width, img.height);
            hta_bitmap_free(&img);
        } else {
            hta_gun_set_decal(&s->gun, NULL, 0, 0);
        }
    }

    /* The weapon's own round, if it is an object rather than a particle.
     * Most of the roster has nothing to draw, which is not a failure. */
    if (s->gpu_parts) { hta_gfx_mesh_free(s->gfx, s->gpu_parts); s->gpu_parts = NULL; }
    if (s->gpu_proj) { hta_gfx_mesh_free(s->gfx, s->gpu_proj); s->gpu_proj = NULL; }
    {
        char perr[HTA_ERRLEN];
        if (hta_projectiles_equip(&s->proj, &s->cache,
                                  s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                                  &s->weap, perr, sizeof(perr))) {
            hta_log("[weapon] projectile: %u verts, %.1f wu/s, range %.0f, "
                    "blast %.2f", s->proj.verts_each, s->proj.speed_initial,
                    s->proj.range, s->proj.blast_radius);
            if (s->proj.detonation_snd) bank_get(s, s->proj.detonation_snd);
            if (s->gfx)
                s->gpu_proj = hta_gfx_mesh_upload_dynamic(s->gfx, &s->proj.mesh,
                                                          perr, sizeof(perr));
        }
        /* What its detonation throws out. Built once here, because
         * interning a texture mid-game would move the mesh under the
         * buffer the GPU is reading. */
        if (s->gpu_parts) {
            hta_gfx_mesh_free(s->gfx, s->gpu_parts);
            s->gpu_parts = NULL;
        }
        if (s->proj.det_effect &&
            hta_particles_load(&s->parts, &s->cache,
                               s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                               s->proj.det_effect, perr, sizeof(perr))) {
            hta_log("[weapon] detonation particles: %u type(s)", s->parts.type_count);
            if (s->gfx)
                s->gpu_parts = hta_gfx_mesh_upload_dynamic(s->gfx, &s->parts.mesh,
                                                           perr, sizeof(perr));
        }
    }

    hta_ammo_init(&s->ammo, &s->weap);
    /* A new weapon comes up unzoomed, and takes its own field of view. */
    s->zoom_level = 0;
    apply_zoom(s);

    /* Rebuild the viewmodel and everything hanging off it. */
    if (s->gpu_fp) { hta_gfx_mesh_free(s->gfx, s->gpu_fp); s->gpu_fp = NULL; }
    hta_viewmodel_free(&s->vm);
    s->have_fp = false;
    if (hta_viewmodel_load(&s->vm, &s->cache, bm, &s->weap, err, sizeof(err))) {
        s->have_fp = true;
        if (s->gfx)
            s->gpu_fp = hta_gfx_mesh_upload_dynamic(s->gfx, &s->vm.mesh, err, sizeof(err));
    } else {
        hta_log("[weapon] viewmodel: %s", err);
    }

    /* And the HUD, whose crosshair and ammo block are this weapon's. */
    if (s->gpu_hud) { hta_gfx_mesh_free(s->gfx, s->gpu_hud); s->gpu_hud = NULL; }
    hta_hud_free(&s->hud);
    hta_hud_load(&s->hud, &s->cache, bm, &s->weap, err, sizeof(err));
    hta_hud_set_shield(&s->hud, 1.0f);
    hta_hud_set_health(&s->hud, 1.0f);
    if (s->gfx && s->hud.elem_count)
        s->gpu_hud = hta_gfx_mesh_upload_dynamic(s->gfx, &s->hud.mesh, err, sizeof(err));

    hta_log("[weapon] %s: ROF %.1f/s  mag %d/%d  spread %.1f-%.1f deg  %s  %s  %s",
            s->weap.path, s->weap.rof, s->ammo.loaded, s->ammo.reserve,
            s->weap.error_angle[0] * 57.2957795f,
            s->weap.error_angle[1] * 57.2957795f,
            s->have_fp ? "viewmodel" : "NO viewmodel",
            s->vm.have_flash ? "flash" : "no flash",
            s->hud.have_cross ? "crosshair" : "no crosshair");
}

/* What the round hit. Halo keeps one response per material on the
 * projectile itself, each naming the effect -- so a bullet into sand and a
 * bullet into a base wall are the weapon's own two sounds, not one of
 * ours. */
static void play_impact(hta_android *s, uint8_t material)
{
    if (material >= 33u || !s->weap.projectile_id) return;
    if (!s->impact_known[material]) {
        s->impact_known[material] = 1;
        s->impact_snd[material] =
            hta_projectile_impact_sound(&s->cache, s->weap.projectile_id, material);
        if (s->impact_snd[material]) bank_get(s, s->impact_snd[material]);
    }
    if (s->impact_snd[material]) play_tag(s, s->impact_snd[material], 0.8f);
}

/* The footstep for what you are standing on. Halo keeps these in the
 * biped's own `foot` tag, one sound per material, and plenty of materials
 * have none -- silence is the right answer there, so remember that too. */
static void play_footstep(hta_android *s, uint8_t material)
{
    if (material >= 33u || !s->player.phys.footsteps_id) return;
    if (!s->foot_known[material]) {
        s->foot_known[material] = 1;
        s->foot_snd[material] =
            hta_material_effect_sound(&s->cache, s->player.phys.footsteps_id, 0u, material);
        if (s->foot_snd[material]) bank_get(s, s->foot_snd[material]);
    }
    if (s->foot_snd[material]) play_tag(s, s->foot_snd[material], 0.7f);
}


static bool find_named(hta_android *s, const char *name, char *out, size_t outlen)
{
    /* Reuse the map search directories: same folder as the cache, plus Download. */
    char dir[512];
    if (s->map_path[0]) {
        snprintf(dir, sizeof(dir), "%s", s->map_path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            char cand[520];
            snprintf(cand, sizeof(cand), "%s/%s", dir, name);
            if (file_exists(cand)) { snprintf(out, outlen, "%s", cand); return true; }
        }
    }
    static const char *public_dirs[] = {
        "/sdcard/halo-trial/maps", "/sdcard/halo-trial",
        "/sdcard/Download/halo-trial", "/sdcard/Download",
        "/storage/emulated/0/Download",
    };
    for (unsigned i = 0; i < sizeof(public_dirs)/sizeof(public_dirs[0]); i++) {
        char cand[520];
        snprintf(cand, sizeof(cand), "%s/%s", public_dirs[i], name);
        if (file_exists(cand)) { snprintf(out, outlen, "%s", cand); return true; }
    }
    const char *ext = s->app->activity->externalDataPath;
    if (ext) {
        char cand[520];
        snprintf(cand, sizeof(cand), "%s/%s", ext, name);
        if (file_exists(cand)) { snprintf(out, outlen, "%s", cand); return true; }
    }
    return false;
}

static bool load_map(hta_android *s)
{
    if (!find_map(s)) return false;
    hta_log("[assets] found %s", s->map_path);

    int fd = open(s->map_path, O_RDONLY);
    if (fd < 0) { snprintf(s->status, sizeof(s->status), "cannot open map"); return false; }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { close(fd); snprintf(s->status, sizeof(s->status), "cannot stat map"); return false; }

    /* mmap the cache read-only: no 700 MB copy, and the OS pages it in lazily */
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { snprintf(s->status, sizeof(s->status), "mmap failed"); return false; }
    s->map_data = (uint8_t *)p;
    s->map_size = (size_t)st.st_size;

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

    if (!hta_bsp_load_first(&s->cache, &s->mesh, err, sizeof(err))) {
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

    /* sounds.map, same external-resource pattern as bitmaps but type 2. */
    if (find_named(s, "sounds.map", s->sounds_path, sizeof(s->sounds_path))) {
        int sfd = open(s->sounds_path, O_RDONLY);
        struct stat sst;
        if (sfd >= 0 && fstat(sfd, &sst) == 0 && sst.st_size > 0) {
            void *sp = mmap(NULL, (size_t)sst.st_size, PROT_READ, MAP_PRIVATE, sfd, 0);
            close(sfd);
            if (sp != MAP_FAILED) {
                s->sounds_data = (uint8_t *)sp;
                s->sounds_size = (size_t)sst.st_size;
                if (hta_resource_open_typed(&s->sounds_rm, s->sounds_data, s->sounds_size,
                                            HTA_RESOURCE_SOUNDS, err, sizeof(err)))
                    hta_log("[assets] sounds.map %zu bytes from %s",
                            s->sounds_size, s->sounds_path);
                else
                    hta_log("[assets] sounds.map rejected: %s", err);
            }
        } else if (sfd >= 0) close(sfd);
    } else {
        hta_log("[assets] no sounds.map -- the game will be silent. Copy it next to bloodgulch.map");
    }

    /* Kept on the state: swapping weapons re-decodes their art, so the
     * resource map has to outlive load_map. */
    memset(&s->bitmaps_rm, 0, sizeof(s->bitmaps_rm));
    if (find_named(s, "bitmaps.map", s->bitmaps_path, sizeof(s->bitmaps_path))) {
        int bfd = open(s->bitmaps_path, O_RDONLY);
        struct stat bst;
        if (bfd >= 0 && fstat(bfd, &bst) == 0 && bst.st_size > 0) {
            void *bp = mmap(NULL, (size_t)bst.st_size, PROT_READ, MAP_PRIVATE, bfd, 0);
            close(bfd);
            if (bp != MAP_FAILED) {
                s->bitmaps_data = (uint8_t *)bp;
                s->bitmaps_size = (size_t)bst.st_size;
                if (hta_resource_open(&s->bitmaps_rm, s->bitmaps_data, s->bitmaps_size, err, sizeof(err)))
                    hta_log("[assets] bitmaps.map %zu bytes from %s", s->bitmaps_size, s->bitmaps_path);
                else
                    hta_log("[assets] bitmaps.map rejected: %s", err);
            }
        } else if (bfd >= 0) close(bfd);
    } else {
        hta_log("[assets] no bitmaps.map — world will stay untextured. Copy it next to bloodgulch.map");
    }
    if (!hta_bsp_load_textures(&s->cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL, &s->mesh, err, sizeof(err)))
        hta_log("[assets] texture load: %s", err);
    else
        hta_log("[assets] textures: %u unique (albedos+lightmaps)", s->mesh.texture_count);

    if (hta_bsp_load_collision(&s->cache, &s->coll_mesh, err, sizeof(err))) {
        s->have_coll = true;
        if (hta_scenario_add_collision(&s->coll_mesh, &s->cache, err, sizeof(err)))
            hta_log("[assets] %s", err);
        if (!hta_collision_build(&s->col, &s->coll_mesh))
            hta_log("[assets] collision BSP grid failed; %s", err);
        else
            hta_log("[assets] collision BSP %u verts / %u tris, grid %ux%u",
                    s->coll_mesh.vertex_count, s->coll_mesh.index_count / 3,
                    s->col.nx, s->col.ny);
    } else {
        hta_log("[assets] collision BSP: %s — using render mesh", err);
        if (!hta_collision_build(&s->col, &s->mesh))
            hta_log("[assets] collision grid failed to build; player will free-fly");
        else
            hta_log("[assets] collision grid %ux%u cells (render mesh)", s->col.nx, s->col.ny);
    }

    if (hta_scenario_add_objects(&s->mesh, &s->cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL, err, sizeof(err)))
        hta_log("[assets] %s  (now %u verts / %u submeshes)", err,
                s->mesh.vertex_count, s->mesh.submesh_count);
    if (!s->have_coll)
        hta_collision_rebind(&s->col, s->mesh.vertices, s->mesh.indices);

    s->bitmaps_ok = (s->bitmaps_rm.data != NULL);
    s->weapon_count = hta_weapon_list_playable(&s->cache, s->weapons,
                                               (uint32_t)(sizeof(s->weapons)/sizeof(s->weapons[0])));
    hta_log("[weapon] %u playable weapon(s) in this cache", s->weapon_count);
    /* Start on whatever the default picker prefers -- the assault rifle -- and
     * remember where it sits in the roster so SWAP carries on from there. */
    if (hta_weapon_load_default(&s->cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL, &s->weap, NULL, err, sizeof(err))) {
        for (uint32_t i = 0; i < s->weapon_count; i++) {
            hta_weapon_def probe;
            if (hta_weapon_load_id(&s->cache, NULL, s->weapons[i], &probe, NULL, NULL, 0) &&
                strcmp(probe.path, s->weap.path) == 0) { s->weapon_slot = i; break; }
        }
        equip_weapon(s, s->weap.path[0] ? s->weapons[s->weapon_slot] : 0);
    } else {
        hta_log("[weapon] %s", err);
    }
    if (hta_sky_load(&s->sky, &s->cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL, err, sizeof(err))) {
        s->have_sky = true;
        hta_log("[assets] sky %u verts / %u submeshes", s->sky.vertex_count, s->sky.submesh_count);
    } else {
        hta_log("[assets] sky: %s", err);
    }

    /* spawn at a real player start if the scenario has one */
    hta_spawn_point sp[64];
    uint32_t nsp = hta_scenario_spawns(&s->cache, sp, 64);
    hta_player_init(&s->player);
    {
        hta_player_physics phys;
        if (hta_player_physics_load(&phys, &s->cache, err, sizeof(err))) {
            hta_player_apply_physics(&s->player, &phys);
            s->cam.fov_y = phys.fov_y;
            s->base_fov = phys.fov_y;
            hta_collision_set_slope(&s->col, phys.max_slope);
            hta_log("[player] cyborg_mp run %.2f wu/s jump %.2f cam %.2f r %.2f slope %.0f deg",
                    phys.run_forward, phys.jump_speed, phys.cam_stand, phys.radius,
                    phys.max_slope * (180.0f / 3.14159265f));
            hta_log("[player] footsteps tag 0x%08X, step every %.2f wu",
                    phys.footsteps_id, HTA_STEP_LENGTH);
        } else {
            hta_log("[player] using fallback physics (%s)", err);
        }
    }
    if (nsp > 0) {
        hta_player_spawn(&s->player, &sp[0]);
        float gz;
        if (s->col.built &&
            hta_collision_ground(&s->col, s->player.pos[0], s->player.pos[1],
                                 s->player.pos[2] + 8.0f, &gz)) {
            s->player.pos[2] = gz;
            s->player.on_ground = true;
            hta_log("[assets] snapped spawn to ground z=%.2f", gz);
        }
        hta_log("[assets] %u spawn points; spawning at (%.2f %.2f %.2f)",
                nsp, s->player.pos[0], s->player.pos[1], s->player.pos[2]);
        s->cam.yaw = sp[0].facing;
    } else {
        s->player.pos[0] = 0.5f * (s->mesh.bounds_min[0] + s->mesh.bounds_max[0]);
        s->player.pos[1] = 0.5f * (s->mesh.bounds_min[1] + s->mesh.bounds_max[1]);
        s->player.pos[2] = s->mesh.bounds_max[2] + 1.0f;
        hta_log("[assets] no spawn points; starting above the centre of the BSP");
    }

    hta_scene_light_from_bsp(&s->mesh, s->scene.light_dir, s->scene.light_color, s->scene.ambient);
    s->scene.clear[0] = 0.42f; s->scene.clear[1] = 0.55f; s->scene.clear[2] = 0.72f;  /* sky-ish */

    s->have_mesh = true;
    s->map_loaded = true;
    snprintf(s->status, sizeof(s->status), "loaded %s", s->cache.name);
    return true;
}

/* ------------------------------- input ------------------------------- */

#define STICK_RADIUS_FRAC 0.12f   /* of the shorter screen edge */
#define LOOK_SENSITIVITY  0.006f
#define PAD_LOOK_SPEED    2.6f

static int32_t on_input(struct android_app *app, AInputEvent *event)
{
    hta_android *s = (hta_android *)app->userData;
    int32_t src  = AInputEvent_getSource(event);
    int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if ((src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
            float lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            float rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            float ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
            const float DEAD = 0.18f;
            s->pad_move[0] = fabsf(lx) > DEAD ? lx : 0.0f;
            s->pad_move[1] = fabsf(ly) > DEAD ? ly : 0.0f;
            s->pad_look[0] = fabsf(rx) > DEAD ? rx : 0.0f;
            s->pad_look[1] = fabsf(ry) > DEAD ? ry : 0.0f;
            {
                float rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
                s->pad_fire = rt > 0.35f;
            }
            return 1;
        }

        int32_t action = AMotionEvent_getAction(event);
        int32_t code   = action & AMOTION_EVENT_ACTION_MASK;
        int32_t pindex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                       >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        if (!app->window) return 1;
        float w = (float)ANativeWindow_getWidth(app->window);
        float h = (float)ANativeWindow_getHeight(app->window);
        if (w <= 0 || h <= 0) return 1;

        size_t count = AMotionEvent_getPointerCount(event);

        if (code == AMOTION_EVENT_ACTION_DOWN || code == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            if ((size_t)pindex < count) {
                int32_t id = AMotionEvent_getPointerId(event, (size_t)pindex);
                float x = AMotionEvent_getX(event, (size_t)pindex);
                float y = AMotionEvent_getY(event, (size_t)pindex);
                /* If the Java HUD is up it owns stick/fire/jump. Always keep
                 * native look so a missing overlay cannot freeze the camera. */
                if (!s->hud_ready && x < w * 0.5f && s->move_pointer < 0) {
                    s->move_pointer = id;
                    s->move_origin[0] = x; s->move_origin[1] = y;
                    s->move_cur[0] = x;    s->move_cur[1] = y;
                } else if (s->look_pointer < 0) {
                    if (!s->hud_ready && x > w * 0.82f && y > h * 0.72f)
                        s->jump_held = true;
                    else if (!s->hud_ready && x > w * 0.64f && y > h * 0.72f)
                        s->fire_held = true;
                    else {
                        s->look_pointer = id;
                        s->look_last[0] = x; s->look_last[1] = y;
                    }
                }
            }
        } else if (code == AMOTION_EVENT_ACTION_MOVE) {
            for (size_t i = 0; i < count; i++) {
                int32_t id = AMotionEvent_getPointerId(event, i);
                float x = AMotionEvent_getX(event, i);
                float y = AMotionEvent_getY(event, i);
                if (id == s->move_pointer) { s->move_cur[0] = x; s->move_cur[1] = y; }
                else if (id == s->look_pointer) {
                    s->pending_yaw   += -(x - s->look_last[0]) * LOOK_SENSITIVITY;
                    s->pending_pitch += -(y - s->look_last[1]) * LOOK_SENSITIVITY;
                    s->look_last[0] = x; s->look_last[1] = y;
                }
            }
        } else if (code == AMOTION_EVENT_ACTION_UP || code == AMOTION_EVENT_ACTION_POINTER_UP ||
                   code == AMOTION_EVENT_ACTION_CANCEL) {
            int32_t id = ((size_t)pindex < count) ? AMotionEvent_getPointerId(event, (size_t)pindex) : -1;
            if (code == AMOTION_EVENT_ACTION_CANCEL) {
                s->move_pointer = s->look_pointer = -1;
                s->jump_held = false;
                s->fire_held = false;
            } else {
                if (id == s->move_pointer) s->move_pointer = -1;
                if (id == s->look_pointer) s->look_pointer = -1;
                s->jump_held = false;
                s->fire_held = false;
            }
        }
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(event);
        bool down = (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN);
        if (code == AKEYCODE_BACK) {
            if (down) { hta_log("[input] BACK -> exit"); ANativeActivity_finish(app->activity); }
            return 1;
        }
        if (code == AKEYCODE_BUTTON_A || code == AKEYCODE_SPACE) { s->jump_held = down; return 1; }
        if (code == AKEYCODE_BUTTON_R1 || code == AKEYCODE_BUTTON_R2 ||
            code == AKEYCODE_BUTTON_X) { s->fire_held = down; return 1; }
        if (code == AKEYCODE_BUTTON_Y && down) { s->hud_reload = true; return 1; }
        if (code == AKEYCODE_BUTTON_R1 && down) { s->hud_melee = true; return 1; }
        if (code == AKEYCODE_BUTTON_L1 && down) { s->hud_swap = true; return 1; }
        if (code == AKEYCODE_BUTTON_THUMBR && down) { s->hud_zoom = true; return 1; }
        if (code == AKEYCODE_BUTTON_B && down) {
            s->player.noclip = !s->player.noclip;
            hta_log("[input] noclip %s", s->player.noclip ? "ON" : "OFF");
            return 1;
        }
        return 1;
    }
    return 0;
}

static void gather_input(hta_android *s, hta_player_input *in, float dt)
{
    memset(in, 0, sizeof(*in));

    /* Java HUD stick, or fallback invisible left-half stick */
    if (s->hud_ready) {
        in->move_right   += s->hud_move[0];
        in->move_forward += s->hud_move[1];
    } else if (s->move_pointer >= 0 && s->app->window) {
        float w = (float)ANativeWindow_getWidth(s->app->window);
        float h = (float)ANativeWindow_getHeight(s->app->window);
        float shorter = w < h ? w : h;
        float r = shorter * STICK_RADIUS_FRAC;
        if (r < 1.0f) r = 1.0f;
        float dx = (s->move_cur[0] - s->move_origin[0]) / r;
        float dy = (s->move_cur[1] - s->move_origin[1]) / r;
        float len = sqrtf(dx*dx + dy*dy);
        if (len > 1.0f) { dx /= len; dy /= len; }
        in->move_right   += dx;
        in->move_forward += -dy;   /* dragging up walks forward */
    }

    /* gamepad */
    in->move_right   += s->pad_move[0];
    in->move_forward += -s->pad_move[1];
    in->look_yaw   += -s->pad_look[0] * PAD_LOOK_SPEED * dt;
    in->look_pitch += -s->pad_look[1] * PAD_LOOK_SPEED * dt;

    /* accumulated touch look */
    in->look_yaw   += s->pending_yaw;
    in->look_pitch += s->pending_pitch;
    s->pending_yaw = s->pending_pitch = 0.0f;

    if (in->move_forward >  1.0f) in->move_forward =  1.0f;
    if (in->move_forward < -1.0f) in->move_forward = -1.0f;
    if (in->move_right   >  1.0f) in->move_right   =  1.0f;
    if (in->move_right   < -1.0f) in->move_right   = -1.0f;

    in->jump = s->jump_held || s->hud_jump;
    in->fire = s->fire_held || s->pad_fire || s->hud_fire;
    in->crouch = s->hud_crouch;
}

/* ------------------------------ lifecycle ------------------------------ */

static void start_gfx(hta_android *s)
{
    char err[HTA_ERRLEN];
    s->gfx = hta_gfx_create_window(s->app->window, err, sizeof(err));
    if (!s->gfx) { hta_log("[gfx] init FAILED: %s", err); s->has_window = false; return; }
    s->has_window = true;
    s->win_w = ANativeWindow_getWidth(s->app->window);
    s->win_h = ANativeWindow_getHeight(s->app->window);

    uint32_t w, h;
    hta_gfx_extent(s->gfx, &w, &h);
    hta_log("[gfx] ready: %s, %ux%u", hta_gfx_device_name(s->gfx), w, h);
    s->cam.aspect = h ? (float)w / (float)h : 1.777f;

    if (s->have_mesh) {
        double t0 = hta_time_seconds();
        s->gpu_mesh = hta_gfx_mesh_upload(s->gfx, &s->mesh, err, sizeof(err));
        if (!s->gpu_mesh) hta_log("[gfx] mesh upload FAILED: %s", err);
        else hta_log("[gfx] uploaded %u verts / %u tris in %.1f ms (device mem %.2f MiB)",
                     s->mesh.vertex_count, s->mesh.index_count/3,
                     (hta_time_seconds()-t0)*1000.0,
                     hta_gfx_device_memory_used(s->gfx)/(1024.0*1024.0));
        if (s->have_sky) {
            s->gpu_sky = hta_gfx_mesh_upload(s->gfx, &s->sky, err, sizeof(err));
            if (!s->gpu_sky) hta_log("[gfx] sky upload FAILED: %s", err);
        }
        if (s->gun.n) {
            hta_gun_build_mesh(&s->gun);
            s->gpu_fx = hta_gfx_mesh_upload(s->gfx, &s->gun.mesh, err, sizeof(err));
        if (s->proj.loaded && s->proj.mesh.index_count)
            s->gpu_proj = hta_gfx_mesh_upload_dynamic(s->gfx, &s->proj.mesh,
                                                      err, sizeof(err));
        if (s->parts.loaded && s->parts.mesh.index_count)
            s->gpu_parts = hta_gfx_mesh_upload_dynamic(s->gfx, &s->parts.mesh,
                                                       err, sizeof(err));
        }
        if (s->have_fp) {
            s->gpu_fp = hta_gfx_mesh_upload_dynamic(s->gfx, &s->vm.mesh, err, sizeof(err));
            if (!s->gpu_fp) hta_log("[gfx] fp weapon upload FAILED: %s", err);
        }
        if (s->hud.elem_count) {
            s->gpu_hud = hta_gfx_mesh_upload_dynamic(s->gfx, &s->hud.mesh, err, sizeof(err));
            if (!s->gpu_hud) hta_log("[gfx] hud upload FAILED: %s", err);
        }
        float span = s->mesh.bounds_max[0] - s->mesh.bounds_min[0];
        if (span < 1.0f) span = 1.0f;
        s->cam.zfar  = span * 6.0f;
        s->cam.znear = 0.02f;
    }
}

static void stop_gfx(hta_android *s)
{
    if (s->gpu_hud) { hta_gfx_mesh_free(s->gfx, s->gpu_hud); s->gpu_hud = NULL; }
    if (s->gpu_fp) { hta_gfx_mesh_free(s->gfx, s->gpu_fp); s->gpu_fp = NULL; }
    if (s->gpu_parts) { hta_gfx_mesh_free(s->gfx, s->gpu_parts); s->gpu_parts = NULL; }
    if (s->gpu_proj) { hta_gfx_mesh_free(s->gfx, s->gpu_proj); s->gpu_proj = NULL; }
    if (s->gpu_fx) { hta_gfx_mesh_free(s->gfx, s->gpu_fx); s->gpu_fx = NULL; }
    if (s->gpu_sky) { hta_gfx_mesh_free(s->gfx, s->gpu_sky); s->gpu_sky = NULL; }
    if (s->gpu_mesh) { hta_gfx_mesh_free(s->gfx, s->gpu_mesh); s->gpu_mesh = NULL; }
    if (s->gfx) { hta_gfx_destroy(s->gfx); s->gfx = NULL; }
    s->has_window = false;
}

/* SCREEN_ORIENTATION_SENSOR_LANDSCAPE = 6. SetupActivity is already locked;
 * NativeActivity must request it too or Samsung can keep the portrait window
 * that INIT_WINDOW first sees. */
static void request_landscape(struct android_app *app)
{
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK || !env) return;
    jclass cls = (*env)->GetObjectClass(env, app->activity->clazz);
    if (!cls) return;
    jmethodID mid = (*env)->GetMethodID(env, cls, "setRequestedOrientation", "(I)V");
    if (mid) {
        (*env)->CallVoidMethod(env, app->activity->clazz, mid, 6);
        hta_log("[app] requested SENSOR_LANDSCAPE");
    }
    (*env)->DeleteLocalRef(env, cls);
}

static void rebuild_gfx_if_size_changed(hta_android *s)
{
    if (!s->app->window) return;
    int w = ANativeWindow_getWidth(s->app->window);
    int h = ANativeWindow_getHeight(s->app->window);
    if (w <= 0 || h <= 0) return;
    if (s->gfx && s->win_w == w && s->win_h == h) return;
    hta_log("[app] window %dx%d (was %dx%d) -> rebuild", w, h, s->win_w, s->win_h);
    stop_gfx(s);
    start_gfx(s);
}

static void on_cmd(struct android_app *app, int32_t cmd)
{
    hta_android *s = (hta_android *)app->userData;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) {
            hta_log("[app] INIT_WINDOW %dx%d", ANativeWindow_getWidth(app->window),
                    ANativeWindow_getHeight(app->window));
            if (!s->probe_done) {
                /* Trial base first, then retail — see INVADER_ASSET_PIPELINE.md §2.3 */
                hta_probe_fixed_map(0x4BF10000ull, 23u * 1024u * 1024u);
                hta_probe_fixed_map(0x40440000ull, 23u * 1024u * 1024u);
                s->probe_done = true;
            }
            if (!s->map_loaded) {
                if (!load_map(s)) {
                    hta_log("[app] running without map data: %s", s->status);
                    /* Unmistakable on-screen signal: magenta means "no data".
                     * Sky blue means the map loaded. No text renderer yet. */
                    s->scene.clear[0] = 0.55f;
                    s->scene.clear[1] = 0.05f;
                    s->scene.clear[2] = 0.45f;
                }
            }
            start_gfx(s);
        }
        break;
    case APP_CMD_TERM_WINDOW: hta_log("[app] TERM_WINDOW"); stop_gfx(s); break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
        hta_log("[app] size/config cmd %d", (int)cmd);
        rebuild_gfx_if_size_changed(s);
        break;
    case APP_CMD_GAINED_FOCUS: hta_log("[app] focus gained"); break;
    case APP_CMD_LOST_FOCUS:   hta_log("[app] focus lost");   break;
    default: break;
    }
}

/* Where the pawn is, for the HUD to draw. Chasing a collision bug from a
 * screenshot needs coordinates: without them you are guessing which doorway
 * out of a 126 x 145 world unit map the reporter was standing in. */
static char g_debug_text[128];
static char g_ammo_text[32];

JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeDebugText(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, g_debug_text);
}

JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeAmmoText(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, g_ammo_text);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudReady(JNIEnv *env, jclass cls, jboolean ready)
{
    (void)env; (void)cls;
    g_hud_wanted = ready ? true : false;
    if (g_android) g_android->hud_ready = g_hud_wanted;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudMove(JNIEnv *env, jclass cls, jfloat x, jfloat y)
{
    (void)env; (void)cls;
    if (!g_android) return;
    g_android->hud_move[0] = x;
    g_android->hud_move[1] = y;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudLook(JNIEnv *env, jclass cls, jfloat dx, jfloat dy)
{
    (void)env; (void)cls;
    if (!g_android) return;
    g_android->pending_yaw   += -dx * LOOK_SENSITIVITY;
    g_android->pending_pitch += -dy * LOOK_SENSITIVITY;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudJump(JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    if (g_android) g_android->hud_jump = down ? true : false;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudFire(JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    if (g_android) g_android->hud_fire = down ? true : false;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudCrouch(JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    if (g_android) g_android->hud_crouch = down ? true : false;
}

/* A request, not a held button: the game loop consumes and clears it. */
JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudReload(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) g_android->hud_reload = true;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudMelee(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) g_android->hud_melee = true;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudSwap(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) g_android->hud_swap = true;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudZoom(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) g_android->hud_zoom = true;
}

void android_main(struct android_app *app)
{
    static hta_android state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    state.move_pointer = state.look_pointer = -1;
    g_android = &state;
    state.hud_ready = g_hud_wanted;

    app->userData     = &state;
    app->onAppCmd     = on_cmd;
    app->onInputEvent = on_input;

    hta_camera_init(&state.cam);
    hta_player_init(&state.player);
    hta_gun_init(&state.gun);
    state.rng = 0x9E3779B9u;
    /* Before any asset loading: the clip table lives in the mixer, and
     * hta_audio_init clears it, so the stream has to exist first. */
    state.audio_ok = hta_audio_android_start(&state.audio);
    if (state.audio_ok) {
        /* The AR fires 15/s and its gunshot is 0.70 s long, so ten shots
         * overlap in steady fire. At unity that sums straight into the
         * clamp and turns into a buzz; leave headroom instead. */
        state.audio.master_gain = 0.45f;
    } else {
        hta_log("[audio] no output stream; running silent");
    }
    state.last_time = hta_time_seconds();

    hta_log("[app] android_main; pointer size %zu bytes", sizeof(void *));
    hta_log("[app] external data path: %s",
            app->activity->externalDataPath ? app->activity->externalDataPath : "(null)");
    request_landscape(app);

    while (1) {
        int events;
        struct android_poll_source *source;
        int timeout = state.has_window ? 0 : -1;
        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) goto done;
            timeout = 0;
        }

        double now = hta_time_seconds();
        float dt = (float)(now - state.last_time);
        state.last_time = now;

        hta_player_input in;
        gather_input(&state, &in, dt);
        hta_player_update(&state.player, &state.cam,
                          state.col.built ? &state.col : NULL, &in, dt);
        if (state.player.footstep && state.col.built) {
            uint8_t mat = hta_collision_ground_material(&state.col,
                                                        state.player.pos[0],
                                                        state.player.pos[1],
                                                        state.player.pos[2] + 0.1f);
            play_footstep(&state, mat);
        }
        hta_gun_update(&state.gun, dt);
        /* Ammo gates the shot: hta_gun_fire spends the cooldown whether or
         * not the magazine could pay, so ask before pulling. */
        hta_ammo_update(&state.ammo, dt);
        /* A shell-at-a-time reload chains on its own, and every shell after
         * the first was being loaded silently with no animation -- which is
         * most of a shotgun reload from empty. Each one replays the clip. */
        if (state.ammo.reload_began && state.vm.loaded)
            hta_viewmodel_play(&state.vm, HTA_VM_RELOAD);
        if (state.dry_cooldown > 0.0f) state.dry_cooldown -= dt;

        /* Swapping rebuilds the viewmodel and the HUD, so do it before
         * anything this frame reads either. */
        if (state.hud_swap) {
            state.hud_swap = false;
            if (state.weapon_count > 1) {
                state.weapon_slot = (state.weapon_slot + 1u) % state.weapon_count;
                equip_weapon(&state, state.weapons[state.weapon_slot]);
            }
        }

        if (state.hud_zoom) {
            state.hud_zoom = false;
            cycle_zoom(&state);
        }

        /* A swing takes the weapon out of the fight until it finishes, so
         * the rest of this frame's trigger work has to know about it. */
        bool swinging = state.vm.loaded && state.vm.state == HTA_VM_MELEE;
        if (state.hud_melee) {
            state.hud_melee = false;
            if (!swinging && state.ammo.phase != HTA_AMMO_RELOADING) {
                hta_viewmodel_play(&state.vm, HTA_VM_MELEE);
                swinging = state.vm.state == HTA_VM_MELEE;
            }
        }

        if (state.hud_reload) {
            state.hud_reload = false;
            if (!swinging && hta_ammo_reload(&state.ammo))
                hta_viewmodel_play(&state.vm, HTA_VM_RELOAD);
        }
        if (in.fire && !swinging && hta_gun_ready(&state.gun)) {
            if (hta_ammo_shoot(&state.ammo)) {
                if (state.proj.loaded) {
                    /* An object round does its own collision on the way, so
                     * there is no hitscan to trace and no impact yet. */
                    float dir[3];
                    if (hta_gun_launch(&state.gun, &state.cam, dir)) {
                        float muzzle[3];
                        for (int k = 0; k < 3; k++)
                            muzzle[k] = state.cam.pos[k] + dir[k] * 0.35f;
                        hta_projectiles_fire(&state.proj, muzzle, dir);
                    }
                } else {
                    hta_gun_fire(&state.gun, state.col.built ? &state.col : NULL,
                                 &state.cam);
                    play_impact(&state, state.gun.hit_material);
                }
                hta_viewmodel_play(&state.vm, HTA_VM_FIRE);
                hta_viewmodel_flash(&state.vm);
                play_tag(&state, state.fire_snd, 1.0f);
            } else if (state.ammo.dry && state.dry_cooldown <= 0.0f) {
                /* Click, then reload by itself, the way Halo does. */
                play_tag(&state, state.empty_snd, 1.0f);
                state.dry_cooldown = 0.35f;
                if (hta_ammo_reload(&state.ammo))
                    hta_viewmodel_play(&state.vm, HTA_VM_RELOAD);
            }
        }
        /* A continuous weapon sounds while the trigger is actually doing
         * something, and goes quiet the moment it is released, the magazine
         * runs out, or a swing takes the weapon out of the fight. */
        fire_loop(&state, in.fire && !swinging &&
                          state.ammo.phase == HTA_AMMO_READY &&
                          state.ammo.loaded >= state.ammo.per_shot);

        /* The gun carries its own round counter, and the HUD carries the
         * same magazine as a grid of pips. */
        hta_viewmodel_set_counter(&state.vm, (uint32_t)state.ammo.loaded);
        if (state.ammo.mag_max > 0) {
            float full = (float)state.ammo.loaded / (float)state.ammo.mag_max;
            hta_hud_set_ammo(&state.hud, full);
            /* Halo's HUD counter is the TOTAL carried: the magazine plus
             * the reserve. The pips are the magazine, the gun's own readout
             * is the magazine. */
            hta_hud_set_number(&state.hud, state.ammo.loaded + state.ammo.reserve);
            /* And the needler wears its magazine: its needles fold away as
             * it empties and spring back on the reload. */
            hta_viewmodel_set_ammo(&state.vm, full);
        }
        if (state.ammo.reload_done)
            hta_log("[weapon] reloaded: %d / %d", state.ammo.loaded, state.ammo.reserve);
        /* The animation graph fires a snd! id when a clip crosses its sound
         * frame -- reload clacks, the weapon-ready rack. Nothing consumed it
         * until now. */
        if (state.vm.sound_cue) {
            play_tag(&state, state.vm.sound_cue, 1.0f);
            state.vm.sound_cue = 0;
        }
        hta_audio_android_poll(&state.audio);
        hta_viewmodel_update(&state.vm, dt);
        /* Rounds in flight. A detonation leaves the same scorch and plays
         * the same material impact a hitscan round would. */
        if (state.proj.loaded) {
            hta_projectiles_update(&state.proj,
                                   state.col.built ? &state.col : NULL, dt);
            if (state.proj.detonated) {
                hta_gun_add_mark(&state.gun, state.proj.hit, state.proj.hit_normal,
                                 state.proj.blast_radius);
                /* An explosion has a bang of its own; a round that does not
                 * falls back to what the surface it hit sounds like. */
                if (state.proj.detonation_snd)
                    play_tag(&state, state.proj.detonation_snd, 1.0f);
                else
                    play_impact(&state, state.proj.hit_material);
                /* Thrown out along the surface it hit. */
                hta_particles_burst(&state.parts, state.proj.hit,
                                    state.proj.hit_normal);
            }
        }

        hta_particles_update(&state.parts, &state.cam, dt);

        if (state.gun.dirty && state.gfx) {
            char err[HTA_ERRLEN];
            hta_gun_build_mesh(&state.gun);
            if (state.gpu_fx) { hta_gfx_mesh_free(state.gfx, state.gpu_fx); state.gpu_fx = NULL; }
            if (state.gun.mesh.index_count)
                state.gpu_fx = hta_gfx_mesh_upload(state.gfx, &state.gun.mesh, err, sizeof(err));
        }

        if (state.has_window && state.gfx) {
            rebuild_gfx_if_size_changed(&state);
            if (!state.gfx) continue;
            hta_gfx_viewmodel vmdraw;
            memset(&vmdraw, 0, sizeof(vmdraw));
            hta_gfx_overlay huddraw;
            memset(&huddraw, 0, sizeof(huddraw));
            if (state.gpu_hud) {
                uint32_t ew = 0, eh = 0;
                hta_gfx_extent(state.gfx, &ew, &eh);
                hta_hud_layout(&state.hud, ew, eh);
                huddraw.mesh = state.gpu_hud;
                huddraw.vertices = state.hud.mesh.vertices;
                huddraw.vertex_count = state.hud.mesh.vertex_count;
                huddraw.submeshes = state.hud.mesh.submeshes;
                huddraw.submesh_count = state.hud.mesh.submesh_count;
            }
            /* Scoped means looking THROUGH the weapon, so Halo takes it
             * off the screen entirely while zoomed. Without this the sniper
             * reads as a magnified view with a rifle in front of it. */
            if (state.gpu_fp && state.zoom_level == 0) {
                vmdraw.mesh = state.gpu_fp;
                vmdraw.vertices = state.vm.posed;
                vmdraw.vertex_count = state.vm.mesh.vertex_count;
                for (int k = 0; k < 3; k++) vmdraw.offset[k] = state.weap.fp_offset[k];
            }
            hta_gfx_dynamic dynlist[2];
            uint32_t dyncount = 0;
            if (state.gpu_proj) {
                dynlist[dyncount].mesh = state.gpu_proj;
                dynlist[dyncount].vertices = state.proj.mesh.vertices;
                dynlist[dyncount].vertex_count = state.proj.mesh.vertex_count;
                dyncount++;
            }
            if (state.gpu_parts) {
                dynlist[dyncount].mesh = state.gpu_parts;
                dynlist[dyncount].vertices = state.parts.mesh.vertices;
                dynlist[dyncount].vertex_count = state.parts.mesh.vertex_count;
                dyncount++;
            }
            if (!hta_gfx_draw(state.gfx, &state.cam, &state.scene, state.gpu_mesh,
                              state.gpu_sky, state.gpu_fx,
                              dynlist, dyncount,
                              vmdraw.mesh ? &vmdraw : NULL,
                              state.gpu_hud ? &huddraw : NULL)) {
                hta_log("[app] surface lost; rebuilding renderer");
                stop_gfx(&state);
                if (app->window) start_gfx(&state);
            }
            state.frames++;
            state.fps_accum += dt;
            state.fps_frames++;
            if (state.ammo.phase == HTA_AMMO_RELOADING)
                snprintf(g_ammo_text, sizeof(g_ammo_text), "-- / %d", state.ammo.reserve);
            else
                snprintf(g_ammo_text, sizeof(g_ammo_text), "%d / %d",
                         state.ammo.loaded, state.ammo.reserve);
            snprintf(g_debug_text, sizeof(g_debug_text),
                     "%.2f %.2f %.2f  %s  %.0f fps",
                     state.player.pos[0], state.player.pos[1], state.player.pos[2],
                     state.player.on_ground ? "ground" : "air",
                     state.fps_accum > 0.05 ? state.fps_frames / state.fps_accum : 0.0);
            if (state.fps_accum >= 2.0) {
                hta_log("[perf] %.1f fps | pos (%.2f %.2f %.2f) %s | tris %u"
                        " | audio %s %u voices %u started %u dropped",
                        state.fps_frames / state.fps_accum,
                        state.player.pos[0], state.player.pos[1], state.player.pos[2],
                        state.player.on_ground ? "grounded" : "airborne",
                        state.have_mesh ? state.mesh.index_count / 3 : 0,
                        hta_audio_android_running() ? "on" : "off",
                        hta_audio_active_voices(&state.audio),
                        state.audio.started,
                        (unsigned)atomic_load(&state.audio.dropped));
                hta_log("[weapon] spread %.2f deg", hta_gun_spread(&state.gun) * 57.2957795f);
                hta_log("[weapon] ammo %d / %d%s", state.ammo.loaded,
                        state.ammo.reserve,
                        state.ammo.phase == HTA_AMMO_RELOADING ? " (reloading)" : "");
                state.fps_accum = 0.0;
                state.fps_frames = 0;
            }
        }
    }

done:
    hta_log("[app] shutting down after %llu frames", (unsigned long long)state.frames);
    /* Stop the stream before freeing the PCM its voices point at. */
    hta_audio_android_stop();
    hta_hud_free(&state.hud);
    for (uint32_t i = 0; i < state.bank_count; i++)
        for (uint32_t k = 0; k < state.bank[i].count; k++)
            free(state.bank[i].pcm[k]);
    state.bank_count = 0;
    stop_gfx(&state);
    hta_collision_free(&state.col);
    hta_gun_free(&state.gun);
    hta_projectiles_free(&state.proj);
    hta_particles_free(&state.parts);
    hta_bsp_free(&state.mesh);
    hta_bsp_free(&state.sky);
    hta_bsp_free(&state.coll_mesh);
    hta_viewmodel_free(&state.vm);
    if (state.map_data) munmap(state.map_data, state.map_size);
    if (state.bitmaps_data) munmap(state.bitmaps_data, state.bitmaps_size);
    if (state.sounds_data) munmap(state.sounds_data, state.sounds_size);
}
