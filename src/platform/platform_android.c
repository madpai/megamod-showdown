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
#include "../app/session.h"
#include "../app/report.h"
#include "../app/host_net.h"
#include "../app/input.h"
#include "../app/fs.h"
#include "../app/content.h"
#include "../app/match_load.h"
#include <dlfcn.h>
#include <signal.h>
#include <unwind.h>
#include "../engine/engine.h"
#include "../engine/camera.h"
#include "../engine/player.h"
#include "../engine/gun.h"
#include "../engine/projectile.h"
#include "../engine/particle.h"
#include "../engine/vitals.h"
#include "../asset/dialogue.h"
#include "../engine/actor.h"
#include "../engine/pickup.h"
#include "../engine/bot.h"
#include "../engine/vehicle.h"
#include "../engine/contrail.h"
#include "../engine/shake.h"
#include <stdatomic.h>
#include <pthread.h>
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
#include "../net/session.h"
#include "../net/replication.h"
#include "../game/game.h"
#include "../game/view.h"
#include "../game/nav.h"
#include "../game/menu.h"
#include "../asset/items.h"
#include "../asset/strings.h"
#include "../asset/external_map.h"
#include "../game/external_world.h"
#include "../game/imported.h"
#include "../asset/oal_asset.h"
#include "../gfx/gfx_settings.h"
#include "../game/world_fx_audio.h"
#include "../game/world_fx_gpu.h"

#include <android/log.h>
#include <android/input.h>
#include <android/asset_manager.h>
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

/* Diagnostics: what the game was doing, for a crash record (see report_native). */
static const char *volatile g_phase = "start";   /* what the game was doing */

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

/* Tags, not clips -- see HTA_AUDIO_MAX_CLIPS. Weapon fire, dry-fire, zoom
 * in and out, one impact per material the map contains, the projectile's
 * detonation, the grenade's, and a footstep per material: Blood Gulch asks
 * for around thirty of these, and 24 was not enough to hold them. */

typedef struct {
    /* The match (src/app/session.h): read as s->game here, or whole as
     * s->session by code that must not know it runs on Android. */
    union {
        struct { HTA_SESSION_FIELDS };
        hta_session session;
    };

    /* Android's own: the app, APK mappings, the window, touch. */
    struct android_app *app;
    /* What was mapped, for unmapping: an APK asset maps from a page
     * boundary before its data, so the pointer handed out is not the base. */
    void     *mapped_base[8];
    size_t    mapped_len[8];
    uint32_t  mapped_count;
    bool          has_window;
    int32_t       win_w, win_h;  /* window size the current swapchain was built for */
    bool          probe_done;
    hta_fs        fs;        /* the APK, then app storage (android_fs_init) */

    /* input */
    int32_t move_pointer, look_pointer;
    float   move_origin[2], move_cur[2];
    float   look_last[2];
    float   pad_move[2], pad_look[2];
    bool    jump_held;
    bool    fire_held;
    bool    pad_fire;
    float   pending_yaw, pending_pitch;

    bool    prev_jump_held, prev_fire_held;   /* for their press edges */

    /* Java HUD (GameActivity). When ready, touch move/jump/fire/look
     * come from JNI instead of hot-corners; what JNI sends waits in
     * g_hud_in until the frame takes it (android_read_input). */
    bool    hud_ready;
} hta_android;
_Static_assert(offsetof(hta_android, session) == 0,
               "host_unit_added casts a session back to its hta_android");
_Static_assert(offsetof(hta_android, game) == offsetof(hta_android, session.game),
               "hta_android's session fields and hta_session must share a layout");

static hta_android *g_android;
/* 0 walk, 1 near a free seat, 2 driving, 3 gunning, 4 riding armed,
 * 5 riding; +16 when the seat has a second trigger. */
static _Atomic int g_vehicle_mode;
static char g_vehicle_text[96];
/* 0..255 edge flash, read by the Java overlay on its own thread. */
static _Atomic int g_damage_flash;
#define HTA_DAMAGE_FLASH_TIME 0.55f
/* 0 solo, 1 joining, 2 in match, 3 hosting alone, 4 hosting with peer,
 * 5 unavailable, 6 connected without match state, 7 map mismatch, 8 full. */
static _Atomic int g_net_status;
/* The pause screen is up: the world holds still and the HUD shows it. */
static _Atomic int g_paused;
/* The killcam's still frame, for the Java HUD: "name<TAB>weapon<TAB>health%". */
static char g_killcam_text[128];
static _Atomic int g_killcam_ready;

/* Which submenu the Java overlay has up: 0 none (the main menu), 1 solo,
 * 2 multiplayer. It decides the camera shot and what BACK does. */
static _Atomic int g_shell_screen;
static hta_shell g_shell;
static _Atomic int g_shell_ready;
static int g_shell_shown = -1;
static void call_activity(hta_android *s, const char *method);
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
/* Built-in Trial data. A personal build (publish_apk.sh --with-assets)
 * carries the owner's own maps uncompressed under assets/maps/, and they
 * are mapped straight out of the APK -- no copy, no picker. A path of the
 * form "apk:maps/<name>" names one of those. */
#define HTA_APK_PREFIX "apk:"

static bool apk_has(hta_android *s, const char *name)
{
    AAssetManager *am = s->app->activity->assetManager;
    if (!am) return false;
    AAsset *a = AAssetManager_open(am, name, AASSET_MODE_UNKNOWN);
    if (!a) return false;
    off64_t start = 0, len = 0;
    int fd = AAsset_openFileDescriptor64(a, &start, &len);
    AAsset_close(a);
    if (fd < 0) {
        hta_log("[assets] %s is in the APK but compressed; it cannot be mapped", name);
        return false;
    }
    close(fd);
    return len > 0;
}

/* The APK as an hta_fs root (app/fs.h): an asset stored uncompressed is
 * mapped straight out of the file; a compressed one is copied. */
static bool apk_fs_map(void *ctx, const char *rel, hta_fs_blob *out)
{
    AAssetManager *am = ctx;
    memset(out, 0, sizeof(*out));
    AAsset *a = am ? AAssetManager_open(am, rel, AASSET_MODE_UNKNOWN) : NULL;
    if (!a) return false;
    off64_t start = 0, len = 0;
    int fd = AAsset_openFileDescriptor64(a, &start, &len);
    if (fd < 0) {
        size_t n = (size_t)AAsset_getLength(a);
        const void *buf = AAsset_getBuffer(a);
        void *copy = buf && n ? malloc(n) : NULL;
        if (copy) memcpy(copy, buf, n);
        AAsset_close(a);
        if (!copy) return false;
        out->data = copy; out->size = n; out->base = copy; out->heap = true;
        return true;
    }
    AAsset_close(a);
    if (len <= 0) { close(fd); return false; }
    long page = sysconf(_SC_PAGESIZE);
    off64_t aligned = start - (start % page);
    size_t maplen = (size_t)(len + (start - aligned));
    void *p = mmap(NULL, maplen, PROT_READ, MAP_PRIVATE, fd, aligned);
    close(fd);
    if (p == MAP_FAILED) return false;
    out->data = (const uint8_t *)p + (start - aligned);
    out->size = (size_t)len;
    out->base = p;
    out->base_len = maplen;
    return true;
}

static void apk_fs_list(void *ctx, const char *rel_dir, hta_fs_name_fn fn, void *user)
{
    AAssetDir *d = ctx ? AAssetManager_openDir(ctx, rel_dir) : NULL;
    if (!d) return;
    const char *n;
    while ((n = AAssetDir_getNextFileName(d))) fn(user, n);
    AAssetDir_close(d);
}

/* Content by name: the APK's assets first, then the app's own storage
 * (the picked external.oalmap lives there). */
static void android_fs_init(hta_android *s)
{
    hta_fs_init(&s->fs);
    hta_fs_mount(&s->fs, "", apk_fs_map, apk_fs_list, s->app->activity->assetManager);
    const char *store = s->app->activity->externalDataPath;
    if (!store) store = s->app->activity->internalDataPath;
    if (store) hta_fs_mount_dir(&s->fs, "", store);
}

/* Maps a data file read-only, from disk or from the APK, and keeps it
 * mapped for the run (the Trial's cache keeps pointers into it). */
static bool map_data_file(hta_android *s, const char *path, uint8_t **out, size_t *out_size)
{
    if (s->mapped_count >= 8) return false;
    hta_fs_blob b;
    if (!strncmp(path, HTA_APK_PREFIX, strlen(HTA_APK_PREFIX))) {
        if (!apk_fs_map(s->app->activity->assetManager, path + strlen(HTA_APK_PREFIX), &b))
            return false;
        if (b.heap) { hta_fs_unmap(&b); return false; }   /* compressed: apk_has says so */
    } else if (!hta_fs_map_path(path, &b)) {
        return false;
    }
    if (!b.base) return false;                              /* an empty file */
    s->mapped_base[s->mapped_count] = b.base;
    s->mapped_len[s->mapped_count] = b.base_len;
    s->mapped_count++;
    *out = (uint8_t *)b.data;
    *out_size = b.size;
    return true;
}

static bool find_map(hta_android *s)
{
    if (apk_has(s, "maps/bloodgulch.map")) {
        snprintf(s->map_path, sizeof(s->map_path), HTA_APK_PREFIX "maps/bloodgulch.map");
        hta_log("[assets] using the Trial data built into this APK");
        return true;
    }
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
    if (s->bank_count >= HTA_SND_MAX_BANK) {
        /* Never let this starve quietly again: it cost three weapons their
         * sound and looked like a decoder bug. */
        hta_log("[audio] sound bank FULL at %u tags; 0x%08X will be silent",
                s->bank_count, tag_id);
        return -1;
    }

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
        if (clip == HTA_AUDIO_NO_CLIP) {
            hta_log("[audio] clip table FULL; 0x%08X keeps %u of %u "
                    "permutation(s)", tag_id, s->bank[idx].count, want);
            hta_pcm_free(&pcm);
            break;
        }
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

/* How hard a grenade is thrown.
 *
 * INVENTED -- the fourth number in this project that is. The frag
 * grenade's own projectile tag says an initial velocity of 0.00, because
 * in Halo the throw comes from the player rather than the tag, and the
 * player's throw strength is an engine constant that is not in the data.
 * Nine world units a second puts one about twenty-five out on a flat
 * throw, which is roughly the distance it goes in the real game. */
#define HTA_GRENADE_THROW 9.0f

/* How far a world sound carries.
 *
 * INVENTED, and the third number in this project that is. Halo keeps a
 * minimum and maximum distance on every `snd!` (at +8 and +12) -- and in
 * the Trial every single one of them reads 0.0 .. 0.0, because the real
 * values live in per-CLASS defaults inside the engine rather than in the
 * data. The tag's `sound class` field is set (weapon fire 4, projectile
 * impact 0, object impacts 13) but the ranges those map to are not
 * shippable data we have.
 *
 * So: full volume within three world units, inverse falloff after that,
 * silent at sixty -- which is about the length of Blood Gulch. If the
 * class ranges ever turn up, these two lines are what to replace. */
#define HTA_SOUND_NEAR  3.0f
#define HTA_SOUND_FAR  60.0f

/* A sound that happens somewhere in the world rather than in your hands:
 * quieter with distance, and placed left or right of where you are
 * looking. */
static bool world_voice(hta_android *s, const float at[3], float gain, float *g_out, float *pan_out)
{
    float d[3] = { at[0] - s->cam.pos[0], at[1] - s->cam.pos[1], at[2] - s->cam.pos[2] };
    float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (dist >= HTA_SOUND_FAR) return false;
    float g = gain;
    if (dist > HTA_SOUND_NEAR) { g *= HTA_SOUND_NEAR / dist; g *= 1.0f - dist / HTA_SOUND_FAR; }
    if (g <= 0.001f) return false;
    float pan = 0.0f;
    if (dist > 0.01f) {
        float right[3];
        hta_camera_right(&s->cam, right);
        pan = (d[0]*right[0] + d[1]*right[1] + d[2]*right[2]) / dist;
    }
    *g_out = g; *pan_out = pan;
    return true;
}

static void play_tag_at(hta_android *s, uint32_t tag_id, const float at[3],
                        float gain)
{
    if (!tag_id || !at) return;
    float d[3] = { at[0] - s->cam.pos[0],
                   at[1] - s->cam.pos[1],
                   at[2] - s->cam.pos[2] };
    float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (dist >= HTA_SOUND_FAR) return;               /* too far to hear */

    float g = gain;
    if (dist > HTA_SOUND_NEAR) {
        g *= (HTA_SOUND_NEAR / dist);                /* inverse falloff */
        g *= (1.0f - dist / HTA_SOUND_FAR);          /* and reach zero cleanly */
    }
    if (g <= 0.001f) return;

    float pan = 0.0f;
    if (dist > 0.01f) {
        float right[3];
        hta_camera_right(&s->cam, right);
        pan = (d[0]*right[0] + d[1]*right[1] + d[2]*right[2]) / dist;
    }

    int b = bank_get(s, tag_id);
    if (b < 0) return;
    uint32_t n = s->bank[b].count;
    s->rng = s->rng * 1664525u + 1013904223u;
    uint32_t pick = n > 1 ? (s->rng >> 16) % n : 0u;
    hta_audio_play_pan(&s->audio, s->bank[b].clip[pick], g, pan);
}

/* The one continuous voice we keep; any non-zero id would do. */
#define HTA_LOOP_FIRE 1u

/* The unit HUD's own sounds. Halo hangs these off the `unhi`, latched to a
 * condition each: the shield charging back up, the hit that broke it, the
 * warning tones. Loop ids of their own so they can sound together -- the
 * heartbeat under the recharge hum is exactly right. */
#define HTA_LOOP_SHIELD_CHARGE  2u
#define HTA_LOOP_SHIELD_LOW     3u
#define HTA_LOOP_HEALTH_LOW     4u

/* When "low" starts. Halo has no threshold for this in any tag -- the HUD
 * flashes and the heartbeat starts on a hardcoded fraction -- so this one
 * is ours. A quarter left is about where the real game begins to nag. */
#define HTA_VITALS_LOW  0.25f

/* Holds one of those loops while its condition lasts. Safe to call every
 * frame: the mixer leaves a running loop alone rather than restarting it. */
static void hud_loop(hta_android *s, uint32_t loop_id, uint32_t snd, bool on,
                     bool *state)
{
    if (!snd) return;
    if (on) {
        int b = bank_get(s, snd);
        if (b < 0) return;
        hta_audio_loop(&s->audio, loop_id, s->bank[b].clip[0], 1.0f);
        *state = true;
    } else if (*state) {
        hta_audio_loop_stop(&s->audio, loop_id);
        *state = false;
    }
}

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
/* Overshield stacks on top of your own, and drains back down over the time
 * the equipment tag says -- 60 s for the overshield, 45 for camouflage. The
 * MULTIPLIER is ours: Halo's overshield is famously "three bars", and the
 * tag says how long it lasts but not how much it gives. */
#define HTA_OVERSHIELD_MULT 3.0f

/* How much of a blast reaches a point: full inside the core, tapering to
 * nothing at the edge. The player already had this inline twice; a rocket
 * and a grenade now have to ask it about a body as well. */
static float blast_falloff(const float centre[3], const float at[3],
                           float core, float radius)
{
    float dx = at[0] - centre[0];
    float dy = at[1] - centre[1];
    float dz = at[2] - centre[2];
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist >= radius) return 0.0f;
    if (dist <= core || radius <= core) return 1.0f;
    return 1.0f - (dist - core) / (radius - core);
}

/* How far a swing reaches, in world units -- about a metre and a half.
 * Ours: no tag carries a melee range, only what the blow does. */
#define HTA_MELEE_REACH 0.5f

/* Halo's multiplayer respawn. The five seconds are the GAMETYPE's, not any
 * tag's -- no gametype ships inside a map -- so this one number is ours. The
 * fade is shaped around it: black by the time the body has settled, black
 * while you wait, and open again as you come back. */
#define HTA_RESPAWN_DELAY   5.0f
#define HTA_RESPAWN_MIN     1.5f   /* "INSTANT": the fall, then the fade. Ours */
/* The black comes at the END, not the start. Fading out as you die would
 * mean the body you have just been shown is on screen for half a second
 * before the screen swallows it; Halo lets you watch the whole time and
 * only closes the shot to cover the respawn. */
#define HTA_DEATH_FADE_OUT  0.8f   /* seconds of black BEFORE coming back */
#define HTA_DEATH_FADE_IN   0.6f   /* and back, once you are standing */
/* How far the camera sinks as the body goes down, in world units. The
 * Trial's cyborg stands with its eye 0.62 above its feet. */
#define HTA_DEATH_EYE_DROP  0.45f
/* Where the camera goes to watch. World units: 1 wu is 3.05 m, so this
 * settles about 6 m behind the body and 2.5 m above it, aimed at its chest.
 * Halo's own death camera is closer than that, but Halo's is looking at a
 * ragdoll that is still moving -- ours holds the last frame of a kill
 * animation, and a little distance is kinder to it. */
#define HTA_DEATH_CAM_BACK  2.0f
#define HTA_DEATH_CAM_UP    0.8f
#define HTA_DEATH_LOOK_AT   0.35f   /* up the body from its feet, to the chest */
#define HTA_DEATH_PULLBACK  1.2f    /* seconds for the camera to get there */

static void equip_weapon(hta_android *s, uint32_t weap_tag_id);
static void equip_weapon_tag(hta_android *s, uint32_t weap_tag_id);
static int32_t held_roster(hta_android *s);
static const hta_game_weapon *held_imported(hta_android *s);
static int imported_index(const hta_android *s, const hta_oal_asset *a);

/* How far above a start to look down for its floor. Blood Gulch's starts
 * are in the open and some sit a little under the ground, so 8 wu; an
 * imported map's are already on the floor, under arches and ceilings. */
static float spawn_lift(const hta_android *s) { return s->world_loaded ? HTA_EXTERNAL_SPAWN_LIFT : 8.0f; }

/* The first-person clip for a viewmodel state, on whichever view is up:
 * Halo's own, or the imported weapon's. */
static void vm_play(hta_android *s, hta_vm_state st)
{
    if (s->vm.loaded) hta_viewmodel_play(&s->vm, st);
    s->ivm_role = st == HTA_VM_FIRE ? "fire" : st == HTA_VM_RELOAD ? "reload" : "idle";
    {
        /* A bat's swing is its "fire" clip. */
        const hta_game_weapon *mw = held_imported(s);
        if (st == HTA_VM_MELEE && mw && mw->melee_only) {
            s->ivm_role = "fire";
            int k = imported_index(s, mw->asset);     /* and its swoosh */
            if (k >= 0 && s->imp_clip[k][0] != HTA_AUDIO_NO_CLIP) hta_audio_play(&s->audio, s->imp_clip[k][0], 0.9f);
        }
    }
    s->ivm_clip = -2;
    s->ivm_rate = 1.0f;
    const hta_game_weapon *w = held_imported(s);
    /* A reload clip is played to last exactly as long as the reload does:
     * one round's worth for a round-at-a-time weapon, replayed per round. */
    if (w && st == HTA_VM_RELOAD && s->ammo.reload_time > 0.05f) {
        const hta_oal_model *m = &w->asset->models[1];
        float len = hta_oal_clip_length(m, hta_oal_clip_find(m, "reload"));
        if (len > 0.05f) s->ivm_rate = len / s->ammo.reload_time;
    }
    if (w && st == HTA_VM_RELOAD) {
        int k = imported_index(s, w->asset);
        if (k >= 0 && s->imp_clip[k][1] != HTA_AUDIO_NO_CLIP) hta_audio_play(&s->audio, s->imp_clip[k][1], 0.9f);
    }
}

/* The imported first-person view: the clip wanted, else idle, posed in
 * view space (+X forward, +Y left, +Z up -- Source's viewmodel axes are
 * Halo's). A one-shot clip falls back to idle when it ends. */
static void ivm_update(hta_android *s, float dt)
{
    const hta_game_weapon *w = held_imported(s);
    if (!w || !s->gfx) return;
    const hta_oal_model *m = &w->asset->models[1];
    int32_t roster = held_roster(s);
    if (s->ivm_weapon != roster || !s->gpu_ivm) {
        char err[HTA_ERRLEN];
        if (s->gpu_ivm) { hta_gfx_mesh_free(s->gfx, s->gpu_ivm); s->gpu_ivm = NULL; }
        s->gpu_ivm = hta_gfx_mesh_upload_dynamic(s->gfx, &m->mesh, err, sizeof(err));
        if (!s->gpu_ivm) hta_log("[imported] view model upload failed: %s", err);
        if (s->ivm_cap < m->mesh.vertex_count) {
            free(s->ivm_posed);
            s->ivm_posed = malloc(m->mesh.vertex_count * sizeof(hta_vertex));
            s->ivm_cap = s->ivm_posed ? m->mesh.vertex_count : 0;
        }
        s->ivm_weapon = roster;
        s->ivm_role = "draw";
        s->ivm_clip = -2;
        s->ivm_rate = 1.0f;
    }
    if (!s->ivm_posed) return;
    if (s->ivm_clip == -2) {
        s->ivm_clip = hta_oal_clip_find(m, s->ivm_role ? s->ivm_role : "idle");
        if (s->ivm_clip < 0) s->ivm_clip = hta_oal_clip_find(m, "idle");
        s->ivm_time = 0.0f;
    } else {
        s->ivm_time += dt * (s->ivm_rate > 0.0f ? s->ivm_rate : 1.0f);
    }
    if (s->ivm_clip >= 0 && !m->clips[s->ivm_clip].loop &&
        s->ivm_time > hta_oal_clip_length(m, s->ivm_clip)) {
        s->ivm_role = "idle";
        s->ivm_clip = hta_oal_clip_find(m, "idle");
        s->ivm_time = 0.0f;
        s->ivm_rate = 1.0f;
    }
    /* Sway, ours (Source's own is cl_bob and a lagged view angle; these
     * numbers are by eye): the weapon trails a turn by up to 3.5 degrees
     * and catches up in about a sixth of a second, and bobs with the walk. */
    float dyaw = s->cam.yaw - s->ivm_last[0], dpitch = s->cam.pitch - s->ivm_last[1];
    while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
    while (dyaw < -3.14159265f) dyaw += 6.2831853f;
    s->ivm_last[0] = s->cam.yaw; s->ivm_last[1] = s->cam.pitch;
    if (fabsf(dyaw) > 0.5f || fabsf(dpitch) > 0.5f) dyaw = dpitch = 0.0f;   /* a respawn, not a turn */
    const float LAG_MAX = 0.06f;
    float keep = expf(-dt * 12.0f);
    s->ivm_lag[0] = (s->ivm_lag[0] - dyaw * 0.35f) * keep;
    /* A positive pitch lag turns the barrel down (rotation about +Y):
     * looking up, the weapon trails below. */
    s->ivm_lag[1] = (s->ivm_lag[1] + dpitch * 0.35f) * keep;
    for (int k = 0; k < 2; k++) {
        if (s->ivm_lag[k] > LAG_MAX) s->ivm_lag[k] = LAG_MAX;
        if (s->ivm_lag[k] < -LAG_MAX) s->ivm_lag[k] = -LAG_MAX;
    }
    float run = s->player.phys.run_forward > 0.1f ? s->player.phys.run_forward : 2.25f;
    float speed = hypotf(s->player.velocity[0], s->player.velocity[1]) / run;
    if (speed > 1.0f) speed = 1.0f;
    if (!s->player.on_ground) speed *= 0.2f;
    s->ivm_bob += dt * 8.5f * (0.3f + 0.7f * speed);
    if (s->ivm_bob > 62.831853f) s->ivm_bob -= 62.831853f;
    float side = sinf(s->ivm_bob) * 0.006f * speed;
    float up = -fabsf(cosf(s->ivm_bob)) * 0.005f * speed + sinf(s->ivm_bob * 0.5f) * 0.0006f;
    /* View space: +X forward, +Y left, +Z up. Yaw about Z, then pitch
     * about Y, then the bob. */
    float cy = cosf(s->ivm_lag[0]), sy = sinf(s->ivm_lag[0]);
    float cp = cosf(s->ivm_lag[1]), sp = sinf(s->ivm_lag[1]);
    const float root[12] = { cy*cp, -sy, cy*sp, 0.0f,
                             sy*cp,  cy, sy*sp, side,
                             -sp,   0.0f, cp,   up };
    hta_oal_pose(m, s->ivm_clip, s->ivm_time, s->ivm_world);
    hta_oal_skin(m, (const float (*)[12])s->ivm_world, root, s->ivm_posed);
}

/* The class to spawn with, when the match has custom classes: the start
 * weapons become the class's (tags of their base, and which are imported). */
static void class_start(hta_android *s)
{
    for (uint32_t k = 0; k < HTA_CARRY_MAX; k++) s->start_asset[k] = -1;
    if (!s->game_on || !s->game.classes || s->me < 0) return;
    const hta_unit *u = &s->game.units[s->me];
    uint32_t n = 0;
    for (int k = 0; k < 2; k++) {
        int32_t r = u->loadout[k];
        if (r < 0 || (uint32_t)r >= s->game.weapon_count) continue;
        s->start_weapon[n] = s->game.weapons[r].tag;
        s->start_asset[n] = s->game.weapons[r].asset ? r : -1;
        n++;
    }
    if (n) s->start_count = n;
}

/* Your class: its two weapons by the names the menu showed. */
static void class_apply(hta_android *s)
{
    int32_t pick[2] = { -1, -1 };
    for (int k = 0; k < 2; k++)
        for (uint32_t w = 0; w < s->game.weapon_count && pick[k] < 0; w++)
            if (hta_game_class_weapon(&s->game, (int32_t)w) && s->my_class[k][0] &&
                !strcasecmp(s->game.weapons[w].display, s->my_class[k])) pick[k] = (int32_t)w;
    hta_game_set_loadout(&s->game, s->me, pick[0], pick[1]);
    hta_log("[class] %s / %s -> roster %d / %d", s->my_class[0], s->my_class[1], (int)pick[0], (int)pick[1]);
}

/* Out of the match until a class is picked: dead to everyone, not drawn,
 * the respawn clock stopped. */
static void class_hold(hta_android *s)
{
    if (!s->game_on || !s->game.classes || s->me < 0) return;
    s->choosing = true;
    s->dead = true;
    s->dead_timer = s->respawn_delay;
    s->corpse_up = false;
    hta_unit *u = &s->game.units[s->me];
    u->alive = false;
    u->dead_for = 1e4f;                       /* no body to show */
    u->death_yaw = s->cam.yaw;
}

/* The local player's character stats: its health and shield maximums on
 * the game's copy of your vitals, its run speeds on your own physics. */
static void apply_my_body(hta_android *s)
{
    if (!s->game_on || s->me < 0) return;
    hta_game_apply_body(&s->game, s->me);
    hta_body_attr b = hta_game_body(&s->game, s->me);
    hta_player_physics ph;
    hta_game_body_physics(&s->game, b.speed, &ph);
    hta_player_apply_physics(&s->player, &ph);
}

static void respawn(hta_android *s)
{
    if (!s->spawn_count) return;
    uint32_t i = hta_scenario_spawn_pick(s->spawn, s->spawn_count,
                                         s->death_pos, &s->spawn_rng);
    hta_spawn_point chosen = s->spawn[i];
    /* In a game, away from the people trying to kill you. */
    if (s->game_on)
        hta_game_pick_spawn(&s->game, s->me, chosen.position, &chosen.facing);
    hta_player_spawn(&s->player, &chosen);
    float gz;
    if (s->col.built &&
        hta_collision_ground(&s->col, s->player.pos[0], s->player.pos[1],
                             s->player.pos[2] + spawn_lift(s), &gz)) {
        s->player.pos[2] = gz;
        s->player.on_ground = true;
    }
    s->cam.yaw = chosen.facing;
    s->cam.pitch = 0.0f;

    /* Your character's health, shield and speed (and back on your feet). */
    if (s->game_on && s->me >= 0) {
        if (s->next_character != -2) {
            hta_game_assign_character(&s->game, s->me, s->next_character);
            s->next_character = -2;
        }
        apply_my_body(s);
    }
    s->power_fly = false;
    hta_vitals_reset(s->vit);
    /* You come back with what the map arms you with, not with whatever you
     * had scavenged. */
    /* Every slot, not just the first: a weapon picked up (or handed over by
     * the debug pad) into the second hand used to survive a death. */
    class_start(s);
    bool rearm = s->start_count && s->held_count != s->start_count;
    for (uint32_t k = 0; s->start_count && !rearm && k < s->start_count; k++)
        if (s->held[k] != s->start_weapon[k] || s->held_asset[k] != s->start_asset[k]) rearm = true;
    if (s->start_count && s->held_slot != 0) rearm = true;
    if (rearm) {
        s->held_count = s->start_count;
        for (uint32_t i = 0; i < s->start_count; i++) {
            s->held[i] = s->start_weapon[i];
            s->held_asset[i] = s->start_asset[i];
        }
        s->held_slot = 0;
        equip_weapon(s, s->held[0]);
    }
    /* A fresh magazine and a full reserve, and the weapon comes up unzoomed
     * with its idle pose rather than mid-reload. */
    hta_ammo_init(&s->ammo, &s->weap);
    for (uint32_t k = 0; k < HTA_CARRY_MAX; k++) s->held_ammo_set[k] = false;
    s->zoom_level = 0;
    apply_zoom(s);
    fire_loop(s, false);
    vm_play(s, HTA_VM_IDLE);
    s->nade_count = s->nade_max;

    s->corpse_up = false;
    s->killcam_unit = -1;
    s->killcam_phase = 0;
    atomic_store(&g_killcam_ready, 0);
    if (s->game_on) {
        hta_game_revive(&s->game, s->me);
        hta_game_sync_local(&s->game, &s->player, &s->cam, held_roster(s));
    }
    hta_log("[player] respawned at spawn %u (%.2f %.2f %.2f)",
            i, s->player.pos[0], s->player.pos[1], s->player.pos[2]);
}

static void equip_imported(hta_android *s);

static void equip_weapon(hta_android *s, uint32_t weap_tag_id)
{
    if (!weap_tag_id) return;
    equip_weapon_tag(s, weap_tag_id);
    equip_imported(s);
}

/* An imported weapon in hand lays its numbers over its base's, and brings
 * its own first-person view up with its draw clip. */
static void equip_imported(hta_android *s)
{
    const hta_game_weapon *w = held_imported(s);
    s->ivm_weapon = -1;
    if (!w) return;
    uint32_t fp = s->weap.fp_model_id, hud = s->weap.hud_interface_id;
    s->weap = w->def;
    s->weap.fp_model_id = fp;
    s->weap.hud_interface_id = hud;
    s->gun.fire_interval = s->weap.cooldown;
    hta_gun_set_error(&s->gun, s->weap.error_angle, s->weap.error_accel, s->weap.error_decel);
    hta_ammo_init(&s->ammo, &s->weap);
    s->ivm_role = "draw";
    s->ivm_clip = -2;
    /* Its own crosshair, when its package names one. The HUD was just
     * rebuilt for the base weapon; swap the reticle and upload again. */
    const hta_oal_asset *a = w->asset;
    unsigned shape = (strstr(a->crosshair, "arms") ? HTA_HUD_CROSS_ARMS : 0u) |
                     (strstr(a->crosshair, "dot") ? HTA_HUD_CROSS_DOT : 0u) |
                     (strstr(a->crosshair, "ring") ? HTA_HUD_CROSS_RING : 0u);
    if (shape && hta_hud_custom_cross(&s->hud, shape, a->crosshair_size > 4.0f ? a->crosshair_size : 24.0f) &&
        s->gfx) {
        char err[HTA_ERRLEN];
        if (s->gpu_hud) { hta_gfx_mesh_free(s->gfx, s->gpu_hud); s->gpu_hud = NULL; }
        s->gpu_hud = hta_gfx_mesh_upload_dynamic(s->gfx, &s->hud.mesh, err, sizeof(err));
    }
    hta_log("[imported] holding %s: %.1f rounds/s, %d-round magazine, damage x%.2f",
            w->display, w->def.rof, w->def.rounds_loaded_max, w->damage_scale);
}

static void equip_weapon_tag(hta_android *s, uint32_t weap_tag_id)
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
    /* And what a round of it does to a man. */
    s->impact_jpt = hta_projectile_impact_damage(&s->cache, s->weap.projectile_id);

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
    if (s->gpu_nades) { hta_gfx_mesh_free(s->gfx, s->gpu_nades); s->gpu_nades = NULL; }
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
                s->gpu_proj = hta_gfx_mesh_upload_dynamic_world(s->gfx, &s->proj.mesh,
                                                          perr, sizeof(perr));
        }
        /* Everything this weapon will ever throw: its detonation, and one
         * impact effect per material the MAP actually contains. Built once
         * here, because interning a texture mid-game would move the mesh
         * under the buffer the GPU is reading. */
        if (s->gpu_parts) {
            hta_gfx_mesh_free(s->gfx, s->gpu_parts);
            s->gpu_parts = NULL;
        }
        hta_particles_free(&s->parts);
        hta_particles_init(&s->parts);
        s->det_recipe = HTA_PART_NO_RECIPE;
        s->casing_recipe = HTA_PART_NO_RECIPE;
        for (uint32_t m = 0; m < 33u; m++) s->impact_recipe[m] = HTA_PART_NO_RECIPE;

        const hta_resource_map *pbm = s->bitmaps_ok ? &s->bitmaps_rm : NULL;
        if (s->proj.det_effect)
            s->det_recipe = hta_particles_add(&s->parts, &s->cache, pbm,
                                              s->proj.det_effect);
        for (uint32_t k = 0; k < s->map_material_count; k++) {
            uint8_t m = s->map_material[k];
            uint32_t fx = hta_projectile_response_effect(&s->cache,
                                                         s->weap.projectile_id, m);
            if (fx) s->impact_recipe[m] = hta_particles_add(&s->parts, &s->cache,
                                                            pbm, fx);
        }
        /* The brass. A weapon's firing effect carries its muzzle flashes
         * AND its ejected casing; the flash is already drawn by the
         * viewmodel, so only the particles on `primary ejection` are
         * taken. The covenant weapons have none, which is correct. */
        if (s->weap.firing_fx_id)
            s->casing_recipe = hta_particles_add_marker(&s->parts, &s->cache, pbm,
                                                        s->weap.firing_fx_id,
                                                        "primary ejection");
        /* A continuous weapon sprays a particle SYSTEM rather than firing
         * a burst: the flamethrower's jet is a `pctl` on its `spawn fire`
         * marker, at the speed of the flame projectile it also launches. */
        s->jet_recipe = HTA_PART_NO_RECIPE;
        {
            uint32_t pctl = hta_object_attachment(&s->cache, weap_tag_id,
                                                  "spawn fire",
                                                  HTA_FOURCC('p','c','t','l'));
            if (pctl) {
                float jet = s->proj.speed_initial > 0.0f ? s->proj.speed_initial
                                                         : 3.0f;
                s->jet_recipe = hta_particles_add_system(&s->parts, &s->cache,
                                                         pbm, pctl, jet);
                if (s->jet_recipe != HTA_PART_NO_RECIPE)
                    hta_log("[weapon] continuous jet 0x%08X at %.1f wu/s",
                            pctl, (double)jet);
            }
        }

        /* The grenade blast, which is the same whatever you are holding. */
        s->nade_recipe = HTA_PART_NO_RECIPE;
        if (s->nades.det_effect)
            s->nade_recipe = hta_particles_add(&s->parts, &s->cache, pbm,
                                               s->nades.det_effect);
        if (s->nade_snd) bank_get(s, s->nade_snd);
        /* The vehicle guns' own muzzle flashes and brass. */
        for (uint32_t w = 0; w < HTA_GAME_MAX_WEAPONS; w++) {
            s->vfire_recipe[w] = HTA_PART_NO_RECIPE;
            if (s->game_on && w < s->game.weapon_count && s->game.weapons[w].vehicle &&
                s->game.weapons[w].def.firing_fx_id)
                s->vfire_recipe[w] = hta_particles_add(&s->parts, &s->cache, pbm,
                                                       s->game.weapons[w].def.firing_fx_id);
        }
        /* A wreck: the tank shell's explosion, thrown twice over. And the
         * sparks a hull on its last legs gives off: the chaingun's own
         * impact on thick metal. */
        s->wreck_recipe = s->spark_recipe = HTA_PART_NO_RECIPE;
        s->wreck_snd = 0;
        if (s->game_on && s->game.wreck_effect) {
            s->wreck_recipe = hta_particles_add(&s->parts, &s->cache, pbm, s->game.wreck_effect);
            float r;
            uint32_t deca;
            hta_effect_detonation(&s->cache, s->game.wreck_effect, &s->wreck_snd, &r, &deca);
            if (s->wreck_snd) bank_get(s, s->wreck_snd);
            uint32_t sparks = 0;
            for (uint32_t w = 0; w < s->game.weapon_count && !sparks; w++)
                if (s->game.weapons[w].vehicle && !s->game.weapons[w].travels)
                    sparks = hta_projectile_response_effect(&s->cache,
                        s->game.weapons[w].def.projectile_id, HTA_HULL_MATERIAL);
            if (sparks) s->spark_recipe = hta_particles_add(&s->parts, &s->cache, pbm, sparks);
        }
        /* Rounds into bodies: decoded now rather than on the first hit. */
        for (uint32_t w = 0; s->game_on && w < s->game.weapon_count; w++)
            for (uint8_t m = HTA_MATERIAL_CYBORG_ARMOR; m <= HTA_MATERIAL_CYBORG_SHIELD; m++) {
                uint32_t snd = s->game.weapons[w].def.projectile_id
                    ? hta_projectile_impact_sound(&s->cache, s->game.weapons[w].def.projectile_id, m) : 0;
                if (snd) bank_get(s, snd);
            }
        /* And whatever the bots' rounds throw when they go off. */
        for (uint32_t p = 0; p < HTA_GAME_MAX_POOLS; p++) {
            s->pool_recipe[p] = HTA_PART_NO_RECIPE;
            if (s->game_on && p < s->game.pool_count && s->game.pools[p].det_effect)
                s->pool_recipe[p] = hta_particles_add(&s->parts, &s->cache, pbm,
                                                      s->game.pools[p].det_effect);
        }
        if (hta_particles_build(&s->parts, perr, sizeof(perr))) {
            hta_log("[weapon] particles: %u type(s), %u recipe(s)",
                    s->parts.type_count, s->parts.recipe_count);
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
static void play_impact_at(hta_android *s, uint8_t material, const float at[3])
{
    if (material >= 33u) return;
    if (!s->impact_known[material]) {
        s->impact_snd[material] =
            hta_projectile_impact_sound(&s->cache, s->weap.projectile_id, material);
        s->impact_known[material] = 1;
    }
    if (s->impact_snd[material]) play_tag_at(s, s->impact_snd[material], at, 0.8f);
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
    if (!strncmp(s->map_path, HTA_APK_PREFIX, strlen(HTA_APK_PREFIX))) {
        char asset[128];
        snprintf(asset, sizeof(asset), "maps/%s", name);
        if (apk_has(s, asset)) { snprintf(out, outlen, HTA_APK_PREFIX "%s", asset); return true; }
    }
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

static void start_game(hta_android *s);
static void start_gfx(hta_android *s);
static void stop_gfx(hta_android *s);
static void rebuild_gfx_if_size_changed(hta_android *s);
static void game_gpu_upload(hta_android *s);

/* A separate walkable Source-map mode uses the same renderer and collision
 * code, without constructing a Halo scenario or starting a match. */
static bool load_external_map(hta_android *s)
{
    const char *dir = s->app->activity->externalDataPath;
    if (!dir) dir = s->app->activity->internalDataPath;
    char path[1024], err[HTA_ERRLEN];
    if (!dir || snprintf(path, sizeof(path), "%s/external.oalmap", dir) >= (int)sizeof(path)) {
        snprintf(s->status, sizeof(s->status), "external map path unavailable");
        return false;
    }
    hta_external_map external;
    if (!hta_external_map_load(path, &external, err, sizeof(err))) {
        snprintf(s->status, sizeof(s->status), "external map: %s", err);
        return false;
    }
    hta_bsp_mesh solid;
    hta_external_map_collision_view(&external.mesh, &external, &solid);
    if (!hta_collision_build_cells(&s->col, &solid, HTA_COLLISION_CELLS_IMPORTED)) {
        hta_external_map_free(&external);
        snprintf(s->status, sizeof(s->status), "external map collision failed");
        return false;
    }
    s->mesh = external.mesh;
    memset(&external.mesh, 0, sizeof(external.mesh));
    /* The collision grid points into the solid list: keep it with the state. */
    s->world_ext.solid_indices = external.solid_indices;
    s->world_ext.solid_index_count = external.solid_index_count;
    external.solid_indices = NULL;
    s->have_mesh = s->map_loaded = true;
    hta_player_init(&s->player);
    hta_camera_init(&s->cam);
    if (external.spawn_count) {
        uint32_t chosen = 0;
        float ground = 0.0f;
        for (uint32_t i = 0; i < external.spawn_count; i++) {
            const float *p = external.spawns[i].position;
            if (hta_collision_ground(&s->col, p[0], p[1], p[2] + 1.0f, &ground)) {
                chosen = i;
                break;
            }
        }
        hta_player_spawn(&s->player, &external.spawns[chosen]);
        const float *p = external.spawns[chosen].position;
        if (hta_collision_ground(&s->col, p[0], p[1], p[2] + 1.0f, &ground)) {
            s->player.pos[2] = ground;
            s->player.on_ground = true;
        }
        s->cam.yaw = external.spawns[chosen].facing;
    } else {
        for (int axis = 0; axis < 3; axis++)
            s->player.pos[axis] = 0.5f * (s->mesh.bounds_min[axis] + s->mesh.bounds_max[axis]);
        s->player.pos[2] = s->mesh.bounds_max[2] + 1.0f;
    }
    memcpy(s->cam.pos, s->player.pos, sizeof(s->cam.pos));
    s->cam.pos[2] += s->player.eye_height;
    s->scene.light_dir[0] = 0.35f; s->scene.light_dir[1] = 0.4f; s->scene.light_dir[2] = 0.85f;
    s->scene.light_color[0] = s->scene.light_color[1] = s->scene.light_color[2] = 1.0f;
    s->scene.ambient[0] = s->scene.ambient[1] = s->scene.ambient[2] = 0.7f;
    s->scene.clear[0] = 0.1f; s->scene.clear[1] = 0.15f; s->scene.clear[2] = 0.23f;
    snprintf(s->status, sizeof(s->status), "exploring external map");
    hta_log("[external] loaded %u triangles, %u spawns", s->mesh.index_count / 3, external.spawn_count);
    free(external.spawns);
    return true;
}
static void game_gpu_free(hta_android *s);

/* The match's imported world, in place of Blood Gulch's BSP. A package in
 * the APK is mapped, read and unmapped: everything kept is copied out. */
/* The imported characters and weapons (characters/NAME.oalasset,
 * weapons/NAME.oalasset; app/content.c): loaded once, for the menus and
 * every match. Personal builds carry them in the APK -- publish_apk.sh
 * --with-assets puts them there. */
static void load_imported(hta_android *s)
{
    hta_session_load_imported(&s->session, &s->fs);
}

/* What the menus offer, for the Java side: one line per entry,
 * "W<TAB>name" for every weapon a class may hold (the Trial's hand-held
 * weapons, then imported ones), "C<TAB>id<TAB>name" per imported body. */
static char g_catalog[8192];
static _Atomic int g_catalog_ready;

static void catalog_build(hta_android *s)
{
    if (atomic_load(&g_catalog_ready)) return;
    load_imported(s);
    size_t len = 0;
    g_catalog[0] = 0;
    uint8_t *data = NULL;
    size_t size = 0;
    if (find_map(s) && map_data_file(s, s->map_path, &data, &size)) {
        hta_cache c;
        char err[HTA_ERRLEN];
        if (hta_cache_open(&c, data, size, err, sizeof(err))) {
            uint32_t tags[HTA_GAME_MAX_WEAPONS];
            uint32_t n = hta_weapon_list_playable(&c, tags, HTA_GAME_MAX_WEAPONS);
            for (uint32_t i = 0; i < n; i++) {
                int32_t ti = hta_cache_find_tag_by_id(&c, tags[i]);
                hta_tag_entry t;
                char path[256];
                if (ti < 0 || !hta_cache_tag(&c, (uint32_t)ti, &t) || !hta_cache_tag_path(&c, &t, path, sizeof(path)))
                    continue;
                /* The same name the game's roster uses: the tag's own leaf. */
                const char *leaf = strrchr(path, '\\');
                leaf = leaf ? leaf + 1 : path;
                int w = snprintf(g_catalog + len, sizeof(g_catalog) - len, "W\t%s\n", leaf);
                if (w > 0 && (size_t)w < sizeof(g_catalog) - len) len += (size_t)w;
            }
        }
        /* Only the catalog needed it: unmap the copy just made. */
        s->mapped_count--;
        munmap(s->mapped_base[s->mapped_count], s->mapped_len[s->mapped_count]);
    }
    for (uint32_t k = 0; k < s->imp_weap_count; k++) {
        int w = snprintf(g_catalog + len, sizeof(g_catalog) - len, "W\t%s\n", s->imp_weap[k].display);
        if (w > 0 && (size_t)w < sizeof(g_catalog) - len) len += (size_t)w;
    }
    for (uint32_t k = 0; k < s->imp_char_count; k++) {
        /* Body, default weapons, hero group, and per-match limit. */
        int w = snprintf(g_catalog + len, sizeof(g_catalog) - len, "C\t%s\t%s\t%s\t%s\t%s\t%d\t%s\n", s->imp_char[k].name,
                         s->imp_char[k].display, s->imp_char[k].loadout[0], s->imp_char[k].loadout[1],
                         s->imp_char[k].hero_group, s->imp_char[k].unique_limit, s->imp_char[k].ability_name);
        if (w > 0 && (size_t)w < sizeof(g_catalog) - len) len += (size_t)w;
    }
    atomic_store(&g_catalog_ready, 1);
}

JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeCatalog(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, atomic_load(&g_catalog_ready) ? g_catalog : "");
}

/* Look and class for the next match, set by the menu before it starts one. */
static struct { char character[48]; int bots_imported, classes; char cls[2][48]; } g_loadout;

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeSetLoadout(JNIEnv *env, jclass cls, jstring character,
                                                     jint bots_imported, jint classes,
                                                     jstring primary, jstring secondary)
{
    (void)cls;
    memset(&g_loadout, 0, sizeof(g_loadout));
    const char *u;
    if (character && (u = (*env)->GetStringUTFChars(env, character, NULL))) {
        snprintf(g_loadout.character, sizeof(g_loadout.character), "%s", u);
        (*env)->ReleaseStringUTFChars(env, character, u);
    }
    if (primary && (u = (*env)->GetStringUTFChars(env, primary, NULL))) {
        snprintf(g_loadout.cls[0], sizeof(g_loadout.cls[0]), "%s", u);
        (*env)->ReleaseStringUTFChars(env, primary, u);
    }
    if (secondary && (u = (*env)->GetStringUTFChars(env, secondary, NULL))) {
        snprintf(g_loadout.cls[1], sizeof(g_loadout.cls[1]), "%s", u);
        (*env)->ReleaseStringUTFChars(env, secondary, u);
    }
    g_loadout.bots_imported = bots_imported;
    g_loadout.classes = classes;
}

/* A class picked in play: before your first spawn, or for the next one. */
static struct { char cls[2][48]; char character[48]; } g_chosen;
static _Atomic int g_chosen_ready;
/* 1: held out until a class is picked; 2: the match has classes. */
static _Atomic int g_class_state;
static _Atomic int g_team_request;

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeChooseTeam(JNIEnv *env, jclass cls, jint team)
{
    (void)env; (void)cls;
    if (team==0 || team==1) atomic_store(&g_team_request, team+1);
}
static _Atomic uint32_t g_taken_heroes;

static void hero_occupancy(hta_android *s)
{
    uint32_t taken = 0;
    if (s->game_on && !s->allow_duplicate_heroes)
        for (uint32_t i = 0; i < s->game.unit_count; i++) {
            const hta_unit *u = &s->game.units[i];
            int c = u->character;
            if ((int32_t)i == s->me || u->kind == HTA_UNIT_BOT ||
                c < 0 || c >= 32 || (uint32_t)c >= s->game.character_count) continue;
            if (s->game.characters[c]->unique_limit == 1) taken |= 1u << c;
        }
    atomic_store(&g_taken_heroes, taken);
}

JNIEXPORT jboolean JNICALL
Java_net_hta_halotrial_GameActivity_nativeCharacterAvailable(JNIEnv *env, jclass cls, jstring name)
{
    (void)cls;
    if (!name || !g_android || !g_android->game_on) return JNI_TRUE;
    const char *id = (*env)->GetStringUTFChars(env, name, NULL);
    if (!id) return JNI_TRUE;
    bool available = true;
    if (!g_android->allow_duplicate_heroes)
        for (uint32_t i = 0; i < g_android->imp_char_count && i < 32; i++)
            if (!strcmp(id, g_android->imp_char[i].name) &&
                g_android->imp_char[i].unique_limit == 1 &&
                (atomic_load(&g_taken_heroes) & (1u << i))) available = false;
    (*env)->ReleaseStringUTFChars(env, name, id);
    return available ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_net_hta_halotrial_GameActivity_nativeClassState(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return atomic_load(&g_class_state);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeChooseClass(JNIEnv *env, jclass cls, jstring character,
                                                      jstring primary, jstring secondary)
{
    (void)cls;
    if (atomic_load(&g_chosen_ready)) return;
    const char *u;
    g_chosen.character[0] = 0;
    if (character && (u = (*env)->GetStringUTFChars(env, character, NULL))) {
        snprintf(g_chosen.character, sizeof(g_chosen.character), "%s", u);
        (*env)->ReleaseStringUTFChars(env, character, u);
    }
    jstring in[2] = { primary, secondary };
    for (int k = 0; k < 2; k++) {
        g_chosen.cls[k][0] = 0;
        if (in[k] && (u = (*env)->GetStringUTFChars(env, in[k], NULL))) {
            snprintf(g_chosen.cls[k], sizeof(g_chosen.cls[k]), "%s", u);
            (*env)->ReleaseStringUTFChars(env, in[k], u);
        }
    }
    atomic_store(&g_chosen_ready, 1);            /* publishes g_chosen */
}

/* The game side of it, once a frame: take a pick, and say where we stand. */
static void class_poll(hta_android *s)
{
    bool on = s->game_on && s->game.classes;
    int team=atomic_exchange(&g_team_request,0);
    if (on && s->choosing && team>0 && s->game.teams) {
        s->selected_team=team-1;
        s->game.units[s->me].team=(uint8_t)(team-1);
    }
    if (atomic_load(&g_chosen_ready)) {
        if (on && (!s->game.teams || s->selected_team>=0)) {
            /* The class's body: now, before the first spawn; at the next
             * respawn when picked from the pause screen. */
            int32_t body = -1;
            for (uint32_t k = 0; k < s->game.character_count; k++)
                if (!strcmp(s->game.characters[k]->name, g_chosen.character)) body = (int32_t)k;
            int32_t prior_body = s->game.units[s->me].character;
            if (!hta_game_assign_character(&s->game, s->me, body)) {
                hta_log("[class] %s is already taken", g_chosen.character);
                atomic_store(&g_chosen_ready, 0);
                atomic_store(&g_class_state, (s->choosing ? 1 : 0) | 2);
                return;
            }
            for (int k = 0; k < 2; k++) snprintf(s->my_class[k], sizeof(s->my_class[k]), "%s", g_chosen.cls[k]);
            class_apply(s);
            snprintf(s->my_character, sizeof(s->my_character), "%s", g_chosen.character);
            s->next_character = body;
            if (!s->choosing) s->game.units[s->me].character = (int8_t)prior_body;
            if (s->choosing) {
                s->choosing = false;
                /* Through the black, in. */
                s->dead_timer = HTA_DEATH_FADE_OUT;
            }
        }
        atomic_store(&g_chosen_ready, 0);
    }
    if (s->choosing && s->dead_timer < s->respawn_delay) s->dead_timer = s->respawn_delay;
    atomic_store(&g_class_state, (s->choosing ? 1 : 0) | (on ? 2 : 0) |
                 (on && s->choosing && s->game.teams && s->selected_team<0 ? 4 : 0));
}

/* A plain ding when no sound pack brings TF2's: E6 with its octave,
 * struck and decaying; the kill is a rising pair. Ours. */
static uint32_t synth_ding(hta_android *s, bool kill)
{
    enum { RATE = 44100, N = RATE / 4 };
    static int16_t pcm[2][N];
    int16_t *o = pcm[kill ? 1 : 0];
    for (int i = 0; i < N; i++) {
        float t = (float)i / RATE, v = 0.0f;
        float f = kill && t > 0.09f ? 1760.0f : 1318.5f, t0 = kill && t > 0.09f ? t - 0.09f : t;
        v = sinf(6.2831853f * f * t0) * expf(-t0 * 16.0f) + 0.45f * sinf(6.2831853f * 2.0f * f * t0) * expf(-t0 * 24.0f);
        o[i] = (int16_t)(v * 0.4f * 32767.0f);
    }
    return hta_audio_add_clip(&s->audio, o, N, RATE, 1);
}

/* The mixer keeps the pointer. Freeing the buffer after add_clip was a
 * use-after-free the moment a voice played it. */
static int16_t *keep_pcm(int16_t *p)
{
    static int16_t *bag[24];
    static int n;
    if (p && n < 24) bag[n++] = p;
    return p;
}

static float sat(float x)
{
    float y = tanhf(x);
    if (y > 0.98f) y = 0.98f;
    if (y < -0.98f) y = -0.98f;
    return y;
}

static float nz(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return ((int)(*s >> 16) - 32768) / 32768.0f;
}

/* A glottal buzz through three formants. Original, not a recording. */
static float vowel(float *ph, float f0, float f1, float f2, float f3,
                   float *q1, float *q2, float *q3, float rate, float breath, uint32_t *rng)
{
    *ph += f0 / rate;
    if (*ph >= 1.0f) *ph -= floorf(*ph);
    float g = 0.0f;
    if (*ph < 0.40f) g = 0.5f * (1.0f - cosf(3.14159265f * (*ph / 0.40f)));
    else if (*ph < 0.58f) g = 0.5f * (1.0f + cosf(3.14159265f * ((*ph - 0.40f) / 0.18f)));
    *q1 += f1 / rate; *q2 += f2 / rate; *q3 += f3 / rate;
    float v = (0.62f * sinf(*q1 * 6.2831853f) + 0.28f * sinf(*q2 * 6.2831853f) +
               0.12f * sinf(*q3 * 6.2831853f)) * (0.25f + 0.75f * g);
    if (breath > 0.0f) v += breath * nz(rng) * (0.35f + 0.65f * g);
    return v;
}

/* A steady roar. The old one was a rising chirp, which is why every beam
 * sounded like a screech once a few copies overlapped. Pitch stays put and
 * the layers thicken. */
static uint32_t synth_power(hta_android *s, int color)
{
    enum { RATE = 22050, N = RATE * 9 / 10 };
    int16_t *pcm = malloc((size_t)N * sizeof(*pcm));
    if (!pcm) return HTA_AUDIO_NO_CLIP;
    uint32_t noise = 0x458ace1u + (uint32_t)color * 97u;
    float f0 = 42.0f + (float)color * 7.0f;
    float f1 = 96.0f + (float)color * 18.0f;
    float f2 = 180.0f + (float)color * 22.0f;
    float p0 = 0.0f, p1 = 0.0f, p2 = 0.0f;
    float dur = (float)N / (float)RATE;
    for (int i = 0; i < N; i++) {
        float t = (float)i / (float)RATE;
        float env = t < 0.012f ? t / 0.012f : 1.0f;
        env *= 0.78f + 0.22f * sinf(6.2831853f * (5.0f + (float)color) * t);
        env *= 0.62f + 0.38f * (1.0f - t / dur);
        float vib = 1.0f + 0.012f * sinf(6.2831853f * 5.5f * t);
        p0 += f0 * vib / (float)RATE;
        p1 += f1 * vib / (float)RATE;
        p2 += f2 / (float)RATE;
        noise = noise * 1664525u + 1013904223u;
        float n = ((int)(noise >> 16) - 32768) / 32768.0f;
        float crackle = n * (color == 0 ? 0.34f : 0.16f);
        float v = 0.58f * sinf(p0 * 6.2831853f) + 0.30f * sinf(p1 * 6.2831853f) +
                  0.14f * sinf(p2 * 6.2831853f) + crackle;
        pcm[i] = (int16_t)(sat(v * env * 1.55f) * 32767.0f);
    }
    uint32_t clip = hta_audio_add_clip(&s->audio, pcm, N, RATE, 1);
    keep_pcm(pcm);
    return clip;
}

/* One shout at the start of an ability. 0 heat grunt, 1 the ki call,
 * 2 a repulsor lock, 3 a deep roar, 4 a boom, 5 a struck chord. */
static uint32_t synth_shout(hta_android *s, int kind)
{
    enum { RATE = 22050 };
    int N = kind == 1 ? RATE * 11 / 10 : kind == 0 ? RATE : RATE * 7 / 10;
    int16_t *pcm = malloc((size_t)N * sizeof(*pcm));
    if (!pcm) return HTA_AUDIO_NO_CLIP;
    float ph = 0.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f, sweep = 0.0f;
    uint32_t rng = 0x51u * (uint32_t)(kind + 3);
    float dur = (float)N / (float)RATE;
    for (int i = 0; i < N; i++) {
        float t = (float)i / (float)RATE;
        float env = t < 0.015f ? t / 0.015f : (dur - t) < 0.06f ? (dur - t) / 0.06f : 1.0f;
        float v = 0.0f;
        if (kind == 1) {
            float f0, f1, f2, f3, breath, amp;
            if (t < 0.08f)      { f0 = 120; f1 = 700; f2 = 1500; f3 = 2400; breath = 0.75f; amp = 0.55f; }
            else if (t < 0.26f) { f0 = 155; f1 = 800; f2 = 1150; f3 = 2500; breath = 0.04f; amp = 1.00f; }
            else if (t < 0.34f) { f0 = 140; f1 = 420; f2 = 1400; f3 = 2200; breath = 0.02f; amp = 0.40f; }
            else if (t < 0.52f) { f0 = 175; f1 = 520; f2 = 1900; f3 = 2600; breath = 0.04f; amp = 1.05f; }
            else if (t < 0.60f) { f0 = 160; f1 = 600; f2 = 1500; f3 = 2400; breath = 0.60f; amp = 0.50f; }
            else if (t < 0.82f) { f0 = 195; f1 = 820; f2 = 1200; f3 = 2550; breath = 0.05f; amp = 1.20f; }
            else if (t < 0.90f) { f0 = 180; f1 = 430; f2 = 1300; f3 = 2200; breath = 0.02f; amp = 0.45f; }
            else                { f0 = 220; f1 = 840; f2 = 1180; f3 = 2600; breath = 0.06f; amp = 1.35f; }
            v = vowel(&ph, f0, f1, f2, f3, &q1, &q2, &q3, (float)RATE, breath, &rng) * amp;
            v += 0.22f * sinf(6.2831853f * 72.0f * t);
        } else if (kind == 0) {
            float f0 = 92.0f + 6.0f * sinf(6.2831853f * 4.0f * t);
            v = vowel(&ph, f0, 620, 1080, 2300, &q1, &q2, &q3, (float)RATE, 0.18f, &rng) * 1.25f;
            v += 0.35f * sinf(6.2831853f * 48.0f * t);
        } else if (kind == 2) {
            float f = t < 0.18f ? 160.0f + 280.0f * (t / 0.18f) : 70.0f;
            sweep += f / (float)RATE;
            float hit = t > 0.18f ? expf(-(t - 0.18f) * 7.0f) : 0.0f;
            v = (t < 0.18f ? 0.50f : 0.0f) * sinf(sweep * 6.2831853f);
            v += hit * (0.85f * sinf(6.2831853f * 52.0f * t) +
                        0.40f * sinf(sweep * 6.2831853f * 2.41f) +
                        0.22f * sinf(sweep * 6.2831853f * 3.73f) +
                        0.28f * nz(&rng));
        } else if (kind == 3) {
            float f0 = 78.0f + 10.0f * sinf(6.2831853f * 6.0f * t);
            v = vowel(&ph, f0, 480, 900, 2200, &q1, &q2, &q3, (float)RATE, 0.28f, &rng) * 1.35f;
        } else if (kind == 5) {
            float e = expf(-t * 2.6f);
            v = e * (0.55f * sinf(6.2831853f * 523.25f * t) +
                     0.40f * sinf(6.2831853f * 659.25f * t) +
                     0.32f * sinf(6.2831853f * 783.99f * t) +
                     0.18f * sinf(6.2831853f * 1046.5f * t));
            v += 0.20f * nz(&rng) * expf(-t * 16.0f);
        } else {
            float e = expf(-t * 2.2f);
            v = e * (0.75f * sinf(6.2831853f * 44.0f * t) + 0.30f * sinf(6.2831853f * 88.0f * t));
            v += 0.40f * nz(&rng) * expf(-t * 5.0f);
            v += 0.18f * expf(-t * 3.5f) * sinf(6.2831853f * 740.0f * t);
        }
        pcm[i] = (int16_t)(sat(v * env * 1.2f) * 32767.0f);
    }
    uint32_t clip = hta_audio_add_clip(&s->audio, pcm, (uint32_t)N, RATE, 1);
    keep_pcm(pcm);
    return clip;
}

static void add_bark(hta_android *s, const char *name, int kind)
{
    if (s->bark_count >= 12) return;
    uint32_t c = synth_shout(s, kind);
    if (c == HTA_AUDIO_NO_CLIP) return;
    s->bark_name[s->bark_count] = name;
    s->bark_clip[s->bark_count] = c;
    s->bark_count++;
}

/* An original broom anthem in 3/4: bells, a string pad and a bass drum.
 * Loud on purpose. It is not a film theme. The loop is crossfaded so the
 * join is not a click. */
static uint32_t synth_flight(hta_android *s)
{
    enum { RATE = 22050, BEAT = RATE * 2 / 5, BARS = 4, BEATS = 3 * BARS, N = BEAT * BEATS, X = 600 };
    static const float mel[BEATS] = {
        523.25f, 659.25f, 783.99f,
        880.00f, 783.99f, 659.25f,
        698.46f, 587.33f, 523.25f,
        587.33f, 493.88f, 523.25f
    };
    static const float root[BARS] = { 130.81f, 110.00f, 87.31f, 130.81f };
    int16_t *pcm = malloc((size_t)N * sizeof(*pcm));
    if (!pcm) return HTA_AUDIO_NO_CLIP;
    float pm = 0.0f, pb = 0.0f, ps = 0.0f;
    for (int i = 0; i < N; i++) {
        int beat = i / BEAT;
        int bar = beat / 3;
        float u = (float)(i % BEAT) / (float)BEAT;
        if (i % BEAT == 0) pm = 0.0f;
        float f = mel[beat];
        pm += f / (float)RATE;
        pb += root[bar] / (float)RATE;
        ps += (f * 0.5f) / (float)RATE;
        float bell = expf(-u * 5.2f) * (0.58f * sinf(pm * 6.2831853f) +
                                        0.24f * sinf(pm * 6.2831853f * 2.76f) +
                                        0.10f * sinf(pm * 6.2831853f * 5.04f));
        float bow = 0.26f * (0.55f * sinf(ps * 6.2831853f) +
                             0.20f * sinf(ps * 6.2831853f * 2.0f) +
                             0.08f * sinf(ps * 6.2831853f * 3.0f));
        float kick = (beat % 3 == 0) ? expf(-u * 12.0f) * sinf(pb * 6.2831853f) * 0.70f
                                     : 0.16f * sinf(pb * 6.2831853f);
        pcm[i] = (int16_t)(sat((bell + bow + kick) * 1.45f) * 32767.0f);
    }
    for (int i = 0; i < X; i++) {
        float a = (float)i / (float)X;
        int j = N - X + i;
        pcm[j] = (int16_t)((1.0f - a) * (float)pcm[j] + a * (float)pcm[i]);
    }
    uint32_t clip = hta_audio_add_clip(&s->audio, pcm, N, RATE, 1);
    keep_pcm(pcm);
    return clip;
}

/* The imported weapons' sounds as mixer clips, once audio is up. */
static void imported_sounds(hta_android *s)
{
    if (s->audio_ok && s->ding_clip == HTA_AUDIO_NO_CLIP) {
        for(int c=0;c<6;c++) s->hero_sound[c]=synth_power(s,c);
        s->flight_clip = synth_flight(s);
        add_bark(s, "superman64", 0);
        add_bark(s, "goku", 1);
        add_bark(s, "iron_man", 2);
        add_bark(s, "dragonborn", 3);
        add_bark(s, "dumbledore", 4);
        add_bark(s, "harry", 5);
        add_bark(s, "master_chief", 4);
        add_bark(s, "scout", 3);
        const hta_oal_sound *h = hta_oal_sound_find(&s->ui_sounds, "hit");
        const hta_oal_sound *k = hta_oal_sound_find(&s->ui_sounds, "kill");
        s->ding_clip = h ? hta_audio_add_clip(&s->audio, h->samples, h->frames, h->rate, (uint8_t)h->channels)
                         : synth_ding(s, false);
        s->kill_clip = k ? hta_audio_add_clip(&s->audio, k->samples, k->frames, k->rate, (uint8_t)k->channels)
                         : synth_ding(s, true);
        const hta_oal_sound *f = hta_oal_sound_find(&s->ui_sounds, "freeze");
        const hta_oal_sound *p = hta_oal_sound_find(&s->ui_sounds, "snapshot");
        if (f) s->freeze_clip = hta_audio_add_clip(&s->audio, f->samples, f->frames, f->rate, (uint8_t)f->channels);
        if (p) s->snap_clip = hta_audio_add_clip(&s->audio, p->samples, p->frames, p->rate, (uint8_t)p->channels);
    }
    static const char *const ROLES[2] = { "fire", "reload" };
    static const char *const VOICES[2] = { "hurt", "death" };
    for (uint32_t k=0;k<s->imp_char_count && s->audio_ok;k++)
        for (int r=0;r<2;r++) {
            if (s->imp_voice[k][r]!=HTA_AUDIO_NO_CLIP) continue;
            const hta_oal_sound *snd=hta_oal_sound_find(&s->imp_char[k],VOICES[r]);
            if (snd) s->imp_voice[k][r]=hta_audio_add_clip(&s->audio,snd->samples,snd->frames,snd->rate,(uint8_t)snd->channels);
        }
    for (uint32_t k = 0; k < s->imp_weap_count && s->audio_ok; k++)
        for (int r = 0; r < 2; r++) {
            if (s->imp_clip[k][r] != HTA_AUDIO_NO_CLIP) continue;
            const hta_oal_sound *snd = hta_oal_sound_find(&s->imp_weap[k], ROLES[r]);
            if (snd)
                s->imp_clip[k][r] = hta_audio_add_clip(&s->audio, snd->samples, snd->frames, snd->rate,
                                                       (uint8_t)snd->channels);
        }
}

static bool load_map(hta_android *s)
{
    g_phase = "loading map";
    if (!find_map(s)) return false;
    load_imported(s);
    imported_sounds(s);
    hta_log("[assets] found %s", s->map_path);

    /* mmap the cache read-only: no copy, and the OS pages it in lazily */
    if (!map_data_file(s, s->map_path, &s->map_data, &s->map_size)) {
        snprintf(s->status, sizeof(s->status), "cannot map %s", s->map_path);
        return false;
    }

    char err[HTA_ERRLEN];
    /* sounds.map and bitmaps.map beside it (the menu may have mapped them
     * already); the world is built from all three (app/match_load.c). */
    if (!s->sounds_data && find_named(s, "sounds.map", s->sounds_path, sizeof(s->sounds_path)))
        map_data_file(s, s->sounds_path, &s->sounds_data, &s->sounds_size);
    if (!s->bitmaps_data && find_named(s, "bitmaps.map", s->bitmaps_path, sizeof(s->bitmaps_path)))
        map_data_file(s, s->bitmaps_path, &s->bitmaps_data, &s->bitmaps_size);
    if (!hta_match_load_world(&s->session, &s->fs, s->app->activity->externalDataPath))
        return false;

    s->bitmaps_ok = (s->bitmaps_rm.data != NULL);
    s->weapon_count = hta_weapon_list_playable(&s->cache, s->weapons,
                                               (uint32_t)(sizeof(s->weapons)/sizeof(s->weapons[0])));
    hta_log("[weapon] %u playable weapon(s) in this cache", s->weapon_count);
    /* What the MAP says you spawn holding: Blood Gulch's starting equipment
     * names the assault rifle and the pistol, and the campaign map's player
     * starting profile agrees down to the magazines. Nothing here is
     * chosen by us. */
    s->start_count = hta_scenario_starting_weapons(&s->cache, s->start_weapon,
                                                   HTA_CARRY_MAX);
    if (!s->start_count && s->weapon_count) {
        /* A map with no loadout at all still has to arm you. */
        s->start_weapon[0] = s->weapons[0];
        s->start_count = 1;
        hta_log("[weapon] no starting equipment in this map; taking the first");
    }
    for (uint32_t i = 0; i < s->start_count; i++) {
        hta_weapon_def probe;
        if (hta_weapon_load_id(&s->cache, NULL, s->start_weapon[i], &probe,
                               NULL, NULL, 0))
            hta_log("[weapon] spawn with %s", probe.path);
    }
    s->held_count = s->start_count;
    for (uint32_t i = 0; i < s->start_count; i++) { s->held[i] = s->start_weapon[i]; s->held_asset[i] = -1; }
    s->held_slot = 0;
    if (s->held_count) equip_weapon(s, s->held[0]);
    else hta_log("[weapon] nothing to hold");
    if (hta_sky_load(&s->sky, &s->cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL, err, sizeof(err))) {
        s->have_sky = true;
        hta_log("[assets] sky %u verts / %u submeshes", s->sky.vertex_count, s->sky.submesh_count);
    } else {
        hta_log("[assets] sky: %s", err);
    }

    /* spawn at a real player start if the scenario has one. Kept, because
     * dying means coming back at another one. */
    hta_spawn_point *sp = s->spawn;
    uint32_t nsp = hta_scenario_spawns(&s->cache, sp, 64);
    if (s->world_loaded) {
        nsp = s->world_ext.spawn_count < 64 ? s->world_ext.spawn_count : 64;
        memcpy(sp, s->world_ext.spawns, nsp * sizeof(*sp));
    }
    s->spawn_count = nsp;
    s->spawn_rng = 0x9E3779B9u;
    hta_player_init(&s->player);
    {
        hta_player_physics phys;
        if (hta_player_physics_load(&phys, &s->cache, err, sizeof(err))) {
            hta_player_apply_physics(&s->player, &phys);
            s->cam.fov_y = phys.fov_y;
            s->base_fov = phys.fov_y;
            /* The grenades. `globals` keeps a table of them at +296,
             * 68 bytes an entry: how many you may carry, how many you
             * spawn with in multiplayer, and the projectile itself. */
            {
                int32_t gi = hta_cache_find_tag_by_class(&s->cache, HTA_TAG_MATG);
                hta_tag_entry gt;
                uint32_t gb;
                if (gi >= 0 && hta_cache_tag(&s->cache, (uint32_t)gi, &gt) &&
                    hta_cache_ptr_to_offset(&s->cache, gt.tag_data_ptr, &gb)) {
                    uint32_t n = 0, p2 = 0, off = 0;
                    if (hta_read_reflexive(&s->cache, gb + 296u, &n, &p2) && n &&
                        hta_cache_ptr_to_offset(&s->cache, p2, &off)) {
                        int16_t mx = 0, sp = 0;
                        uint32_t proj = 0;
                        hta_rd_u16(&s->cache, off + 0u, (uint16_t *)&mx);
                        hta_rd_u16(&s->cache, off + 2u, (uint16_t *)&sp);
                        hta_rd_u32(&s->cache, off + 52u + 12u, &proj);
                        char gerr[HTA_ERRLEN];
                        if (proj && hta_projectiles_equip_projectile(
                                &s->nades, &s->cache,
                                s->bitmaps_rm.data ? &s->bitmaps_rm : NULL,
                                proj, gerr, sizeof(gerr))) {
                            s->nade_max = mx > 0 ? mx : 4;
                            s->nade_count = sp > 0 ? sp : 2;
                            s->nade_snd = s->nades.detonation_snd;
                            hta_log("[player] %d frag grenade(s) of %d, "
                                    "fuse %.2fs after the bounce, blast %.0f",
                                    s->nade_count, s->nade_max,
                                    s->nades.timer, s->nades.blast_damage);
                        }
                    }
                }
            }
            if (hta_vitals_load(s->vit, &s->cache))
                hta_log("[player] %.0f health, %.0f shield, back in %.1fs at %.0f%%/s"
                        "; a fall hurts past %.1f wu/s and kills at %.1f",
                        s->vit->max_health, s->vit->max_shield,
                        s->vit->recharge_delay, s->vit->recharge_rate * 100.0f,
                        s->vit->fall_harmful_min, s->vit->fall_fatal);
            /* Somebody to shoot at, out in front of the spawn. */
            {
                char berr[HTA_ERRLEN];
                uint32_t bip = 0;
                for (uint32_t i = 0; i < s->cache.tag_count && !bip; i++) {
                    hta_tag_entry t;
                    if (!hta_cache_tag(&s->cache, i, &t)) continue;
                    if (t.primary_class != HTA_FOURCC('b','i','p','d')) continue;
                    char p[128];
                    hta_cache_tag_path(&s->cache, &t, p, sizeof(p));
                    if (strstr(p, "cyborg_mp")) bip = t.tag_id;
                }
                s->melee_damage = hta_biped_melee_damage(&s->cache, bip);
                if (bip && hta_bot_load(&s->bot, &s->cache,
                                        s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                                        bip, berr, sizeof(berr))) {
                    hta_log("[bot] %s; melee does %.0f", berr, s->melee_damage);
                    /* Something in its hands. `stand rifle idle` poses them
                     * to hold a rifle; without one it reads as a man
                     * standing with his arms out. */
                    uint32_t ar = 0;
                    for (uint32_t i = 0; i < s->cache.tag_count && !ar; i++) {
                        hta_tag_entry t;
                        if (!hta_cache_tag(&s->cache, i, &t)) continue;
                        if (t.primary_class != HTA_FOURCC('m','o','d','2')) continue;
                        char p[160];
                        hta_cache_tag_path(&s->cache, &t, p, sizeof(p));
                        if (strcmp(p, "weapons\\assault rifle\\assault rifle") == 0)
                            ar = t.tag_id;
                    }
                    if (ar && hta_bot_arm(&s->bot, &s->cache,
                                          s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                                          ar, berr, sizeof(berr)))
                        hta_log("[bot] armed: %s", berr);
                    else
                        hta_log("[bot] unarmed (%s)", berr);
                    if (s->net_enabled) {
                        for (int slot=0;slot<2;slot++) {
                            uint32_t model=slot==0 ? ar : 0;
                            if (slot==1) {
                                for (uint32_t i=0;i<s->cache.tag_count && !model;i++) {
                                    hta_tag_entry t; char path[160];
                                    if (!hta_cache_tag(&s->cache,i,&t) ||
                                        t.primary_class!=HTA_FOURCC('m','o','d','2')) continue;
                                    hta_cache_tag_path(&s->cache,&t,path,sizeof(path));
                                    if (!strcmp(path,"weapons\\pistol\\pistol")) model=t.tag_id;
                                }
                            }
                            hta_actor *a=&s->remote[slot];
                            if (!hta_actor_load(a,&s->cache,
                                    s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                                    bip,berr,sizeof(berr))) continue;
                            if (model) hta_actor_hold(a,&s->cache,
                                s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                                model,"right hand",berr,sizeof(berr));
                            hta_actor_play(a,slot ? "stand pistol idle" : "stand rifle idle",false);
                        }
                        hta_log("[net] remote Spartan models: AR=%d pistol=%d",
                                s->remote[0].loaded,s->remote[1].loaded);
                        /* The stationary practice target is another Spartan.
                         * In a live session it is misleading, and its local
                         * damage is not server authority, so remove it. */
                        hta_bot_free(&s->bot);
                    }
                } else {
                    hta_log("[bot] none (%s)", berr);
                }
            }

            /* The shield's own voice. Every one of these is the tag's:
             * which sound, and which condition it is latched to. */
            {
                bool lp = false;
                s->shield_charge_snd =
                    hta_unit_hud_sound(&s->cache, HTA_HUDSND_SHIELD_RECHARGING, &lp);
                s->shield_hit_snd =
                    hta_unit_hud_sound(&s->cache, HTA_HUDSND_SHIELD_DAMAGED, &lp);
                s->shield_low_snd =
                    hta_unit_hud_sound(&s->cache, HTA_HUDSND_SHIELD_LOW, &lp);
                s->shield_empty_snd =
                    hta_unit_hud_sound(&s->cache, HTA_HUDSND_SHIELD_EMPTY, &lp);
                s->health_low_snd =
                    hta_unit_hud_sound(&s->cache, HTA_HUDSND_HEALTH_LOW, &lp);
                /* Four of the five are `lsnd`, and nothing downstream can
                 * play one -- a mixer clip comes from a `snd!`. Resolve
                 * each to its first track here, once. */
                uint32_t *snds[5] = {
                    &s->shield_charge_snd, &s->shield_hit_snd,
                    &s->shield_low_snd, &s->shield_empty_snd,
                    &s->health_low_snd
                };
                for (int q = 0; q < 5; q++) {
                    hta_loop_sound ls;
                    if (!*snds[q]) continue;
                    if (hta_loop_sound_track(&s->cache, *snds[q], &ls))
                        *snds[q] = ls.loop ? ls.loop : ls.start;
                    else
                        *snds[q] = 0;
                }
                if (s->shield_charge_snd) bank_get(s, s->shield_charge_snd);
                if (s->shield_hit_snd)    bank_get(s, s->shield_hit_snd);
                if (s->shield_empty_snd)  bank_get(s, s->shield_empty_snd);
                if (s->shield_low_snd)    bank_get(s, s->shield_low_snd);
                if (s->health_low_snd)    bank_get(s, s->health_low_snd);
                hta_log("[player] hud sounds: charge 0x%08X hit 0x%08X "
                        "low 0x%08X empty 0x%08X heartbeat 0x%08X",
                        s->shield_charge_snd, s->shield_hit_snd,
                        s->shield_low_snd, s->shield_empty_snd,
                        s->health_low_snd);
            }

            /* Your own body, for looking at once it is on the floor. */
            {
                char aerr[HTA_ERRLEN];
                uint32_t bip = 0;
                for (uint32_t i = 0; i < s->cache.tag_count && !bip; i++) {
                    hta_tag_entry t;
                    if (!hta_cache_tag(&s->cache, i, &t)) continue;
                    if (t.primary_class != HTA_FOURCC('b','i','p','d')) continue;
                    char p[128];
                    hta_cache_tag_path(&s->cache, &t, p, sizeof(p));
                    if (strstr(p, "cyborg_mp")) bip = t.tag_id;
                }
                if (bip && hta_actor_load(&s->corpse, &s->cache,
                                          s->bitmaps_ok ? &s->bitmaps_rm : NULL,
                                          bip, aerr, sizeof(aerr)))
                    hta_log("[player] body: %s", aerr);
                else
                    hta_log("[player] no body to leave behind (%s)", aerr);
            }

            /* What the Chief says on the way down. Blood Gulch carries one
             * dialogue tag and it is his. */
            {
                uint32_t udlg = hta_dialogue_tag(&s->cache);
                s->death_quiet_snd =
                    hta_dialogue_sound(&s->cache, udlg, HTA_DLG_DEATH_QUIET);
                s->death_violent_snd =
                    hta_dialogue_sound(&s->cache, udlg, HTA_DLG_DEATH_VIOLENT);
                s->death_falling_snd =
                    hta_dialogue_sound(&s->cache, udlg, HTA_DLG_DEATH_FALLING);
                if (s->death_quiet_snd) bank_get(s, s->death_quiet_snd);
                if (s->death_violent_snd) bank_get(s, s->death_violent_snd);
                hta_log("[player] death dialogue: quiet 0x%08X violent 0x%08X",
                        s->death_quiet_snd, s->death_violent_snd);
            }
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
                                 s->player.pos[2] + spawn_lift(s), &gz)) {
            s->player.pos[2] = gz;
            s->player.on_ground = true;
            hta_log("[assets] snapped spawn to ground z=%.2f", gz);
        }
        /* Eight world units in front of where you start, facing you --
         * far enough to shoot at, near enough to walk up and hit. */
        if (s->bot.loaded) {
            float bp[3] = {
                s->player.pos[0] + cosf(sp[0].facing) * 8.0f,
                s->player.pos[1] + sinf(sp[0].facing) * 8.0f,
                s->player.pos[2]
            };
            float gz;
            if (s->col.built &&
                hta_collision_ground(&s->col, bp[0], bp[1], bp[2] + spawn_lift(s), &gz))
                bp[2] = gz;
            hta_bot_spawn(&s->bot, bp, sp[0].facing + 3.14159265f);
            hta_log("[bot] standing at (%.2f %.2f %.2f)", bp[0], bp[1], bp[2]);
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
    if (s->world_loaded) {
        /* No lightmaps in a package: an even daylight. Ours. The world is
         * drawn at its textures' own brightness; bodies and weapons are lit
         * as albedo x (ambient + light x wrapped N.L) x 2, so these average
         * about 1x too. (1.0 and 0.7 here put bodies at 1.4-3.4x: pale skin
         * and white coats came out as white.) */
        s->scene.light_dir[0] = 0.35f; s->scene.light_dir[1] = 0.4f; s->scene.light_dir[2] = 0.85f;
        for (int k = 0; k < 3; k++) { s->scene.light_color[k] = 0.46f; s->scene.ambient[k] = 0.30f; }
    }

    s->have_mesh = true;
    s->map_loaded = true;
    snprintf(s->status, sizeof(s->status), "loaded %s", s->cache.name);
    start_game(s);
    /* With custom classes you start with your class, not the map's pair. */
    if (s->game_on && s->game.classes) {
        class_start(s);
        s->held_count = s->start_count;
        for (uint32_t i = 0; i < s->start_count; i++) { s->held[i] = s->start_weapon[i]; s->held_asset[i] = s->start_asset[i]; }
        s->held_slot = 0;
        for (uint32_t k = 0; k < HTA_CARRY_MAX; k++) s->held_ammo_set[k] = false;
        class_hold(s);
    }
    /* Again, now the game's rounds exist: their detonations need particle
     * recipes built alongside the held weapon's. */
    if (s->game_on && s->held_count) equip_weapon(s, s->held[s->held_slot]);
    if (s->game_on) hta_game_sync_local(&s->game, &s->player, &s->cam, held_roster(s));
    return true;
}

/* ------------------------------- the game ------------------------------ */

/* What the Java HUD draws over the game: the announcer's banner, where you
 * stand, the kill feed and, at the end, the scoreboard. Sections are
 * separated by 0x1E, lines by '\n'. Written on the game thread, read by the
 * HUD's own redraw; a torn read shows one odd frame of text, nothing worse. */
static char g_game_text[1536];

/* How long a feed line and a banner stay up. Ours: Halo fades its kill
 * messages after a few seconds and the tag does not say how many. */
#define HTA_FEED_TIME    6.0f
#define HTA_BANNER_TIME  3.0f
/* Seconds the scoreboard shows after a game before the next begins. Ours. */
#define HTA_POSTGAME     10.0f

static uint32_t find_sound(const hta_cache *c, const char *path)
{
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        char p[256];
        if (!hta_cache_tag(c, i, &t) || t.primary_class != HTA_TAG_SND) continue;
        if (hta_cache_tag_path(c, &t, p, sizeof(p)) && !strcasecmp(p, path)) return t.tag_id;
    }
    return 0;
}

static int32_t held_roster(hta_android *s)
{
    if (!s->game_on || !s->held_count) return -1;
    if (s->held_asset[s->held_slot] >= 0) return s->held_asset[s->held_slot];
    return hta_game_weapon_index(&s->game, s->held[s->held_slot]);
}

/* The imported weapon in hand, or NULL. */
static const hta_game_weapon *held_imported(hta_android *s)
{
    int32_t w = held_roster(s);
    return w >= 0 && (uint32_t)w < s->game.weapon_count && s->game.weapons[w].asset ? &s->game.weapons[w] : NULL;
}

/* Which loaded package a roster entry came from, -1 if none. */
static int imported_index(const hta_android *s, const hta_oal_asset *a)
{
    for (uint32_t k = 0; k < s->imp_weap_count; k++) if (&s->imp_weap[k] == a) return (int)k;
    return -1;
}

/* A sound somewhere in the world: gain after distance, and pan. False when
 * it is out of earshot. */
static bool world_voice(hta_android *s, const float at[3], float gain, float *g_out, float *pan_out);

/* The imported weapon's own gunshot, in hand or at a place. */
static bool imported_fire_sound(hta_android *s, int32_t roster, const float *at)
{
    if (roster < 0 || (uint32_t)roster >= s->game.weapon_count || !s->game.weapons[roster].asset) return false;
    int k = imported_index(s, s->game.weapons[roster].asset);
    if (k < 0 || s->imp_clip[k][0] == HTA_AUDIO_NO_CLIP) return false;
    if (!at) { hta_audio_play(&s->audio, s->imp_clip[k][0], 1.0f); return true; }
    float g, pan;
    if (world_voice(s, at, 1.0f, &g, &pan)) hta_audio_play_pan(&s->audio, s->imp_clip[k][0], g, pan);
    return true;
}

static void feed_push(hta_android *s, const char *text)
{
    for (int i = 3; i > 0; i--) {
        memcpy(s->feed[i], s->feed[i - 1], sizeof(s->feed[i]));
        s->feed_age[i] = s->feed_age[i - 1];
    }
    snprintf(s->feed[0], sizeof(s->feed[0]), "%s", text);
    s->feed_age[0] = 0.0f;
    hta_log("[game] %s", text);
}


/* Bots walk round whole props and through where they stood: the nav grid
 * follows the props whenever one breaks or comes back. A CTF stand field
 * is worked out at the start, so sync just before it too. */
static void nav_props(hta_android *s)
{
    hta_match_nav_props(&s->session);
}
static void start_game(hta_android *s)
{
    g_phase = "starting match";
    hta_frame_stats_reset(&s->frame_stats);
    char err[HTA_ERRLEN];
    const hta_resource_map *bm = s->bitmaps_ok ? &s->bitmaps_rm : NULL;
    if (!hta_match_start(&s->session, s->app->activity->externalDataPath, true)) return;
    int bots = s->net_enabled && !s->net_hosting ? 0 : s->bot_count;
    if (s->vehicles.loaded) {
        s->veh_in_snd = find_sound(&s->cache, "sound\\sfx\\vehicles\\warthog_7_in");
        s->veh_out_snd = find_sound(&s->cache, "sound\\sfx\\vehicles\\warthog_7_out");
        if (s->veh_in_snd) bank_get(s, s->veh_in_snd);
        if (s->veh_out_snd) bank_get(s, s->veh_out_snd);
        for (uint32_t t = 0; t < s->vehicles.type_count && t < HTA_VEHICLE_TYPES; t++) {
            hta_loop_sound ls;
            s->veh_engine[t] = 0;
            if (hta_object_loop_sound(&s->cache, s->vehicles.types[t].tag_id, "", &ls)) {
                s->veh_engine[t] = ls.loop ? ls.loop : ls.start;
                s->veh_engine_gain[t] = ls.gain > 0.0f ? ls.gain : 1.0f;
                if (s->veh_engine[t]) bank_get(s, s->veh_engine[t]);
            }
        }
    }
    if (s->game.classes) class_apply(s);
    s->game.spawn_protect = s->spawn_protect;
    apply_my_body(s);
    /* The local player's health and shield move into the game, carrying
     * what the tags already gave them. */
    s->game.units[s->me].vitals = *s->vit;
    s->vit = &s->game.units[s->me].vitals;
    /* Bodies for everyone -- ours too, for the seats watched from outside. */
    if (!hta_game_view_load(&s->gview, &s->game, bm,
                            s->net_enabled ? HTA_GAME_MAX_UNITS : s->game.unit_count,
                            err, sizeof(err)))
        hta_log("[game] bodies: %s", err);
    else
        hta_log("[game] %s", err);
    static const char *const LINES[HTA_LINE_COUNT] = {
        NULL,
        "sound\\dialog\\multiplayer1\\slayer",
        "sound\\dialog\\multiplayer1\\double_kill",
        "sound\\dialog\\multiplayer1\\triple_kill",
        "sound\\dialog\\multiplayer1\\killtacular",
        "sound\\dialog\\multiplayer1\\killing_spree",
        "sound\\dialog\\multiplayer1\\running_riot",
        "sound\\dialog\\multiplayer1\\game_over",
        "sound\\dialog\\multiplayer1\\team_slayer",
        "sound\\dialog\\multiplayer1\\capture_the_flag",
        "sound\\dialog\\multiplayer1\\red_team_has_the_flag",
        "sound\\dialog\\multiplayer1\\blue_team_has_the_flag",
        "sound\\dialog\\multiplayer1\\red_team_flag_returned",
        "sound\\dialog\\multiplayer1\\blue_team_flag_returned",
        "sound\\dialog\\multiplayer1\\red_team_score",
        "sound\\dialog\\multiplayer1\\blue_team_score",
    };
    for (int l = 1; l < HTA_LINE_COUNT; l++) {
        s->line_snd[l] = find_sound(&s->cache, LINES[l]);
        if (s->line_snd[l]) bank_get(s, s->line_snd[l]);
    }
    /* The flag's own pickup sound, for the moment you take it. */
    s->flag_take_snd = 0;
    if (s->game.flag_weapon >= 0) {
        hta_weapon_def fd;
        if (hta_weapon_load_id(&s->cache, NULL, s->game.weapons[s->game.flag_weapon].tag,
                               &fd, NULL, NULL, 0) && fd.pickup_snd_id) {
            s->flag_take_snd = fd.pickup_snd_id;
            bank_get(s, s->flag_take_snd);
        }
    }
    for (uint32_t p = 0; p < s->game.pool_count; p++)
        if (s->game.pools[p].detonation_snd) bank_get(s, s->game.pools[p].detonation_snd);
    hta_match_begin(&s->session);
    /* Every round's contrail, once: the roster's and the pools'. */
    hta_contrails_free(&s->trails);
    hta_contrails_init(&s->trails);
    for (uint32_t w = 0; w < HTA_GAME_MAX_WEAPONS; w++)
        s->wtrail[w] = w < s->game.weapon_count
            ? hta_contrails_for_projectile(&s->trails, &s->cache, bm,
                                           s->game.weapons[w].def.projectile_id)
            : HTA_CONT_NONE;
    for (uint32_t p = 0; p < HTA_GAME_MAX_POOLS; p++)
        s->ptrail[p] = p < s->game.pool_count
            ? hta_contrails_for_projectile(&s->trails, &s->cache, bm, s->game.pools[p].proj_tag_id)
            : HTA_CONT_NONE;
    static const float colors[6][3]={{1,.04f,.01f},{.15f,.45f,1},{.72f,.18f,1},{1,.62f,.08f},{.2f,1,.28f},{.15f,1,1}};
    static const float widths[6]={.78f,.68f,.58f,.64f,.52f,.60f};
    for(int k=0;k<6;k++) {
        s->hero_trail[k]=hta_contrails_add_energy(&s->trails,colors[k],widths[k],.75f);
        s->hero_ring[k]=hta_contrails_add_energy(&s->trails,colors[k],widths[k]*1.35f,.95f);
    }
    const float white[3]={1,1,.92f};
    s->hero_core=hta_contrails_add_energy(&s->trails,white,.22f,.4f);
    s->laser_trail=s->hero_trail[0];
    for(int k=0;k<HTA_GAME_MAX_UNITS;k++) {
        s->hero_sound_time[k]=-10.0f;
        s->character_voice_time[k]=-10.0f;
        s->hero_bark_time[k]=-10.0f;
    }
    if (hta_contrails_build(&s->trails, err, sizeof(err))) hta_log("[game] %s", err);
    /* The practice target is gone: a match's rounds and blasts only look for
     * the match's units, so it would stand there unhittable and walked
     * through -- with no bots as much as with them. */
    hta_bot_free(&s->bot);
    static const char *const MODES[HTA_MODE_COUNT] = { "Slayer", "Team Slayer", "CTF" };
    hta_log("[game] %s: you (team %d) and %d bot(s) at skill %d, first to %d, %d min, respawn %.0f s",
            MODES[s->game.mode], s->game.units[s->me].team, bots, s->bot_skill,
            s->game.score_limit, s->time_limit_min, s->respawn_delay);
}

static void game_gpu_upload(hta_android *s)
{
    if (!s->game_on || !s->gfx) return;
    char err[HTA_ERRLEN];
    for (uint32_t i = 0; i < s->game.unit_count; i++)
        if (s->gview.actor[i].loaded && !s->gpu_units[i])
        {
            s->gpu_units[i] = hta_gfx_mesh_upload_dynamic_world(s->gfx,
                hta_game_view_body_mesh(&s->gview, &s->game, i), err, sizeof(err));
            s->gpu_unit_char[i] = s->game.units[i].character;
        }
    for (uint32_t w = 0; w < s->game.weapon_count; w++)
        if (s->gview.have_weapon[w] && !s->gpu_held[w])
            s->gpu_held[w] = hta_gfx_mesh_upload(s->gfx, &s->gview.weapon_mesh[w], err, sizeof(err));
    if (s->trails.loaded && !s->gpu_trails)
        s->gpu_trails = hta_gfx_mesh_upload_dynamic(s->gfx, &s->trails.mesh, err, sizeof(err));
    if (!s->gpu_gibs && hta_game_view_gib_mesh(&s->gview))
        s->gpu_gibs = hta_gfx_mesh_upload_dynamic(s->gfx, hta_game_view_gib_mesh(&s->gview), err, sizeof(err));
    for (uint32_t p = 0; p < s->game.pool_count; p++)
        if (s->game.pools[p].mesh.index_count && !s->gpu_pools[p])
            s->gpu_pools[p] = hta_gfx_mesh_upload_dynamic_world(s->gfx,
                &s->game.pools[p].mesh, err, sizeof(err));
}

static void game_gpu_free(hta_android *s)
{
    for (uint32_t i = 0; i < HTA_GAME_MAX_UNITS; i++)
        if (s->gpu_units[i]) { hta_gfx_mesh_free(s->gfx, s->gpu_units[i]); s->gpu_units[i] = NULL; }
    for (uint32_t w = 0; w < HTA_GAME_MAX_WEAPONS; w++)
        if (s->gpu_held[w]) { hta_gfx_mesh_free(s->gfx, s->gpu_held[w]); s->gpu_held[w] = NULL; }
    for (uint32_t p = 0; p < HTA_GAME_MAX_POOLS; p++)
        if (s->gpu_pools[p]) { hta_gfx_mesh_free(s->gfx, s->gpu_pools[p]); s->gpu_pools[p] = NULL; }
    if (s->gpu_trails) { hta_gfx_mesh_free(s->gfx, s->gpu_trails); s->gpu_trails = NULL; }
    if (s->gpu_gibs) { hta_gfx_mesh_free(s->gfx, s->gpu_gibs); s->gpu_gibs = NULL; }
    if (s->gpu_ivm) { hta_gfx_mesh_free(s->gfx, s->gpu_ivm); s->gpu_ivm = NULL; }
    s->ivm_weapon = -1;
}

/* A hitscan round's tracer, from where it left to the first thing in its
 * way, if its projectile carries a contrail. */
/* Halo draws one round in (between + 1) as a tracer: the rifle's trigger
 * says 3. `shooter` keeps count per unit; true when this round is one. */
static bool tracer_due(hta_android *s, int32_t shooter, int between)
{
    uint32_t k = shooter >= 0 && shooter < HTA_GAME_MAX_UNITS ? (uint32_t)shooter
                                                              : HTA_GAME_MAX_UNITS;
    if (s->since_tracer[k] < (uint8_t)between) { s->since_tracer[k]++; return false; }
    s->since_tracer[k] = 0;
    return true;
}

/* Drop a horizontal ring onto the ground under `at` when the BSP has a
 * floor there; otherwise a little below the point itself. */
static void hero_ground(hta_android *s, const float at[3], float out[3])
{
    out[0] = at[0]; out[1] = at[1]; out[2] = at[2] - 0.45f;
    if (!s->col.built) return;
    float z = at[2];
    if (hta_collision_ground(&s->col, at[0], at[1], at[2] + 4.0f, &z)) out[2] = z + 0.08f;
}

/* Beams stay in fixed slots so a sustained attack can be swept. Rings live
 * on their own type, or they steal those slots and the beam vanishes. */
static void hero_fx(hta_android *s, int32_t shooter, const hta_oal_asset *hero,
                     const float from[3], const float dir[3])
{
    int color = hero->ability_color >= 0 && hero->ability_color < 6 ? hero->ability_color : 0;
    int unit = shooter >= 0 && shooter < HTA_GAME_MAX_UNITS ? shooter : 0;
    bool beam = hero->ability_beam && hero->ability_radius <= 0.0f;
    bool pulse = hero->ability_radius > 0.0f;
    float end[3], reach = 120.0f;
    if (s->col.built) hta_collision_ray(&s->col, from, dir, reach, &reach, NULL, NULL);
    for (int k = 0; k < 3; k++) end[k] = from[k] + dir[k] * reach;
    if (beam) {
        /* Twin ribbons offset from the look axis: a line through the eye
         * has no area when the camera sits on it. */
        float right[3] = { -dir[1], dir[0], 0.0f };
        float rl = hypotf(right[0], right[1]);
        if (rl < 0.01f) { right[0] = 1.0f; right[1] = 0.0f; rl = 1.0f; }
        for (int side = -1; side <= 1; side += 2) {
            float start[3];
            for (int k = 0; k < 3; k++)
                start[k] = from[k] + dir[k] * 0.85f + right[k] / rl * 0.2f * (float)side;
            start[2] -= 0.06f;
            hta_contrails_beam_key(&s->trails, s->hero_trail[color],
                                   (uint32_t)unit * 16u + (uint32_t)(side > 0), start, end);
        }
        float core0[3], core1[3];
        for (int k = 0; k < 3; k++) {
            core0[k] = from[k] + dir[k] * 0.4f;
            core1[k] = end[k];
        }
        core0[2] -= 0.04f;
        hta_contrails_beam_key(&s->trails, s->hero_core, (uint32_t)unit * 16u + 2u, core0, core1);
        hta_damage_shake kick = { .radius = { 2.0f, 22.0f }, .shake_time = 0.2f,
                                  .shake_move = 0.11f, .shake_rot = 0.04f };
        hta_shake_add(&s->shake, &kick, &s->cam, shooter == s->me ? NULL : from);
    } else if (pulse && hero->ability_cone > 0.0f) {
        float yaw = atan2f(dir[1], dir[0]);
        float reach_cone = hero->ability_radius > 1.0f ? hero->ability_radius : 8.0f;
        for (int i = -2; i <= 2; i++) {
            float a = yaw + (float)i * 0.16f;
            float d[3] = { cosf(a), sinf(a), dir[2] * 0.35f };
            float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
            float tip[3];
            for (int k = 0; k < 3; k++) {
                d[k] /= len;
                tip[k] = from[k] + d[k] * reach_cone;
            }
            hta_contrails_beam_key(&s->trails, s->hero_trail[color],
                                   (uint32_t)unit * 16u + 4u + (uint32_t)(i + 2), from, tip);
        }
    }
    bool throttled = s->game.time - s->hero_sound_time[unit] < (beam ? 0.28f : 0.15f);
    float gain = 1.0f, pan = 0.0f;
    bool heard = s->audio_ok && (shooter == s->me || world_voice(s, from, 1.0f, &gain, &pan));
    if (!throttled) {
        s->hero_sound_time[unit] = s->game.time;
        if (heard) hta_audio_play_pan(&s->audio, s->hero_sound[color], gain, pan);
    }
    /* The call happens once, at the start. Retriggering it every tick of a
     * beam stuttered the word. */
    const hta_unit *vu = shooter >= 0 && shooter < HTA_GAME_MAX_UNITS ? &s->game.units[shooter] : NULL;
    bool opening = vu && (hero->ability_duration <= 0.2f ||
                          vu->ability_active > hero->ability_duration - 0.08f);
    if (heard && opening && s->game.time - s->hero_bark_time[unit] > 0.5f) {
        for (int i = 0; i < s->bark_count; i++)
            if (s->bark_name[i] && !strcmp(s->bark_name[i], hero->name)) {
                s->hero_bark_time[unit] = s->game.time;
                hta_audio_play_pan(&s->audio, s->bark_clip[i], gain * 1.2f, pan);
                break;
            }
    }
    if (throttled) return;
    if (pulse) hta_game_view_debris(&s->gview, from, dir, 5, 4.5f);
    if (beam) hta_game_view_debris(&s->gview, end, dir, 3, 3.0f);
    if (!beam) {
        float move = pulse ? 0.28f : 0.1f, rot = pulse ? 0.08f : 0.03f;
        hta_damage_shake kick = { .radius = { 3.0f, 24.0f }, .shake_time = 0.65f,
                                  .shake_move = move, .shake_rot = rot,
                                  .impulse_time = 0.22f, .impulse_rot = rot * 0.7f,
                                  .impulse_push = pulse ? 0.12f : 0.04f };
        hta_shake_add(&s->shake, &kick, &s->cam, shooter == s->me ? NULL : from);
    } else {
        hta_damage_shake kick = { .radius = { 2.0f, 18.0f }, .impulse_time = 0.18f,
                                  .impulse_rot = 0.04f, .impulse_push = 0.06f };
        hta_shake_add(&s->shake, &kick, &s->cam, shooter == s->me ? NULL : end);
    }
    float feet[3], hit[3];
    hero_ground(s, from, feet);
    hero_ground(s, end, hit);
    uint32_t ring = s->hero_ring[color];
    if (pulse) {
        float radius = hero->ability_radius > 1.0f ? hero->ability_radius : 4.0f;
        hta_contrails_ring(&s->trails, ring, feet, radius * 0.45f);
        hta_contrails_ring(&s->trails, ring, feet, radius);
    } else if (beam) {
        hta_contrails_ring(&s->trails, ring, feet, 1.6f);
        hta_contrails_ring(&s->trails, ring, hit, 2.4f);
    }
}

static void tracer(hta_android *s, int32_t shooter, int32_t weapon, const float from[3], const float dir[3])
{
    if (!s->trails.loaded || weapon < 0 || weapon >= (int32_t)s->game.weapon_count) return;
    const hta_oal_asset *hero = s->game.weapons[weapon].hero;
    if (hero) {
        hero_fx(s, shooter, hero, from, dir);
        if (hero->ability_beam || hero->ability_radius > 0.0f) return;
    }
    uint32_t type = s->wtrail[weapon];
    if (type == HTA_CONT_NONE || s->game.weapons[weapon].travels) return;
    if (!tracer_due(s, shooter, s->game.weapons[weapon].def.between_contrails)) return;
    float t = 100.0f, end[3];
    if (s->col.built) hta_collision_ray(&s->col, from, dir, 100.0f, &t, NULL, NULL);
    for (int k = 0; k < 3; k++) end[k] = from[k] + dir[k] * t;
    hta_contrails_tracer(&s->trails, type, from, end, 300.0f);
}

/* Every blast near the camera shakes it, by the effect's own damage
 * effects: a tank shell kicks the view within 3.25 wu and shakes it out
 * to 8. */
static void shake_effect(hta_android *s, uint32_t effect, const float at[3])
{
    if (!effect || !at) return;
    hta_damage_shake d[4];
    uint32_t n = hta_effect_shakes(&s->cache, effect, d, 4);
    for (uint32_t i = 0; i < n; i++) hta_shake_add(&s->shake, &d[i], &s->cam, at);
}

/* A trigger's firing damage effect is felt by whoever pulled it. */
static void shake_fire(hta_android *s, uint32_t jpt)
{
    hta_damage_shake d;
    if (jpt && hta_damage_shake_read(&s->cache, jpt, &d))
        hta_shake_add(&s->shake, &d, &s->cam, NULL);
}

/* Thunder rolling in: a long low shake, no kick. The Trial has no thunder
 * sound, so this is felt, not heard. Ours: 1.6 s, 0.004 wu, 0.004 rad at
 * full loudness. */
static void shake_thunder(hta_android *s, float gain)
{
    hta_damage_shake d;
    memset(&d, 0, sizeof(d));
    d.radius[0] = d.radius[1] = 1e6f;
    d.shake_time = 1.6f;
    d.shake_move = 0.004f * gain;
    d.shake_rot = 0.004f * gain;
    hta_shake_add(&s->shake, &d, &s->cam, NULL);
}

/* A vehicle blowing up: the shell's fireball twice, a ring of it around
 * the hull, its bang, the shake, the scorch. */
static void wreck_fx(hta_android *s, const float at[3])
{
    float up[3] = { 0, 0, 1 };
    if (s->wreck_recipe != HTA_PART_NO_RECIPE) {
        hta_particles_burst(&s->parts, s->wreck_recipe, at, up);
        for (int i = 0; i < 3; i++) {
            float a = (float)i * 2.094f;
            float p[3] = { at[0] + cosf(a) * 0.5f, at[1] + sinf(a) * 0.5f, at[2] + 0.2f };
            float d[3] = { cosf(a) * 0.5f, sinf(a) * 0.5f, 0.85f };
            hta_particles_burst(&s->parts, s->wreck_recipe, p, d);
        }
    }
    if (s->wreck_snd) play_tag_at(s, s->wreck_snd, at, 1.0f);
    shake_effect(s, s->game.wreck_effect, at);
    float down[3] = { 0, 0, 1 };
    hta_gun_add_mark(&s->gun, at, down, 1.5f);
}

/* The Trial's own words for what you just picked up: `hud_item_messages`
 * says "Picked up an assault rifle" and "Picked up %d rounds for ...".
 * Found by the item's own name in the message, since the list is not in
 * any order a tag points into. */
static void item_message(hta_android *s, uint32_t tag, int rounds)
{
    static uint32_t list;
    if (!list) list = hta_ustr_find(&s->cache, "ui\\hud\\hud_item_messages");
    int32_t ti = hta_cache_find_tag_by_id(&s->cache, tag);
    hta_tag_entry t;
    char path[256] = "";
    if (!list || ti < 0 || !hta_cache_tag(&s->cache, (uint32_t)ti, &t) ||
        !hta_cache_tag_path(&s->cache, &t, path, sizeof(path))) return;
    const char *name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    char want[64];
    snprintf(want, sizeof(want), "%s", name);
    if (strstr(want, "frag grenade")) snprintf(want, sizeof(want), "fragmentation grenade");
    if (!strncmp(want, "mp_", 3)) memmove(want, want + 3, strlen(want + 3) + 1);
    uint32_t n = hta_ustr_count(&s->cache, list);
    for (uint32_t i = 0; i < n; i++) {
        char line[96];
        if (!hta_ustr_get(&s->cache, list, i, line, sizeof(line))) continue;
        bool counts = strstr(line, "%d") != NULL;
        if (counts != (rounds > 0) || !strstr(line, want)) continue;
        if (counts) snprintf(s->item_msg, sizeof(s->item_msg), line, rounds);
        else snprintf(s->item_msg, sizeof(s->item_msg), "%s", line);
        s->item_msg_age = 0.0f;
        return;
    }
}

/* Everything the game did this frame, turned into sound, words and dust. */
/* Damage numbers for the HUD: screen x, y (0..1 of the view), amount and
 * opacity, four floats each, written by the game thread once a frame. */
#define HTA_DMG_MAX 16
static float g_dmg_screen[HTA_DMG_MAX * 4];
static _Atomic int g_dmg_count;
#define HTA_DMG_LIFE 1.1f       /* seconds a number floats. Ours, TF2-like */

static void character_voice(hta_android *s, int32_t unit, bool death)
{
    if (!s->audio_ok || unit<0 || (uint32_t)unit>=s->game.unit_count) return;
    const hta_unit *u=&s->game.units[unit];
    if (u->character<0 || (uint32_t)u->character>=s->game.character_count) return;
    if (!death && (u->vitals.health<=0.0f || s->game.time-s->character_voice_time[unit]<1.5f)) return;
    for (uint32_t k=0;k<s->imp_char_count;k++) {
        if (s->game.characters[u->character]!=&s->imp_char[k]) continue;
        uint32_t clip=s->imp_voice[k][death ? 1 : 0];
        if (clip==HTA_AUDIO_NO_CLIP) return;
        float gain=0.85f,pan=0.0f,at[3];
        hta_game_centre(&s->game,unit,at);
        if (unit!=s->me && !world_voice(s,at,0.85f,&gain,&pan)) return;
        hta_audio_play_pan(&s->audio,clip,gain,pan);
        s->character_voice_time[unit]=s->game.time;
        return;
    }
}

/* Your shot landed: the ding (once per volley -- a shotgun's pellets are
 * one hit), and a number over whoever took it. Hits on the same body in
 * quick succession add up in one number, as TF2's batching does. */
static void hit_feedback(hta_android *s, int32_t victim, const float pos[3], float amount)
{
    if (s->ding_cool <= 0.0f && s->ding_clip != HTA_AUDIO_NO_CLIP) {
        hta_audio_play(&s->audio, s->ding_clip, 0.8f);
        s->ding_cool = 0.06f;
    }
    if (!(amount > 0.0f)) return;
    int slot = -1;
    for (int i = 0; i < HTA_DMG_MAX && slot < 0; i++)
        if (s->dmg[i].amount > 0.0f && s->dmg[i].victim == victim && s->dmg[i].age < 0.25f) slot = i;
    if (slot >= 0) {
        s->dmg[slot].amount += amount;
        s->dmg[slot].age = 0.0f;
        return;
    }
    float oldest = -1.0f;
    for (int i = 0; i < HTA_DMG_MAX; i++) {
        if (s->dmg[i].amount <= 0.0f) { slot = i; break; }
        if (s->dmg[i].age > oldest) { oldest = s->dmg[i].age; slot = i; }
    }
    float at[3] = { pos[0], pos[1], pos[2] };
    if (victim < (int32_t)s->game.unit_count) {
        /* Over the head, a little to the side, so it does not cover the aim. */
        const hta_unit *v = &s->game.units[victim];
        at[0] = v->body.pos[0]; at[1] = v->body.pos[1];
        at[2] = v->body.pos[2] + v->body.phys.coll_stand + 0.1f;
    }
    memcpy(s->dmg[slot].pos, at, sizeof(at));
    s->dmg[slot].amount = amount;
    s->dmg[slot].age = 0.0f;
    s->dmg[slot].victim = victim;
}

/* Age the numbers and put them on the screen for the Java HUD. */
static void damage_numbers(hta_android *s, float dt)
{
    if (s->ding_cool > 0.0f) s->ding_cool -= dt;
    hta_mat4 vp = hta_camera_view_proj(&s->cam);
    int n = 0;
    for (int i = 0; i < HTA_DMG_MAX; i++) {
        if (s->dmg[i].amount <= 0.0f) continue;
        s->dmg[i].age += dt;
        if (s->dmg[i].age >= HTA_DMG_LIFE) { s->dmg[i].amount = 0.0f; continue; }
        float p[4] = { s->dmg[i].pos[0], s->dmg[i].pos[1], s->dmg[i].pos[2] + s->dmg[i].age * 0.35f, 1.0f }, c[4];
        hta_mat4_transform(&vp, p, c);
        if (c[3] <= 0.05f) continue;                /* behind you */
        float x = (c[0] / c[3] + 1.0f) * 0.5f, y = (c[1] / c[3] + 1.0f) * 0.5f;
        if (x < -0.1f || x > 1.1f || y < -0.1f || y > 1.1f) continue;
        float fade = s->dmg[i].age > HTA_DMG_LIFE * 0.6f
                   ? 1.0f - (s->dmg[i].age - HTA_DMG_LIFE * 0.6f) / (HTA_DMG_LIFE * 0.4f) : 1.0f;
        g_dmg_screen[n * 4 + 0] = x;
        g_dmg_screen[n * 4 + 1] = y;
        g_dmg_screen[n * 4 + 2] = s->dmg[i].amount;
        g_dmg_screen[n * 4 + 3] = fade;
        n++;
    }
    atomic_store(&g_dmg_count, n);
}

JNIEXPORT jfloatArray JNICALL
Java_net_hta_halotrial_GameActivity_nativeDamageNumbers(JNIEnv *env, jclass cls)
{
    (void)cls;
    int n = atomic_load(&g_dmg_count);
    if (n < 0) n = 0;
    if (n > HTA_DMG_MAX) n = HTA_DMG_MAX;
    jfloatArray out = (*env)->NewFloatArray(env, n * 4);
    if (out && n) (*env)->SetFloatArrayRegion(env, out, 0, n * 4, g_dmg_screen);
    return out;
}


JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeKillcam(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, atomic_load(&g_killcam_ready) ? g_killcam_text : "");
}

/* Ours, TF2-like: the body cam runs this long before the swoop; the swoop
 * takes this; the frozen view is this much narrower. Too short a respawn
 * for all of it and the killcam stands down. */
#define KILLCAM_AT     1.3f
#define KILLCAM_SWOOP  0.45f
#define KILLCAM_ZOOM   0.5f
#define KILLCAM_NEED   (KILLCAM_AT + KILLCAM_SWOOP + 1.0f + HTA_DEATH_FADE_OUT)

static void killcam(hta_android *s, float dt)
{
    (void)dt;
    if (s->killcam_unit < 0 || s->respawn_delay < KILLCAM_NEED || s->choosing) return;
    const hta_unit *k = &s->game.units[s->killcam_unit];
    if (k->kind == HTA_UNIT_NONE) { s->killcam_unit = -1; return; }
    float gone = s->respawn_delay - s->dead_timer;
    if (s->killcam_phase == 2) { s->cam = s->killcam_cam; return; }
    if (gone < KILLCAM_AT) return;
    if (s->killcam_phase == 0) {
        memcpy(s->killcam_from, s->cam.pos, sizeof(s->killcam_from));
        s->killcam_fov = s->cam.fov_y;
        s->killcam_phase = 1;
        if (s->freeze_clip != HTA_AUDIO_NO_CLIP) hta_audio_play(&s->audio, s->freeze_clip, 0.8f);
    }
    /* Face the killer from IN FRONT of their gaze. The old victim-to-killer
     * line usually stopped behind their head. Try small side offsets when a
     * wall crowds the frontal shot. */
    float face[3] = { k->body.pos[0], k->body.pos[1], k->body.pos[2] + k->body.eye_height * 0.9f };
    const float angle[5] = { 0.0f, 0.55f, -0.55f, 1.0f, -1.0f };
    float want[3] = { face[0], face[1], face[2] + 0.06f };
    float best = -1.0f;
    for (int i = 0; i < 5; i++) {
        float direction[3] = { cosf(k->eye.yaw + angle[i]), sinf(k->eye.yaw + angle[i]), 0.05f };
        float clearance = 1.25f;
        if (s->col.built) {
            float t = clearance;
            if (hta_collision_ray(&s->col, face, direction, clearance, &t, NULL, NULL))
                clearance = fmaxf(0.0f, t - 0.08f);
        }
        float score = clearance - fabsf(angle[i]) * 0.12f;
        if (score <= best) continue;
        best = score;
        for (int j = 0; j < 3; j++) want[j] = face[j] + direction[j] * clearance;
    }
    float f = (gone - KILLCAM_AT) / KILLCAM_SWOOP;
    if (f > 1.0f) f = 1.0f;
    float e = f * f * (3.0f - 2.0f * f);       /* ease in and out */
    for (int i = 0; i < 3; i++) s->cam.pos[i] = s->killcam_from[i] + (want[i] - s->killcam_from[i]) * e;
    float to[3] = { face[0] - s->cam.pos[0], face[1] - s->cam.pos[1], face[2] - s->cam.pos[2] };
    float flat = sqrtf(to[0]*to[0] + to[1]*to[1]);
    if (flat > 1e-4f || fabsf(to[2]) > 1e-4f) {
        s->cam.yaw = atan2f(to[1], to[0]);
        s->cam.pitch = atan2f(to[2], flat);
    }
    s->cam.fov_y = s->killcam_fov * (1.0f - (1.0f - KILLCAM_ZOOM) * e);
    if (f >= 1.0f) {
        /* Freeze on them: this frame is drawn once more (phase 2 skips the
         * draws after it), and the HUD says who. */
        s->killcam_phase = 2;
        s->killcam_cam = s->cam;
        if (s->snap_clip != HTA_AUDIO_NO_CLIP) hta_audio_play(&s->audio, s->snap_clip, 0.9f);
        float hp = hta_vitals_health_fraction(&k->vitals) * 50.0f + hta_vitals_shield_fraction(&k->vitals) * 50.0f;
        snprintf(g_killcam_text, sizeof(g_killcam_text), "%s\t%s\t%d", k->name, s->killcam_weapon,
                 (int)(hp + 0.5f));
        atomic_store(&g_killcam_ready, 1);
    }
}

static void game_events(hta_android *s)
{
    hta_game_event e;
    char buf[96];
    while (hta_game_pop(&s->game, &e)) {
        hta_wfx_game_event(&s->wfx, &e, &s->game);
        if (s->net_hosting && (e.a==-1 || (e.a>=0 && e.a<HTA_GAME_MAX_UNITS)) &&
            (e.kind==HTA_EV_FIRE || e.kind==HTA_EV_HIT_WORLD ||
             e.kind==HTA_EV_DETONATE)) {
            hta_net_fx fx={0};
            fx.kind=e.kind==HTA_EV_FIRE ? HTA_NET_FX_FIRE :
                    e.kind==HTA_EV_HIT_WORLD ? HTA_NET_FX_IMPACT : HTA_NET_FX_DETONATE;
            fx.entity=e.a<0 ? 255 : (uint8_t)e.a;
            fx.weapon=(uint8_t)(e.kind==HTA_EV_DETONATE ? e.pool : e.weapon);
            fx.material=e.material;
            for (int k=0;k<3;k++) { fx.pos[k]=e.pos[k]; fx.dir[k]=e.dir[k]; }
            hta_net_server_fx(&s->host_server,&fx);
        }
        switch (e.kind) {
        case HTA_EV_FIRE:
            if (e.weapon < 0 || e.weapon >= HTA_GAME_MAX_WEAPONS) break;
            {
            /* A vehicle gun or a character's ability is the game's shot,
             * even when it is ours: it shows and sounds from here. */
            bool gamegun = s->game.weapons[e.weapon].vehicle || s->game.weapons[e.weapon].hidden;
            if (e.a == s->me && gamegun)
                shake_fire(s, s->game.weapons[e.weapon].def.firing_damage_id);
            if (e.a != s->me || gamegun) tracer(s, e.a, e.weapon, e.pos, e.dir);
            /* Our own rifle speaks for itself. */
            if (e.a == s->me && !gamegun) break;
            }
            if (s->game.weapons[e.weapon].vehicle &&
                s->vfire_recipe[e.weapon] != HTA_PART_NO_RECIPE)
                hta_particles_burst(&s->parts, s->vfire_recipe[e.weapon], e.pos, e.dir);
            if (!s->unit_fire_known[e.weapon]) {
                s->unit_fire_known[e.weapon] = 1;
                s->unit_fire_snd[e.weapon] = hta_effect_first_sound(&s->cache,
                    s->game.weapons[e.weapon].def.firing_fx_id);
            }
            if (s->game.weapons[e.weapon].hero) break;
            if (imported_fire_sound(s, e.weapon, e.pos)) break;
            if (s->unit_fire_snd[e.weapon]) play_tag_at(s, s->unit_fire_snd[e.weapon], e.pos, 1.0f);
            break;
        case HTA_EV_HIT_WORLD:
            if (e.weapon >= 0 && e.weapon < (int32_t)s->game.weapon_count &&
                s->game.weapons[e.weapon].vehicle) {
                uint32_t sound = hta_projectile_impact_sound(&s->cache,
                    s->game.weapons[e.weapon].def.projectile_id, e.material);
                if (sound) play_tag_at(s, sound, e.pos, 0.8f);
                if (e.material < 33u && s->impact_recipe[e.material] != HTA_PART_NO_RECIPE)
                    hta_particles_burst(&s->parts, s->impact_recipe[e.material], e.pos, e.dir);
            } else if (e.weapon >= 0 && e.weapon == held_roster(s)) {
                play_impact_at(s, e.material, e.pos);
                if (e.material < 33u && s->impact_recipe[e.material] != HTA_PART_NO_RECIPE)
                    hta_particles_burst(&s->parts, s->impact_recipe[e.material], e.pos, e.dir);
            }
            hta_gun_add_mark(&s->gun, e.pos, e.dir, HTA_MARK_SIZE);
            break;
        case HTA_EV_DETONATE:
            if (e.pool >= 0 && (uint32_t)e.pool < s->game.pool_count) {
                const hta_projectiles *pl = &s->game.pools[e.pool];
                if (pl->detonation_snd) play_tag_at(s, pl->detonation_snd, e.pos, 1.0f);
                shake_effect(s, pl->det_effect, e.pos);
                if (s->pool_recipe[e.pool] != HTA_PART_NO_RECIPE)
                    hta_particles_burst(&s->parts, s->pool_recipe[e.pool], e.pos, e.dir);
                if (pl->blast_radius > 0.0f)
                    hta_gun_add_mark(&s->gun, e.pos, e.dir, pl->blast_radius);
                if (pl->blast_damage > 20.0f)
                    hta_game_view_debris(&s->gview, e.pos, e.dir, 6, 5.5f);
            }
            break;
        case HTA_EV_HIT_UNIT: {
            character_voice(s,e.a,false);
            if (e.b == s->me && e.a >= 0 && e.a != s->me) hit_feedback(s, e.a, e.pos, e.amount);
            /* The body answers the round: the weapon's own impact on a
             * cyborg's shield while it holds, on armour after. Heard near
             * enough to matter. */
            if (e.a < 0 || e.a >= (int32_t)s->game.unit_count || e.a == s->me) break;
            const hta_unit *v = &s->game.units[e.a];
            int32_t w = -1;
            if (e.b >= 0 && e.b < (int32_t)s->game.unit_count) {
                const hta_unit *k = &s->game.units[e.b];
                const hta_game_weapon *hw = hta_game_held(&s->game, e.b);
                w = hw ? (int32_t)(hw - s->game.weapons) : -1;
                if (k->vehicle >= 0 && (uint32_t)k->vehicle < s->vehicles.count) {
                    uint16_t ty = s->vehicles.cars[k->vehicle].type;
                    const hta_vehicle_seat *st = hta_vehicles_seat(&s->vehicles,
                        (uint32_t)k->vehicle, (uint32_t)k->seat);
                    if (st && (st->flags & HTA_SEAT_GUNNER) && ty < HTA_VEHICLE_TYPES)
                        w = s->game.vweapon[ty][0];
                }
            }
            if (w < 0 || w >= (int32_t)s->game.weapon_count ||
                !s->game.weapons[w].def.projectile_id) break;
            uint8_t mat = v->vitals.shield > 0.0f ? HTA_MATERIAL_CYBORG_SHIELD
                                                   : HTA_MATERIAL_CYBORG_ARMOR;
            uint32_t snd = hta_projectile_impact_sound(&s->cache,
                s->game.weapons[w].def.projectile_id, mat);
            if (snd) play_tag_at(s, snd, e.pos, e.b == s->me ? 1.0f : 0.7f);
            break;
        }
        case HTA_EV_WRECK:
            if (s->net_hosting) {
                hta_net_fx fx = { .kind = HTA_NET_FX_WRECK,
                                  .entity = e.a >= 0 && e.a < HTA_GAME_MAX_UNITS ? (uint8_t)e.a : 255,
                                  .weapon = (uint8_t)(e.b & 31), .material = 0 };
                for (int k = 0; k < 3; k++) { fx.pos[k] = e.pos[k]; fx.dir[k] = e.dir[k]; }
                hta_net_server_fx(&s->host_server, &fx);
            }
            wreck_fx(s, e.pos);
            break;
        case HTA_EV_PICKUP:
            if (e.a == s->me && s->me >= 0) {
                /* The game added the rounds to its copy. The gun in hand
                 * reads the platform's magazine, so copy them back or the
                 * next frame throws the pickup away. */
                hta_unit *u = &s->game.units[s->me];
                for (unsigned k = 0; k < s->held_count && k < 2; k++) {
                    int32_t mine = s->held_asset[k] >= 0 ? s->held_asset[k]
                                   : hta_game_weapon_index(&s->game, s->held[k]);
                    if (mine < 0 || u->carry[k].weapon != mine) continue;
                    if (k == (s->held_slot & 1u)) s->ammo = u->carry[k].ammo;
                    else { s->held_ammo[k] = u->carry[k].ammo; s->held_ammo_set[k] = true; }
                }
            }
            {
                hta_item_choice ch;
                char path[96];
                memset(&ch, 0, sizeof(ch));
                hta_item_describe(&s->cache, e.tag, path, sizeof(path), &ch);
                uint32_t snd = ch.pickup_snd;
                if (!snd) {
                    hta_weapon_def wd;
                    if (hta_weapon_load_id(&s->cache, NULL, e.tag, &wd, NULL, NULL, 0))
                        snd = wd.pickup_snd_id;
                }
                if (snd) {
                    if (e.a == s->me) play_tag(s, snd, 0.7f);
                    else play_tag_at(s, snd, e.pos, 0.8f);
                }
            }
            break;
        case HTA_EV_KILL:
            character_voice(s,e.a,true);
            if (e.b == s->me && e.a >= 0 && e.a != s->me && s->kill_clip != HTA_AUDIO_NO_CLIP)
                hta_audio_play(&s->audio, s->kill_clip, 0.9f);
            if (e.a == s->me && e.b >= 0 && e.b != s->me && e.b < (int32_t)s->game.unit_count) {
                s->killcam_unit = e.b;
                s->killcam_phase = 0;
                s->killcam_weapon[0] = 0;
                if (e.weapon >= 0 && (uint32_t)e.weapon < s->game.weapon_count)
                    snprintf(s->killcam_weapon, sizeof(s->killcam_weapon), "%s", s->game.weapons[e.weapon].display);
            }
            if (s->net_hosting && e.a>=0 && e.a<HTA_GAME_MAX_UNITS) {
                hta_net_kill kill={0};
                kill.victim=(uint8_t)e.a;
                kill.killer=e.b>=0 && e.b<HTA_GAME_MAX_UNITS ? (uint8_t)e.b : 255;
                size_t k=0;
                while (k<sizeof(kill.text)-1 && e.text[k]) {
                    unsigned char ch=(unsigned char)e.text[k];
                    kill.text[k]=(char)(ch>=32 && ch<127 ? ch : '?'); k++;
                }
                kill.text[k]=0;
                if (e.a<(int32_t)s->game.unit_count && s->game.units[e.a].gibbed) {
                    kill.flags=HTA_NET_KILL_GIBBED;
                    kill.amount=e.amount>4.25f ? 4.25f : e.amount>0.0f ? e.amount : 0.0f;
                    for (int c=0;c<3;c++) {
                        kill.pos[c]=fminf(fmaxf(e.pos[c],-99999.0f),99999.0f);
                        kill.from[c]=fminf(fmaxf(e.dir[c],-99999.0f),99999.0f);
                    }
                }
                hta_net_server_kill(&s->host_server,&kill,s->last_time);
            }
            if (e.b == s->me && e.a != s->me) {
                char fmt[64];
                if (!hta_ustr_get(&s->cache, s->game.text_tag, 88, fmt, sizeof(fmt)))
                    snprintf(fmt, sizeof(fmt), "You killed %%s");
                snprintf(buf, sizeof(buf), fmt, s->game.units[e.a].name);
                feed_push(s, buf);
            } else {
                feed_push(s, e.text);
            }
            break;
        case HTA_EV_ANNOUNCE:
            if (!e.for_local) break;
            snprintf(s->banner, sizeof(s->banner), "%s", e.text);
            s->banner_age = 0.0f;
            if (e.line > HTA_LINE_NONE && e.line < HTA_LINE_COUNT && s->line_snd[e.line])
                play_tag(s, s->line_snd[e.line], 1.0f);
            break;
        case HTA_EV_FLAG:
            if (e.text[0]) {
                snprintf(s->banner, sizeof(s->banner), "%s", e.text);
                s->banner_age = 0.0f;
            }
            if (e.line > HTA_LINE_NONE && e.line < HTA_LINE_COUNT && s->line_snd[e.line])
                play_tag(s, s->line_snd[e.line], 1.0f);
            if (e.pool == HTA_FLAG_TAKEN && s->flag_take_snd)
                play_tag_at(s, s->flag_take_snd, e.pos, 1.0f);
            break;
        case HTA_EV_ENTER:
            if (s->veh_in_snd) play_tag_at(s, s->veh_in_snd, e.pos, 1.0f);
            break;
        case HTA_EV_EXIT:
            if (s->veh_out_snd) play_tag_at(s, s->veh_out_snd, e.pos, 1.0f);
            break;
        case HTA_EV_GAME_OVER:
            snprintf(s->banner, sizeof(s->banner), "%s", e.text);
            s->banner_age = 0.0f;
            s->over_timer = HTA_POSTGAME;
            if (s->line_snd[HTA_LINE_GAME_OVER]) play_tag(s, s->line_snd[HTA_LINE_GAME_OVER], 1.0f);
            break;
        default:
            break;
        }
    }
}

/* The text the HUD draws, rebuilt a few times a second. */
static void game_text(hta_android *s, float dt)
{
    if (!s->game_on) { g_game_text[0] = 0; return; }
    s->banner_age += dt;
    for (int i = 0; i < 4; i++) s->feed_age[i] += dt;
    char place[96] = "";
    if (s->me >= 0 && s->game.unit_count > 1)
        hta_game_place_text(&s->game, s->me, place, sizeof(place));
    size_t n = 0;
    n += (size_t)snprintf(g_game_text + n, sizeof(g_game_text) - n, "%s\x1e%s\x1e",
                          s->banner_age < HTA_BANNER_TIME ? s->banner : "", place);
    for (int i = 3; i >= 0 && n < sizeof(g_game_text); i--)
        if (s->feed[i][0] && s->feed_age[i] < HTA_FEED_TIME)
            n += (size_t)snprintf(g_game_text + n, sizeof(g_game_text) - n, "%s\n", s->feed[i]);
    if (n < sizeof(g_game_text))
        n += (size_t)snprintf(g_game_text + n, sizeof(g_game_text) - n, "\x1e");
    size_t board_at = n;
    if (s->game.over && n < sizeof(g_game_text)) {
        int32_t order[HTA_GAME_MAX_UNITS];
        uint32_t k = hta_game_standings(&s->game, order, HTA_GAME_MAX_UNITS);
        for (uint32_t i = 0; i < k && n < sizeof(g_game_text); i++) {
            const hta_unit *u = &s->game.units[order[i]];
            n += (size_t)snprintf(g_game_text + n, sizeof(g_game_text) - n,
                                  "%u\t%s\t%d\t%d\t%d\t%d\n", i + 1, u->name, u->score,
                                  u->kills, u->assists, u->deaths);
        }
    }
    (void)board_at;
    s->item_msg_age += dt;
    if (n < sizeof(g_game_text))
        n += (size_t)snprintf(g_game_text + n, sizeof(g_game_text) - n, "\x1e%s",
                              s->item_msg_age < 2.5f ? s->item_msg : "");
    /* Waypoints: where each flag is, as screen fractions, its team, how far
     * in metres, and whether it is on screen (else pinned to the edge). */
    if (n < sizeof(g_game_text))
        n += (size_t)snprintf(g_game_text + n, sizeof(g_game_text) - n, "\x1e");
    if (s->game.mode == HTA_MODE_CTF && s->me >= 0 && !s->dead && !s->game.over) {
        hta_mat4 vp = hta_camera_view_proj(&s->cam);
        for (int t = 0; t < 2 && n < sizeof(g_game_text); t++) {
            const hta_game_flag *f = &s->game.flags[t];
            if (!f->present) continue;
            if (f->state == HTA_FLAG_CARRIED && f->carrier == s->me) continue;
            float at[4] = { f->pos[0], f->pos[1], f->pos[2] + 0.9f, 1.0f };
            if (f->state == HTA_FLAG_CARRIED && f->carrier >= 0 &&
                f->carrier < (int32_t)s->game.unit_count) {
                const hta_unit *cu = &s->game.units[f->carrier];
                at[0] = cu->body.pos[0]; at[1] = cu->body.pos[1]; at[2] = cu->body.pos[2] + 1.0f;
            }
            float clip[4];
            hta_mat4_transform(&vp, at, clip);
            float dx = at[0]-s->cam.pos[0], dy = at[1]-s->cam.pos[1], dz = at[2]-s->cam.pos[2];
            float metres = sqrtf(dx*dx + dy*dy + dz*dz) * 3.048f;
            float x, y;
            bool on = clip[3] > 0.05f;
            if (on) {
                x = 0.5f + 0.5f * clip[0] / clip[3];
                y = 0.5f + 0.5f * clip[1] / clip[3];
                on = x > 0.04f && x < 0.96f && y > 0.08f && y < 0.90f;
            } else {
                /* Behind: along the bottom, on the side it lies. */
                float right[3];
                hta_camera_right(&s->cam, right);
                x = dx*right[0] + dy*right[1] > 0.0f ? 0.96f : 0.04f;
                y = 0.90f;
            }
            if (x < 0.04f) x = 0.04f;
            if (x > 0.96f) x = 0.96f;
            if (y < 0.08f) y = 0.08f;
            if (y > 0.90f) y = 0.90f;
            n += (size_t)snprintf(g_game_text + n, sizeof(g_game_text) - n, "%.3f,%.3f,%d,%.0f,%d,%d;",
                                  x, y, t, metres, on ? 1 : 0, (int)f->state);
        }
    }
}

/* ---------------------------------------------------------- diagnostics
 * What the phone measured, for an agent to read: the native half of a
 * report (GameActivity adds the device, memory, thermal state and log, and
 * posts it to the sideload server), and a crash record written from the
 * signal handler that the next launch sends. */

static char   g_crash_path[512];
static uintptr_t g_lib_base;
static char   g_altstack[64 * 1024];

typedef struct { uintptr_t pc[48]; int n; } crash_frames;

static _Unwind_Reason_Code crash_unwind(struct _Unwind_Context *ctx, void *arg)
{
    crash_frames *f = arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && f->n < 48) f->pc[f->n++] = pc;
    return f->n < 48 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

/* Async-signal-safe formatting: no stdio in a signal handler. */
static size_t put_str(char *b, size_t at, size_t cap, const char *s)
{
    while (*s && at + 1 < cap) b[at++] = *s++;
    return at;
}
static size_t put_hex(char *b, size_t at, size_t cap, uintptr_t v)
{
    char t[20];
    int n = 0;
    do { t[n++] = "0123456789abcdef"[v & 15u]; v >>= 4; } while (v && n < 16);
    at = put_str(b, at, cap, "0x");
    while (n && at + 1 < cap) b[at++] = t[--n];
    return at;
}
static size_t put_dec(char *b, size_t at, size_t cap, long v)
{
    char t[24];
    int n = 0;
    if (v < 0) { at = put_str(b, at, cap, "-"); v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 22);
    while (n && at + 1 < cap) b[at++] = t[--n];
    return at;
}

static void crash_handler(int sig, siginfo_t *info, void *uctx)
{
    static char b[4096];
    size_t at = 0, cap = sizeof(b);
    at = put_str(b, at, cap, "signal ");
    at = put_dec(b, at, cap, sig);
    at = put_str(b, at, cap, sig == SIGSEGV ? " SIGSEGV" : sig == SIGABRT ? " SIGABRT" : sig == SIGBUS ? " SIGBUS" :
                              sig == SIGFPE ? " SIGFPE" : sig == SIGILL ? " SIGILL" : "");
    at = put_str(b, at, cap, "\ncode ");
    at = put_dec(b, at, cap, info ? info->si_code : 0);
    at = put_str(b, at, cap, "\nfault_addr ");
    at = put_hex(b, at, cap, info ? (uintptr_t)info->si_addr : 0);
    at = put_str(b, at, cap, "\nphase ");
    at = put_str(b, at, cap, g_phase ? g_phase : "?");
    at = put_str(b, at, cap, "\nlib_base ");
    at = put_hex(b, at, cap, g_lib_base);
#if defined(__aarch64__)
    if (uctx) {
        const ucontext_t *uc = uctx;
        at = put_str(b, at, cap, "\npc ");
        at = put_hex(b, at, cap, (uintptr_t)uc->uc_mcontext.pc);
        at = put_str(b, at, cap, "\nlr ");
        at = put_hex(b, at, cap, (uintptr_t)uc->uc_mcontext.regs[30]);
    }
#else
    (void)uctx;
#endif
    crash_frames f;
    f.n = 0;
    _Unwind_Backtrace(crash_unwind, &f);
    at = put_str(b, at, cap, "\nframes");
    for (int i = 0; i < f.n; i++) {
        at = put_str(b, at, cap, "\n  ");
        at = put_hex(b, at, cap, f.pc[i]);
        if (g_lib_base && f.pc[i] >= g_lib_base) {
            at = put_str(b, at, cap, " lib+");
            at = put_hex(b, at, cap, f.pc[i] - g_lib_base);
        }
    }
    at = put_str(b, at, cap, "\n");
    int fd = g_crash_path[0] ? open(g_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0600) : -1;
    if (fd >= 0) { ssize_t w = write(fd, b, at); (void)w; close(fd); }
    /* Let Android record its own tombstone too. */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void crash_guard_install(const char *dir)
{
    if (!dir || !dir[0]) return;
    snprintf(g_crash_path, sizeof(g_crash_path), "%s/crash-native.txt", dir);
    Dl_info di;
    if (dladdr((void *)crash_guard_install, &di)) g_lib_base = (uintptr_t)di.dli_fbase;
    stack_t ss = { .ss_sp = g_altstack, .ss_size = sizeof(g_altstack), .ss_flags = 0 };
    sigaltstack(&ss, NULL);                 /* so a stack overflow is caught too */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    int sigs[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) sigaction(sigs[i], &sa, NULL);
    hta_log("[report] crash guard on: %s (lib base %p)", g_crash_path, (void *)g_lib_base);
}

/* The native half of a report, as JSON. Read from the UI thread while the
 * game runs: a snapshot, not a transaction -- fine for diagnostics. */
static size_t report_native(const hta_android *s, char *buf, size_t cap)
{
    hta_json j;
    hta_json_init(&j, buf, cap);
    double now = hta_time_seconds();
    hta_json_int(&j, "protocol", HTA_NET_VERSION);
    hta_json_num(&j, "uptime_s", s->started_at > 0 ? now - s->started_at : 0);
    hta_json_str(&j, "phase", g_phase);

    hta_json_object(&j, "match");
    hta_json_bool(&j, "on", s->game_on);
    hta_json_str(&j, "map", s->world[0] ? s->world : "bloodgulch");
    hta_json_int(&j, "map_key", s->world_loaded ? (long long)s->world_ext.key : 0);
    hta_json_int(&j, "map_crc", (long long)s->cache.crc32);
    hta_json_int(&j, "mode", s->game.mode);
    hta_json_int(&j, "units", s->game.unit_count);
    hta_json_int(&j, "bots", s->bot_count);
    hta_json_bool(&j, "me_alive", s->game_on && s->me >= 0 && s->game.units[s->me].alive);
    hta_json_int(&j, "score_red", s->game.team_score[0]);
    hta_json_int(&j, "score_blue", s->game.team_score[1]);
    hta_json_array(&j, "player_pos");
    for (int k = 0; k < 3; k++) hta_json_num(&j, NULL, s->player.pos[k]);
    hta_json_end_array(&j);
    hta_json_end_object(&j);

    hta_json_object(&j, "video");
    char cfg[1024];
    hta_gfx_settings_format(&s->video, cfg, sizeof(cfg));
    hta_json_str(&j, "preset", hta_quality_name(s->video.preset));
    hta_json_bool(&j, "auto", s->video_auto);
    hta_json_str(&j, "settings", cfg);
    if (s->gfx) {
        hta_json_str(&j, "gpu", hta_gfx_device_name(s->gfx));
        hta_json_bool(&j, "composed", hta_gfx_is_composed(s->gfx));
        hta_json_num(&j, "render_scale", hta_gfx_render_scale(s->gfx));
        hta_json_int(&j, "msaa", hta_gfx_msaa(s->gfx));
    }
    hta_json_int(&j, "window_w", s->win_w);
    hta_json_int(&j, "window_h", s->win_h);
    hta_json_end_object(&j);

    hta_json_object(&j, "frames");
    hta_json_frames(&j, "session", &s->frame_stats.session);
    hta_json_frames(&j, "last_minute", &s->frame_stats.last_minute);
    hta_json_frames(&j, "this_minute", &s->frame_stats.minute);
    hta_json_num(&j, "match_s", s->frame_stats.elapsed_ms / 1000.0);
    hta_json_hitches(&j, "hitches", &s->frame_stats);
    hta_json_end_object(&j);

    hta_json_object(&j, "audio");
    hta_json_bool(&j, "ok", s->audio_ok);
    hta_json_bool(&j, "running", hta_audio_android_running());
    hta_json_int(&j, "out_rate", s->audio.out_rate);
    hta_json_int(&j, "clips", s->audio.clip_count);
    hta_json_int(&j, "voices", hta_audio_active_voices(&s->audio));
    hta_json_int(&j, "started", s->audio.started);
    hta_json_int(&j, "stolen", s->audio.stolen);
    hta_json_int(&j, "dropped", (long long)atomic_load(&s->audio.dropped));
    hta_json_bool(&j, "procedural", s->wfx_audio.ready);
    hta_json_int(&j, "procedural_played", s->wfx_audio.played);
    hta_json_end_object(&j);

    hta_json_object(&j, "net");
    hta_json_bool(&j, "enabled", s->net_enabled);
    hta_json_bool(&j, "hosting", s->net_hosting);
    hta_json_bool(&j, "connected", s->net.connected);
    hta_json_int(&j, "id", s->net.id);
    hta_json_int(&j, "reject_reason", s->net.reject_reason);
    const hta_net_stats *ns = s->net_hosting ? &s->host_server.stats : &s->net.stats;
    hta_json_num(&j, "ping_ms", s->net.stats.ping_ms);
    hta_json_int(&j, "packets_in", (long long)ns->packets_in);
    hta_json_int(&j, "packets_out", (long long)ns->packets_out);
    hta_json_int(&j, "bytes_in", (long long)ns->bytes_in);
    hta_json_int(&j, "bytes_out", (long long)ns->bytes_out);
    hta_json_int(&j, "invalid", (long long)ns->invalid);
    hta_json_int(&j, "dropped", (long long)ns->dropped);
    hta_json_int(&j, "rate_limited", (long long)ns->limited);
    if (s->net_hosting) hta_json_int(&j, "peers", hta_net_server_count(&s->host_server));
    hta_json_end_object(&j);

    hta_json_object(&j, "effects");
    hta_json_bool(&j, "ready", s->wfx.ready);
    if (s->wfx.ready) {
        uint32_t broken = 0;
        for (uint32_t i = 0; i < s->wfx.props.count; i++) broken += s->wfx.props.props[i].broken;
        hta_json_int(&j, "debris", hta_rigid_active(&s->wfx.rigid));
        hta_json_int(&j, "debris_cap", s->wfx.rigid.cap);
        hta_json_int(&j, "sprites", hta_fx_live(&s->wfx.fx));
        hta_json_int(&j, "props", s->wfx.props.count);
        hta_json_int(&j, "props_broken", broken);
        hta_json_bool(&j, "props_remote", s->wfx.props.remote);
        hta_json_str(&j, "weather", hta_weather_name(s->wfx.weather.kind));
        hta_json_num(&j, "weather_intensity", s->wfx.weather.intensity);
        hta_json_int(&j, "gib_level", s->wfx.gib_level);
        hta_json_int(&j, "gibbed", s->wfx.gibbed);
    }
    hta_json_int(&j, "collision_instances", s->col.instance_count);
    hta_json_bool(&j, "collision_indexed", s->col.instance_index != NULL);
    hta_json_int(&j, "nav_nodes", s->nav.node_count);
    hta_json_int(&j, "nav_blocked", s->nav.blocked_nodes);
    hta_json_end_object(&j);

    return hta_json_finish(&j);
}

JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeReport(JNIEnv *env, jclass cls)
{
    (void)cls;
    static char buf[16384];
    if (!g_android) return (*env)->NewStringUTF(env, "{}");
    report_native(g_android, buf, sizeof(buf));
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeGameText(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, g_game_text);
}

/* Rigid things drawn with a matrix each this frame: guns in hands and
 * vehicle parts. Handed to the renderer once, just before the draw. */
static hta_gfx_instance g_inst[HTA_GFX_MAX_INSTANCES];
static uint32_t g_inst_count;

/* The bodies, their guns and their rounds, onto the draw list. */
/* Our own body is an imported one: it falls as itself, drawn from `gview`,
 * rather than as the Spartan corpse. */
static bool mine_imported(const hta_android *s)
{
    return s->game_on && s->me >= 0 && s->game.units[s->me].character >= 0;
}

/* Whose body `gview` leaves alone: ours, unless it is on screen. */
static int32_t view_skip(const hta_android *s)
{
    return s->show_self || (s->dead && mine_imported(s)) ? -1 : s->me;
}

static uint32_t game_draw(hta_android *s, hta_gfx_dynamic *dyn, uint32_t n)
{
    if (!s->game_on || !s->gfx) return n;
    for (uint32_t i = 0; i < s->game.unit_count && n < HTA_GFX_MAX_DYNAMIC; i++) {
        if ((int32_t)i == view_skip(s) || !s->gview.shown[i] || !s->gpu_units[i])
            continue;
        if (s->gpu_unit_char[i] != s->game.units[i].character) {
            /* A new body (a class pick, a joiner's model): its own mesh. */
            char err[HTA_ERRLEN];
            hta_gfx_mesh_free(s->gfx, s->gpu_units[i]);
            s->gpu_units[i] = hta_gfx_mesh_upload_dynamic_world(s->gfx,
                hta_game_view_body_mesh(&s->gview, &s->game, i), err, sizeof(err));
            s->gpu_unit_char[i] = s->game.units[i].character;
            if (!s->gpu_units[i]) continue;
        }
        dyn[n].mesh = s->gpu_units[i];
        dyn[n].vertices = hta_game_view_body_vertices(&s->gview, &s->game, i);
        dyn[n].vertex_count = hta_game_view_body_mesh(&s->gview, &s->game, i)->vertex_count;
        dyn[n].lit = true;
        if (s->game.teams) {
            dyn[n].change = true;
            hta_game_team_color(s->game.units[i].team, dyn[n].change_color);
        }
        n++;
    }
    for (uint32_t p = 0; p < s->game.pool_count && n < HTA_GFX_MAX_DYNAMIC; p++) {
        if (!s->gpu_pools[p]) continue;
        dyn[n].mesh = s->gpu_pools[p];
        dyn[n].vertices = s->game.pools[p].mesh.vertices;
        dyn[n].vertex_count = s->game.pools[p].mesh.vertex_count;
        dyn[n].lit = false;
        n++;
    }
    hta_game_held_weapon held[HTA_GAME_MAX_UNITS];
    uint32_t nh = hta_game_view_weapons(&s->gview, &s->game, s->show_self ? -1 : s->me,
                                        held, HTA_GAME_MAX_UNITS);
    for (uint32_t k = 0; k < nh && g_inst_count < HTA_GFX_MAX_INSTANCES; k++) {
        if (!s->gpu_held[held[k].weapon]) continue;
        hta_gfx_instance *in = &g_inst[g_inst_count++];
        in->mesh = s->gpu_held[held[k].weapon];
        memcpy(in->model, held[k].model, sizeof(in->model));
        in->first_submesh = held[k].first_submesh;
        in->submesh_count = held[k].submesh_count;
        in->lit = true;
    }
    /* The flags: upright on their stands, or lying where they fell. */
    for (int t = 0; t < 2 && s->game.flag_weapon >= 0; t++) {
        float fm[16];
        uint32_t first[2], count[2];
        if (!s->gpu_held[s->game.flag_weapon] || !hta_game_flag_model(&s->game, t, fm)) continue;
        uint32_t parts = hta_game_view_flag_parts(&s->gview, t, first, count);
        for (uint32_t p = 0; p < parts && g_inst_count < HTA_GFX_MAX_INSTANCES; p++) {
            hta_gfx_instance *in = &g_inst[g_inst_count++];
            in->mesh = s->gpu_held[s->game.flag_weapon];
            memcpy(in->model, fm, sizeof(fm));
            in->first_submesh = first[p];
            in->submesh_count = count[p];
            in->lit = true;
        }
    }
    /* Weapons on the ground, lying on their side. */
    for (int i = 0; i < HTA_GAME_MAX_DROPS && g_inst_count < HTA_GFX_MAX_INSTANCES; i++) {
        const hta_game_drop *d = &s->game.drops[i];
        if (!d->live || d->weapon < 0 || d->weapon >= HTA_GAME_MAX_WEAPONS ||
            !s->gpu_held[d->weapon]) continue;
        float cy = cosf(d->yaw), sy = sinf(d->yaw);
        hta_gfx_instance *in = &g_inst[g_inst_count++];
        in->mesh = s->gpu_held[d->weapon];
        /* Model +X along the yaw, +Y up (on its side), +Z to the side. */
        float m[16] = { cy, sy, 0, 0,   0, 0, 1, 0,   sy, -cy, 0, 0,
                        d->pos[0], d->pos[1], d->pos[2] + 0.05f, 1 };
        hta_game_view_weapon_space(&s->game, d->weapon, m);
        memcpy(in->model, m, sizeof(m));
        in->first_submesh = in->submesh_count = 0;
        in->lit = true;
    }
    return n;
}

/* Every vehicle, as rigid parts of its type's one mesh. */
static void vehicles_draw(hta_android *s)
{
    if (!s->vehicles.loaded || !s->gfx) return;
    static hta_vehicle_part parts[HTA_GFX_MAX_INSTANCES];
    uint32_t np = hta_vehicles_parts(&s->vehicles, parts, HTA_GFX_MAX_INSTANCES - g_inst_count);
    for (uint32_t i = 0; i < np && g_inst_count < HTA_GFX_MAX_INSTANCES; i++) {
        if (parts[i].type >= HTA_VEHICLE_TYPES || !s->gpu_vtypes[parts[i].type]) continue;
        hta_gfx_instance *in = &g_inst[g_inst_count++];
        in->mesh = s->gpu_vtypes[parts[i].type];
        memcpy(in->model, parts[i].model, sizeof(in->model));
        in->first_submesh = parts[i].first_submesh;
        in->submesh_count = parts[i].submesh_count;
        in->lit = true;
    }
}

/* ------------------------------- main menu ------------------------------ */

#define HTA_LOOP_MUSIC 10u
#define HTA_LOOP_MUSIC_IN 11u   /* the intro, stopped when it has played once */

/* A ui.map sound as a mixer clip, or HTA_AUDIO_NO_CLIP. `chain` joins a
 * long sound's segments -- the title music is cut into five-second pieces. */
static uint32_t menu_clip(hta_android *s, uint32_t tag, bool chain)
{
    if (!tag || !s->audio_ok || !s->sounds_rm.data || s->menu_pcm_count >= 8) return HTA_AUDIO_NO_CLIP;
    char err[HTA_ERRLEN];
    hta_pcm pcm;
    bool ok = chain ? hta_sound_decode_chain(&s->ui_cache, &s->sounds_rm, tag, &s->rng, &pcm, err, sizeof(err))
                    : hta_sound_decode(&s->ui_cache, &s->sounds_rm, tag, 0, &pcm, err, sizeof(err));
    if (!ok) { hta_log("[menu] sound 0x%08X: %s", tag, err); return HTA_AUDIO_NO_CLIP; }
    uint32_t clip = hta_audio_add_clip(&s->audio, pcm.samples, pcm.frame_count,
                                       pcm.sample_rate, pcm.channels);
    if (clip == HTA_AUDIO_NO_CLIP) { hta_pcm_free(&pcm); return clip; }
    s->menu_pcm[s->menu_pcm_count++] = pcm.samples;   /* the mixer reads it */
    return clip;
}

static bool menu_load(hta_android *s)
{
    char err[HTA_ERRLEN];
    char path[512];
    catalog_build(s);
    if (!find_map(s)) return false;           /* where the data is, not loaded */
    if (!find_named(s, "ui.map", path, sizeof(path)) ||
        !map_data_file(s, path, &s->ui_data, &s->ui_size) ||
        !hta_cache_open(&s->ui_cache, s->ui_data, s->ui_size, err, sizeof(err))) {
        hta_log("[menu] no usable ui.map; straight into the game");
        return false;
    }
    if (!s->bitmaps_data && find_named(s, "bitmaps.map", s->bitmaps_path, sizeof(s->bitmaps_path)) &&
        map_data_file(s, s->bitmaps_path, &s->bitmaps_data, &s->bitmaps_size))
        hta_resource_open(&s->bitmaps_rm, s->bitmaps_data, s->bitmaps_size, err, sizeof(err));
    if (!s->sounds_data && find_named(s, "sounds.map", s->sounds_path, sizeof(s->sounds_path)) &&
        map_data_file(s, s->sounds_path, &s->sounds_data, &s->sounds_size))
        hta_resource_open_typed(&s->sounds_rm, s->sounds_data, s->sounds_size,
                                HTA_RESOURCE_SOUNDS, err, sizeof(err));
    if (!hta_menu_load(&s->menu, &s->ui_cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL,
                       err, sizeof(err))) {
        hta_log("[menu] %s", err);
        return false;
    }
    hta_log("[menu] %s", err);
    if (!atomic_load(&g_shell_ready) &&
        hta_shell_load(&g_shell, &s->ui_cache, s->bitmaps_rm.data ? &s->bitmaps_rm : NULL))
        atomic_store(&g_shell_ready, 1);
    g_shell_shown = -1;
    s->menu_clip[0] = menu_clip(s, s->menu.snd_cursor, false);
    s->menu_clip[1] = menu_clip(s, s->menu.snd_forward, false);
    s->menu_clip[2] = menu_clip(s, s->menu.snd_back, false);
    /* The title theme: its intro once, then its loop for as long as you
     * sit here. Both are the looping sound's own first track. */
    s->music_in = s->music_loop = HTA_AUDIO_NO_CLIP;
    hta_loop_sound ls;
    if (s->menu.music && hta_loop_sound_track(&s->ui_cache, s->menu.music, &ls)) {
        s->music_in = menu_clip(s, ls.start, true);
        s->music_loop = menu_clip(s, ls.loop, true);
    }
    if (s->music_in != HTA_AUDIO_NO_CLIP) {
        hta_audio_loop(&s->audio, HTA_LOOP_MUSIC_IN, s->music_in, 0.9f);
        const hta_audio_clip *cl = &s->audio.clips[s->music_in];
        s->music_left = cl->rate ? (float)cl->frames / (float)cl->rate : 0.0f;
    }
    s->menu_pressed = -1;
    return true;
}

static void menu_gpu_upload(hta_android *s)
{
    if (!s->menu_mode || !s->menu.loaded || !s->gfx) return;
    char err[HTA_ERRLEN];
    if (s->menu.scene.index_count)
        s->gpu_menu_scene = hta_gfx_mesh_upload(s->gfx, &s->menu.scene, err, sizeof(err));
    if (s->menu.sky.index_count)
        s->gpu_menu_sky = hta_gfx_mesh_upload(s->gfx, &s->menu.sky, err, sizeof(err));
    s->gpu_menu_ui = hta_gfx_mesh_upload_dynamic(s->gfx, &s->menu.overlay, err, sizeof(err));
}

static void menu_gpu_free(hta_android *s)
{
    if (s->gpu_menu_scene) { hta_gfx_mesh_free(s->gfx, s->gpu_menu_scene); s->gpu_menu_scene = NULL; }
    if (s->gpu_menu_sky) { hta_gfx_mesh_free(s->gfx, s->gpu_menu_sky); s->gpu_menu_sky = NULL; }
    if (s->gpu_menu_ui) { hta_gfx_mesh_free(s->gfx, s->gpu_menu_ui); s->gpu_menu_ui = NULL; }
}

/* A Java method on the activity with no arguments. */
static void call_activity(hta_android *s, const char *method)
{
    JavaVM *vm = s->app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK || !env) return;
    jclass cls = (*env)->GetObjectClass(env, s->app->activity->clazz);
    jmethodID mid = cls ? (*env)->GetMethodID(env, cls, method, "()V") : NULL;
    if (mid) (*env)->CallVoidMethod(env, s->app->activity->clazz, mid);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}

static void menu_sound(hta_android *s, int which)
{
    if (s->audio_ok && s->menu_clip[which] != HTA_AUDIO_NO_CLIP)
        hta_audio_play(&s->audio, s->menu_clip[which], 1.0f);
}

static void menu_activate(hta_android *s, int item)
{
    menu_sound(s, 1);
    switch (item) {
    /* The submenus are the Java overlay's; it tells us which is up. */
    case HTA_MENU_CAMPAIGN:    call_activity(s, "openSolo"); break;
    case HTA_MENU_MULTIPLAYER: call_activity(s, "openMultiplayer"); break;
    case HTA_MENU_SETTINGS:    call_activity(s, "openSettings"); break;
    case HTA_MENU_CREDITS:     call_activity(s, "showCredits"); break;
    case HTA_MENU_QUIT:        ANativeActivity_finish(s->app->activity); break;
    default: break;
    }
}

/* From the Java overlay: 0 down, 1 move, 2 up; x and y are 0..1 of the
 * screen. The HUD owns the touches, so the menu hears them through here. */
/* Where the first menu slot is, for the Java side to label it SINGLEPLAYER
 * over title art: x0 y0 x1 y1 (0..1 of the screen), selected, 0 none. */
static float g_solo_rect[5];
static _Atomic int g_solo_rect_ok;

JNIEXPORT jfloatArray JNICALL
Java_net_hta_halotrial_GameActivity_nativeMenuSoloRect(JNIEnv *env, jclass cls)
{
    (void)cls;
    if (!atomic_load(&g_solo_rect_ok)) return NULL;
    jfloatArray out = (*env)->NewFloatArray(env, 5);
    if (out) (*env)->SetFloatArrayRegion(env, out, 0, 5, g_solo_rect);
    return out;
}

static _Atomic int g_menu_touch_action = -1;
static _Atomic int g_menu_touch_x, g_menu_touch_y;   /* x 10000ths */

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeMenuTouch(JNIEnv *env, jclass cls, jint action,
                                                    jfloat x, jfloat y)
{
    (void)env; (void)cls;
    atomic_store(&g_menu_touch_x, (int)(x * 10000.0f));
    atomic_store(&g_menu_touch_y, (int)(y * 10000.0f));
    /* An up must not be lost behind a move, so it is never overwritten. */
    if (atomic_load(&g_menu_touch_action) != 2 || action == 0)
        atomic_store(&g_menu_touch_action, action);
}

static _Atomic int g_menu_mode;

JNIEXPORT jint JNICALL
Java_net_hta_halotrial_GameActivity_nativePaused(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return atomic_load(&g_paused);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativePause(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    atomic_store(&g_paused, 1);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeResume(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    atomic_store(&g_paused, 0);
}

JNIEXPORT jint JNICALL
Java_net_hta_halotrial_GameActivity_nativeMenuMode(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return atomic_load(&g_menu_mode);
}


/* ---- the submenus -------------------------------------------------
 * The Java overlay draws them over the ring, with ui.map's art and words,
 * which are copied out here once and kept for the life of the process, so
 * the UI thread can read them while this thread loads and frees levels. */
JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeShellText(JNIEnv *env, jclass cls)
{
    (void)cls;
    if (!atomic_load(&g_shell_ready) || !g_shell.text) return NULL;
    return (*env)->NewStringUTF(env, g_shell.text);
}

/* One piece of art as { width, height, ARGB... }, or null. */
JNIEXPORT jintArray JNICALL
Java_net_hta_halotrial_GameActivity_nativeShellArt(JNIEnv *env, jclass cls, jint which)
{
    (void)cls;
    if (!atomic_load(&g_shell_ready) || which < 0 || which >= HTA_SHELL_ART_COUNT) return NULL;
    const hta_shell_image *im = &g_shell.art[which];
    if (!im->rgba || !im->width || !im->height) return NULL;
    size_t n = (size_t)im->width * im->height;
    jint *px = (jint *)malloc((n + 2u) * sizeof(jint));
    if (!px) return NULL;
    px[0] = (jint)im->width; px[1] = (jint)im->height;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *c = im->rgba + i * 4u;
        px[i + 2] = (jint)(((uint32_t)c[3] << 24) | ((uint32_t)c[0] << 16) |
                           ((uint32_t)c[1] << 8) | c[2]);
    }
    jintArray out = (*env)->NewIntArray(env, (jsize)(n + 2u));
    if (out) (*env)->SetIntArrayRegion(env, out, 0, (jsize)(n + 2u), px);
    free(px);
    return out;
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeShellScreen(JNIEnv *env, jclass cls, jint screen)
{
    (void)env; (void)cls;
    atomic_store(&g_shell_screen, (int)screen);
}

/* The shell's clicks: 0 cursor, 1 forward, 2 back. */
static _Atomic int g_shell_sounds;

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeShellSound(JNIEnv *env, jclass cls, jint which)
{
    (void)env; (void)cls;
    if (which >= 0 && which < 3) atomic_fetch_or(&g_shell_sounds, 1 << which);
}

/* A match set up in the submenus, handed over once. */
typedef struct {
    int  mode;               /* 0 solo, 1 host, 2 join */
    int  bots, skill, kills, minutes, respawn, max_players, port, vehicles;
    int  gametype;           /* hta_game_mode; solo only for now */
    int  protect;            /* spawn protection, seconds; 0 none */
    int  duplicate_heroes;   /* custom match rule */
    char host[64];
    char name[HTA_NET_NAME];
    char map[HTA_NET_MAP];   /* "" Blood Gulch, else an imported map's name */
} match_setup;
static match_setup g_match;
static _Atomic int g_match_ready;

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeStartMatch(JNIEnv *env, jclass cls, jintArray cfg,
                                                     jstring host, jstring name, jstring map)
{
    (void)cls;
    if (atomic_load(&g_match_ready)) return;     /* one is already on its way */
    match_setup m;
    memset(&m, 0, sizeof(m));
    jint v[12] = { 0, 3, 1, 25, 0, 5, 8, 32270, HTA_VROSTER_ALL, HTA_MODE_SLAYER, 0, 0 };
    jsize n = cfg ? (*env)->GetArrayLength(env, cfg) : 0;
    if (n > 12) n = 12;
    if (n > 0) (*env)->GetIntArrayRegion(env, cfg, 0, n, v);
    m.mode = v[0]; m.bots = v[1]; m.skill = v[2]; m.kills = v[3];
    m.minutes = v[4]; m.respawn = v[5]; m.max_players = v[6]; m.port = v[7];
    m.vehicles = v[8];
    m.gametype = v[9];
    m.protect = v[10];
    m.duplicate_heroes = v[11];
    const char *u;
    if (host && (u = (*env)->GetStringUTFChars(env, host, NULL))) {
        snprintf(m.host, sizeof(m.host), "%s", u);
        (*env)->ReleaseStringUTFChars(env, host, u);
    }
    if (name && (u = (*env)->GetStringUTFChars(env, name, NULL))) {
        snprintf(m.name, sizeof(m.name), "%s", u);
        (*env)->ReleaseStringUTFChars(env, name, u);
    }
    if (map && (u = (*env)->GetStringUTFChars(env, map, NULL))) {
        if (strcmp(u, "bloodgulch")) snprintf(m.map, sizeof(m.map), "%s", u);
        (*env)->ReleaseStringUTFChars(env, map, u);
    }
    g_match = m;
    atomic_store(&g_match_ready, 1);             /* publishes g_match */
}

/* Ask the LAN (or given addresses) who is hosting. `targets` is a comma
 * list of IPv4 addresses, broadcast ones included; each answer is a line
 * "ip \t port \t name \t players \t max \t kills \t minutes". Blocks for
 * `ms`; call it off the UI thread. */
JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeLanScan(JNIEnv *env, jclass cls, jstring targets,
                                                  jint port, jint ms)
{
    (void)cls;
    char list[512] = "", out[2048] = "";
    const char *u;
    if (targets && (u = (*env)->GetStringUTFChars(env, targets, NULL))) {
        snprintf(list, sizeof(list), "%s", u);
        (*env)->ReleaseStringUTFChars(env, targets, u);
    }
    hta_net_scan scan;
    if (!hta_net_scan_open(&scan)) return (*env)->NewStringUTF(env, "");
    if (port <= 0 || port > 65535) port = 32270;
    double t0 = hta_time_seconds(), next_send = t0;
    size_t len = 0;
    char seen[16][16];
    int nseen = 0;
    while (hta_time_seconds() - t0 < (double)ms / 1000.0) {
        if (hta_time_seconds() >= next_send) {
            /* A few tries: broadcasts get dropped on busy Wi-Fi. */
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s", list);
            for (char *tok = strtok(tmp, ","); tok; tok = strtok(NULL, ","))
                hta_net_scan_send(&scan, tok, (uint16_t)port);
            next_send += 0.4;
        }
        hta_net_info info;
        char ip[16];
        uint16_t from = 0;
        while (hta_net_scan_recv(&scan, &info, ip, &from)) {
            int dup = 0;
            for (int i = 0; i < nseen; i++) dup |= !strcmp(seen[i], ip);
            if (dup || nseen >= 16) continue;
            snprintf(seen[nseen++], 16, "%s", ip);
            int w = snprintf(out + len, sizeof(out) - len, "%s\t%u\t%s\t%u\t%u\t%u\t%u\t%s\n",
                             ip, from, info.name, info.players, info.max_players,
                             info.score_limit, info.time_limit,
                             info.map[0] ? info.map : "bloodgulch");
            if (w > 0 && (size_t)w < sizeof(out) - len) len += (size_t)w;
        }
        struct timespec nap = { 0, 20 * 1000 * 1000 };
        nanosleep(&nap, NULL);
    }
    hta_net_scan_close(&scan);
    return (*env)->NewStringUTF(env, out);
}

/* Open the network side of a match: host (a server here, and this phone
 * joins it through loopback) or join someone else's. */
static void net_begin(hta_android *s, const char *host, bool hosting, uint16_t port,
                      const hta_net_info *info)
{
    if (!host || !host[0]) return;
    if (hosting) {
        s->net_hosting = hta_net_server_open(&s->host_server, port);
        if (!s->net_hosting) hta_log("[net] could not bind LAN host UDP %u", port);
        else if (info) {
            uint8_t max = s->host_server.info.max_players;
            s->host_server.info = *info;
            if (!s->host_server.info.max_players) s->host_server.info.max_players = max;
        }
    }
    s->net_enabled = hta_net_client_open(&s->net, host, port);
    if (!s->net_enabled && s->net_hosting) {
        hta_net_server_close(&s->host_server);
        s->net_hosting = false;
    }
    if (hosting && !s->net_hosting && s->net_enabled) {
        hta_net_client_close(&s->net); s->net_enabled = false;
    }
    atomic_store(&g_net_status, s->net_enabled ? 1 : 5);
    hta_log("[net] %s %s:%u", s->net_enabled ? (hosting ? "hosting, joined" : "joining")
                                            : "bad address", host, port);
}

/* The submenus have set up a match: take it, and go. */
static void match_take(hta_android *s)
{
    match_setup m = g_match;
    s->selected_team=-1;
    s->next_character=-2;
    atomic_store(&g_team_request,0);
    s->bot_count = m.mode == 2 ? 0 : m.bots;
    if (s->bot_count < 0) s->bot_count = 0;
    if (s->bot_count > 7) s->bot_count = 7;
    s->bot_skill = m.skill < 0 ? 0 : m.skill > 3 ? 3 : m.skill;
    s->score_limit = m.kills > 0 ? m.kills : 0;
    s->time_limit_min = m.minutes > 0 ? m.minutes : 0;
    /* Halo's INSTANT still has to show you your body go down. Ours. */
    s->respawn_delay = m.respawn < HTA_RESPAWN_MIN ? HTA_RESPAWN_MIN : (float)m.respawn;
    s->vehicle_roster = m.vehicles >= 0 && m.vehicles < HTA_VROSTER_COUNT ? m.vehicles
                                                                           : HTA_VROSTER_ALL;
    /* A host chooses the game; a joiner learns it from the host's GAME. */
    s->game_mode = m.mode != 2 && m.gametype > 0 && m.gametype < HTA_MODE_COUNT
                 ? m.gametype : HTA_MODE_SLAYER;
    snprintf(s->world, sizeof(s->world), "%s", m.map);
    /* Ours, and only off, 2, 3 or 5 from the menu: long enough to look
     * round, short enough that camping the spawn is not a hiding place. */
    s->spawn_protect = m.protect > 0 && m.protect <= 10 ? (float)m.protect : 0.0f;
    s->allow_duplicate_heroes = m.duplicate_heroes != 0;
    snprintf(s->my_character, sizeof(s->my_character), "%s", g_loadout.character);
    s->bots_imported = g_loadout.bots_imported != 0;
    s->classes = g_loadout.classes != 0;
    for (int k = 0; k < 2; k++) snprintf(s->my_class[k], sizeof(s->my_class[k]), "%s", g_loadout.cls[k]);
    uint16_t port = (uint16_t)(m.port > 0 && m.port < 65536 ? m.port : 32270);
    if (m.mode == 1) {
        hta_net_info info;
        memset(&info, 0, sizeof(info));
        info.max_players = (uint8_t)(m.max_players < 2 ? 2 : m.max_players > (int)HTA_NET_MAX_PLAYERS
                                     ? (int)HTA_NET_MAX_PLAYERS : m.max_players);
        info.score_limit = (uint8_t)(s->score_limit ? s->score_limit : HTA_SLAYER_SCORE_LIMIT);
        info.time_limit = (uint8_t)s->time_limit_min;
        snprintf(info.name, sizeof(info.name), "%s", m.name[0] ? m.name : "Halo");
        snprintf(info.map, sizeof(info.map), "%s", m.map);
        net_begin(s, "127.0.0.1", true, port, &info);
    } else if (m.mode == 2) {
        net_begin(s, m.host, false, port, NULL);
    }
    hta_log("[menu] match on %s: mode %d, game %d, %d bot(s) skill %d, %d to win, %d min, respawn %.0f s",
            s->world[0] ? s->world : "bloodgulch", m.mode, s->game_mode, s->bot_count, s->bot_skill, s->score_limit, s->time_limit_min,
            s->respawn_delay);
    atomic_store(&g_match_ready, 0);
    atomic_store(&g_shell_screen, 0);
    s->menu_go = true;
}

/* MEGAMOD SHOWDOWN's title art for the main menu, from the Java side
 * (decoded from its resources before the native thread starts): ARGB
 * pixels, and where the art ends and the menu's band begins. */
JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeSetMenuArt(JNIEnv *env, jclass cls, jintArray argb,
                                                     jint w, jint h, jfloat art_right)
{
    (void)cls;
    if (!argb || w <= 0 || h <= 0 || (*env)->GetArrayLength(env, argb) < w * h) {
        hta_menu_set_art(NULL, 0, 0, 0.0f);
        return;
    }
    jint *px = (*env)->GetIntArrayElements(env, argb, NULL);
    uint8_t *rgba = px ? (uint8_t *)malloc((size_t)w * (size_t)h * 4u) : NULL;
    if (rgba) {
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
            uint32_t v = (uint32_t)px[i];
            rgba[i * 4 + 0] = (uint8_t)(v >> 16); rgba[i * 4 + 1] = (uint8_t)(v >> 8);
            rgba[i * 4 + 2] = (uint8_t)v;         rgba[i * 4 + 3] = (uint8_t)(v >> 24);
        }
        hta_menu_set_art(rgba, (uint32_t)w, (uint32_t)h, art_right);
        free(rgba);
    }
    if (px) (*env)->ReleaseIntArrayElements(env, argb, px, JNI_ABORT);
}

/* ------------------------------------------------ class preview */

/* What the class screen wants shown: a character by package id ("" for the
 * Spartan) holding a weapon by the name the game shows. Java writes it,
 * the game thread reads it; `on` says whether a class screen is up. */
static _Atomic int g_preview_on;
static char g_preview_char[48], g_preview_weap[48];

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeSetPreview(JNIEnv *env, jclass cls, jstring character,
                                                     jstring weapon, jint on)
{
    (void)cls;
    const char *u;
    if (character && (u = (*env)->GetStringUTFChars(env, character, NULL))) {
        snprintf(g_preview_char, sizeof(g_preview_char), "%s", u);
        (*env)->ReleaseStringUTFChars(env, character, u);
    }
    if (weapon && (u = (*env)->GetStringUTFChars(env, weapon, NULL))) {
        snprintf(g_preview_weap, sizeof(g_preview_weap), "%s", u);
        (*env)->ReleaseStringUTFChars(env, weapon, u);
    }
    atomic_store(&g_preview_on, on ? 1 : 0);
}

/* The body turning slowly in its idle, the class's primary in hand, on the
 * right of the screen (the class list is on the left). Imported bodies
 * only: the Spartan's comes from a loaded map, so the menu shows none. */
static void preview_draw(hta_android *s, float dt)
{
    rebuild_gfx_if_size_changed(s);
    if (!s->gfx) return;
    char want_c[48], want_w[48];
    snprintf(want_c, sizeof(want_c), "%s", g_preview_char);
    snprintf(want_w, sizeof(want_w), "%s", g_preview_weap);
    int32_t ci = -1;
    for (uint32_t k = 0; k < s->imp_char_count; k++) if (!strcmp(s->imp_char[k].name, want_c)) ci = (int32_t)k;
    int32_t wi = -1, ri = -1;
    for (uint32_t k = 0; k < s->imp_weap_count && wi < 0; k++)
        if (!strcasecmp(s->imp_weap[k].display, want_w)) wi = (int32_t)k;
    if (wi < 0 && s->game_on)
        for (uint32_t w = 0; w < s->game.weapon_count && ri < 0; w++)
            if (!s->game.weapons[w].asset && !strcasecmp(s->game.weapons[w].display, want_w) &&
                s->gpu_held[w]) ri = (int32_t)w;
    char err[HTA_ERRLEN];
    if (ci != s->prev_char) {
        if (s->gpu_prev_body) { hta_gfx_mesh_free(s->gfx, s->gpu_prev_body); s->gpu_prev_body = NULL; }
        s->prev_char = ci;
        s->prev_time = 0.0f;
        if (ci >= 0) {
            const hta_oal_model *m = &s->imp_char[ci].models[0];
            s->gpu_prev_body = hta_gfx_mesh_upload_dynamic_world(s->gfx, &m->mesh, err, sizeof(err));
            if (s->prev_cap < m->mesh.vertex_count) {
                free(s->prev_posed);
                s->prev_posed = malloc(m->mesh.vertex_count * sizeof(hta_vertex));
                s->prev_cap = s->prev_posed ? m->mesh.vertex_count : 0;
            }
            /* Height as it stands in its idle, not its bind pose: a TF2
             * body's bind mesh lies on its side. */
            s->prev_height = 0.0f;
            if (s->prev_posed) {
                static const float ident[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
                hta_oal_pose(m, hta_oal_clip_find(m, "idle"), 0.0f, s->prev_world);
                hta_oal_skin(m, (const float (*)[12])s->prev_world, ident, s->prev_posed);
                for (uint32_t v = 0; v < m->mesh.vertex_count; v++)
                    if (s->prev_posed[v].pos[2] > s->prev_height) s->prev_height = s->prev_posed[v].pos[2];
            }
        }
    }
    if (wi != s->prev_weap) {
        if (s->gpu_prev_weap) { hta_gfx_mesh_free(s->gfx, s->gpu_prev_weap); s->gpu_prev_weap = NULL; }
        s->prev_weap = wi;
        if (wi >= 0)
            s->gpu_prev_weap = hta_gfx_mesh_upload(s->gfx, &s->imp_weap[wi].models[0].mesh, err, sizeof(err));
    }
    s->prev_roster = ri;
    s->prev_time += dt;
    uint32_t w = 0, h = 0;
    hta_gfx_extent(s->gfx, &w, &h);
    hta_camera cam;
    float root[12];
    hta_preview_frame(s->prev_height, h ? (float)w / (float)h : 1.777f, 0.35f + s->prev_time * 0.5f, root, &cam);
    hta_gfx_dynamic dyn;
    memset(&dyn, 0, sizeof(dyn));
    uint32_t nd = 0;
    hta_gfx_instance inst;
    uint32_t ni = 0;
    if (ci >= 0 && s->gpu_prev_body && s->prev_posed) {
        const hta_oal_model *m = &s->imp_char[ci].models[0];
        const char *hold = wi >= 0 ? s->imp_weap[wi].hold_type : "";
        char stance = !strcmp(hold, "fist") ? 'f' : !strcmp(hold, "melee") ? 'm'
                    : !strcmp(hold, "pistol") ? 'p' : hold[0] ? 'r' : 0;
        char posed[16];
        int32_t idle = -1;
        if (stance) {
            snprintf(posed, sizeof(posed), "%c_idle", stance);
            idle = hta_oal_clip_find(m, posed);
        }
        if (idle < 0) idle = hta_oal_clip_find(m, "idle");
        hta_oal_pose(m, idle, s->prev_time, s->prev_world);
        hta_oal_skin(m, (const float (*)[12])s->prev_world, root, s->prev_posed);
        dyn.mesh = s->gpu_prev_body; dyn.vertices = s->prev_posed;
        dyn.vertex_count = m->mesh.vertex_count; dyn.lit = true;
        nd = 1;
        float m34[12];
        bool held = false;
        if (wi >= 0 && s->gpu_prev_weap) {
            held = hta_imported_hold_matrix(m, (const float (*)[12])s->prev_world, root,
                                            &s->imp_weap[wi].models[0], m34);
            inst.mesh = s->gpu_prev_weap;
        } else if (ri >= 0) {
            held = hta_imported_halo_in_source_hand(m, (const float (*)[12])s->prev_world, root, m34);
            inst.mesh = s->gpu_held[ri];
        }
        if (held) {
            hta_oal_to_mat4(m34, inst.model);
            inst.first_submesh = inst.submesh_count = 0;
            inst.lit = true;
            ni = 1;
        }
    }
    hta_gfx_set_instances(s->gfx, &inst, ni);
    /* Ours: a warm studio light on a dark, smoky ground. */
    hta_scene sc;
    memset(&sc, 0, sizeof(sc));
    sc.light_dir[0] = 0.55f; sc.light_dir[1] = -0.35f; sc.light_dir[2] = 0.75f;
    /* Light plus ambient stays near 1: brighter blows pale skin to white. */
    sc.light_color[0] = 0.66f; sc.light_color[1] = 0.62f; sc.light_color[2] = 0.58f;
    sc.ambient[0] = 0.36f; sc.ambient[1] = 0.35f; sc.ambient[2] = 0.37f;
    sc.clear[0] = 0.07f; sc.clear[1] = 0.05f; sc.clear[2] = 0.05f;
    if (!hta_gfx_draw(s->gfx, &cam, &sc, NULL, NULL, NULL, nd ? &dyn : NULL, nd, NULL, NULL)) {
        stop_gfx(s);
        if (s->app->window) start_gfx(s);
    }
}

/* One menu frame: the camera drifts, the words answer the finger, the
 * music plays. */
static void menu_frame(hta_android *s, float dt)
{
    if (atomic_load(&g_match_ready)) { match_take(s); return; }
    int snd = atomic_exchange(&g_shell_sounds, 0);
    for (int k = 0; k < 3; k++) if (snd & (1 << k)) menu_sound(s, k);
    int screen = atomic_load(&g_shell_screen);
    if (screen != g_shell_shown) {
        /* The Trial's own camera points; `multiplayer` looks at the ring's
         * inner face, which we still draw washed out, so the dark shots. */
        static const char *const SHOT[3] = { "uicam", "new_campaign", "load_campaign" };
        hta_menu_focus(&s->menu, SHOT[screen >= 0 && screen < 3 ? screen : 0]);
        s->menu.shell = screen != 0;
        s->menu_pressed = -1;
        g_shell_shown = screen;
    }
    int action = atomic_exchange(&g_menu_touch_action, -1);
    if (s->menu.shell) action = -1;     /* the overlay has the finger */
    if (action >= 0 && s->gfx) {
        uint32_t w = 0, h = 0;
        hta_gfx_extent(s->gfx, &w, &h);
        float x = (float)atomic_load(&g_menu_touch_x) / 10000.0f * (float)w;
        float y = (float)atomic_load(&g_menu_touch_y) / 10000.0f * (float)h;
        int hit = hta_menu_hit(&s->menu, x, y);
        if (action == 0 || action == 1) {
            if (hit >= 0 && hit != s->menu.selected) { s->menu.selected = hit; menu_sound(s, 0); }
            if (action == 0) s->menu_pressed = hit;
        } else if (action == 2) {
            if (hit >= 0 && hit == s->menu_pressed) menu_activate(s, hit);
            s->menu_pressed = -1;
        }
    }
    if (s->music_in != HTA_AUDIO_NO_CLIP && !s->music_looping) {
        s->music_left -= dt;
        if (s->music_left <= 0.0f) {
            hta_audio_loop_stop(&s->audio, HTA_LOOP_MUSIC_IN);
            if (s->music_loop != HTA_AUDIO_NO_CLIP)
                hta_audio_loop(&s->audio, HTA_LOOP_MUSIC, s->music_loop, 0.9f);
            s->music_looping = true;
        }
    }
    hta_audio_android_poll(&s->audio);
    hta_menu_update(&s->menu, dt);
    if (!s->has_window || !s->gfx) return;
    if (atomic_load(&g_preview_on)) { preview_draw(s, dt); return; }
    rebuild_gfx_if_size_changed(s);
    if (!s->gfx) return;
    uint32_t w = 0, h = 0;
    hta_gfx_extent(s->gfx, &w, &h);
    hta_menu_layout(&s->menu, w, h);
    if (s->menu.art && w && h && s->menu.item_rect[HTA_MENU_CAMPAIGN][2] > 0.0f) {
        const float *r = s->menu.item_rect[HTA_MENU_CAMPAIGN];
        g_solo_rect[0] = r[0] / (float)w; g_solo_rect[1] = r[1] / (float)h;
        g_solo_rect[2] = r[2] / (float)w; g_solo_rect[3] = r[3] / (float)h;
        g_solo_rect[4] = s->menu.selected == HTA_MENU_CAMPAIGN ? 1.0f : 0.0f;
        atomic_store(&g_solo_rect_ok, 1);
    } else {
        atomic_store(&g_solo_rect_ok, 0);
    }
    hta_camera cam;
    hta_menu_camera(&s->menu, &cam, h ? (float)w / (float)h : 1.777f);
    hta_scene sc = s->menu.light;
    hta_gfx_overlay ov = { s->gpu_menu_ui, s->menu.overlay.vertices, s->menu.overlay.vertex_count,
                           s->menu.overlay.submeshes, s->menu.overlay.submesh_count };
    /* Over title art the ring is not drawn: the art is the whole scene. */
    if (!hta_gfx_draw(s->gfx, &cam, &sc, s->menu.art ? NULL : s->gpu_menu_scene,
                      s->menu.art ? NULL : s->gpu_menu_sky, NULL, NULL, 0,
                      NULL, s->gpu_menu_ui ? &ov : NULL)) {
        stop_gfx(s);
        if (s->app->window) start_gfx(s);
    }
}

/* Leave the menu for the level: the music stops, the shell's meshes go,
 * and Blood Gulch loads behind the last menu frame. */
static void menu_leave(hta_android *s)
{
    hta_audio_loop_stop(&s->audio, HTA_LOOP_MUSIC_IN);
    hta_audio_loop_stop(&s->audio, HTA_LOOP_MUSIC);
    stop_gfx(s);
    hta_menu_free(&s->menu);
    s->menu_mode = false;
    atomic_store(&g_menu_mode, 0);
    if (!load_map(s)) {
        hta_log("[app] running without map data: %s", s->status);
        s->scene.clear[0] = 0.55f; s->scene.clear[1] = 0.05f; s->scene.clear[2] = 0.45f;
    }
    if (s->app->window) start_gfx(s);
}

/* ------------------------------- input ------------------------------- */

#define STICK_RADIUS_FRAC 0.12f   /* of the shorter screen edge */
#define LOOK_SENSITIVITY  0.006f
#define PAD_LOOK_SPEED    2.6f

/* What the Java HUD sent since the last frame. JNI calls arrive on the
 * activity's thread, so they write here under a lock and the frame takes
 * it whole, once (android_read_input); the gamepad's taps come the same
 * way from the looper. Nothing else touches the session from outside. */
static pthread_mutex_t g_hud_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    float move[2], look[2];
    bool  jump, fire, crouch, alt;                     /* held */
    bool  jump_pressed, fire_pressed, crouch_pressed, alt_pressed;
    bool  reload, melee, swap, zoom, grenade, fly, ability;
    int   debug;
} hud_mailbox;
static hud_mailbox g_hud_in;

static void hud_button(bool *held, bool *pressed, bool down)
{
    pthread_mutex_lock(&g_hud_lock);
    if (down && !*held) *pressed = true;
    *held = down;
    pthread_mutex_unlock(&g_hud_lock);
}

static void hud_tap(bool *request)
{
    pthread_mutex_lock(&g_hud_lock);
    *request = true;
    pthread_mutex_unlock(&g_hud_lock);
}

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
        /* The phone's own keys stay the phone's: answering 1 here swallowed
         * volume up/down, so the volume could only be changed outside the
         * game. */
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN ||
            code == AKEYCODE_VOLUME_MUTE || code == AKEYCODE_MUTE)
            return 0;
        if (code == AKEYCODE_BACK) {
            /* In the menu, BACK leaves the app. In a game it pauses, the
             * way Halo's does; the pause screen offers the way out. */
            if (down) {
                if (s->menu_mode && atomic_load(&g_shell_screen)) {
                    /* A submenu is up: BACK goes up one, as Halo's does. */
                    call_activity(s, "shellBack");
                } else if (s->menu_mode || !s->map_loaded) {
                    hta_log("[input] BACK -> exit");
                    ANativeActivity_finish(app->activity);
                } else {
                    atomic_store(&g_paused, !atomic_load(&g_paused));
                    hta_log("[input] BACK -> %s", atomic_load(&g_paused) ? "paused" : "resumed");
                }
            }
            return 1;
        }
        if (code == AKEYCODE_BUTTON_A || code == AKEYCODE_SPACE) { s->jump_held = down; return 1; }
        if (code == AKEYCODE_BUTTON_R1 || code == AKEYCODE_BUTTON_R2 ||
            code == AKEYCODE_BUTTON_X) { s->fire_held = down; return 1; }
        if (code == AKEYCODE_BUTTON_Y && down) { hud_tap(&g_hud_in.reload); return 1; }
        if (code == AKEYCODE_BUTTON_R1 && down) { hud_tap(&g_hud_in.melee); return 1; }
        if (code == AKEYCODE_BUTTON_L1 && down) { hud_tap(&g_hud_in.swap); return 1; }
        if (code == AKEYCODE_BUTTON_THUMBR && down) { hud_tap(&g_hud_in.zoom); return 1; }
        if (code == AKEYCODE_BUTTON_L2 && down) { hud_tap(&g_hud_in.grenade); return 1; }
        if (code == AKEYCODE_BUTTON_B && down) {
            s->player.noclip = !s->player.noclip;
            hta_log("[input] noclip %s", s->player.noclip ? "ON" : "OFF");
            return 1;
        }
        return 1;
    }
    return 0;
}

/* This frame's input, device-neutral: the HUD (or the hot-corner stick
 * without it), a gamepad and touch look, into hta_input for the session. */
static void android_read_input(hta_android *s, hta_input *in, float dt)
{
    memset(in, 0, sizeof(*in));
    pthread_mutex_lock(&g_hud_lock);
    hud_mailbox hud = g_hud_in;
    g_hud_in.look[0] = g_hud_in.look[1] = 0.0f;
    g_hud_in.jump_pressed = g_hud_in.fire_pressed = false;
    g_hud_in.crouch_pressed = g_hud_in.alt_pressed = false;
    g_hud_in.reload = g_hud_in.melee = g_hud_in.swap = g_hud_in.zoom = false;
    g_hud_in.grenade = g_hud_in.fly = g_hud_in.ability = false;
    g_hud_in.debug = 0;
    pthread_mutex_unlock(&g_hud_lock);

    /* Java HUD stick, or fallback invisible left-half stick */
    if (s->hud_ready) {
        in->move_right   += hud.move[0];
        in->move_forward += hud.move[1];
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

    /* accumulated touch look, native and HUD */
    in->look_yaw   += s->pending_yaw + hud.look[0];
    in->look_pitch += s->pending_pitch + hud.look[1];
    s->pending_yaw = s->pending_pitch = 0.0f;

    /* Held buttons from every source, and whether any went down. */
    bool fire_held = s->fire_held || s->pad_fire;
    in->jump = s->jump_held || hud.jump;
    in->jump_pressed = hud.jump_pressed || (s->jump_held && !s->prev_jump_held);
    in->fire = fire_held || hud.fire;
    in->fire_pressed = hud.fire_pressed || (fire_held && !s->prev_fire_held);
    s->prev_jump_held = s->jump_held;
    s->prev_fire_held = fire_held;
    in->crouch = hud.crouch;
    in->crouch_pressed = hud.crouch_pressed;
    in->alt_fire = hud.alt;
    in->alt_pressed = hud.alt_pressed;

    in->reload = hud.reload; in->melee = hud.melee; in->swap = hud.swap;
    in->zoom = hud.zoom; in->grenade = hud.grenade; in->fly = hud.fly;
    in->ability = hud.ability; in->debug = hud.debug;
}

/* ------------------------------ lifecycle ------------------------------ */

/* video.cfg beside the maps, written by SETTINGS. No file, or no preset
 * in it, means AUTO: a preset picked from the GPU once it is known. */
static void load_video(hta_android *s)
{
    s->video_loaded = true;
    s->video_auto = true;
    s->video_cfg_len = 0;
    hta_gfx_settings_preset(&s->video, HTA_QUALITY_MEDIUM);
    const char *dir = s->app->activity->externalDataPath;
    if (!dir) dir = s->app->activity->internalDataPath;
    char path[1024];
    if (!dir || snprintf(path, sizeof(path), "%s/video.cfg", dir) >= (int)sizeof(path)) return;
    FILE *f = fopen(path, "rb");
    if (!f) { hta_log("[video] no video.cfg: auto"); return; }
    s->video_cfg_len = fread(s->video_cfg, 1, sizeof(s->video_cfg) - 1, f);
    fclose(f);
    s->video_cfg[s->video_cfg_len] = 0;
    s->video_auto = strstr(s->video_cfg, "preset") == NULL;
    hta_gfx_settings_parse(&s->video, s->video_cfg, s->video_cfg_len);
    hta_log("[video] video.cfg: preset %s%s", hta_quality_name(s->video.preset),
            s->video_auto ? " (auto)" : "");
}

static void start_gfx(hta_android *s)
{
    char err[HTA_ERRLEN];
    if (!s->video_loaded) load_video(s);
    /* AUTO starts on the original renderer, learns the GPU's name, then
     * moves to the suggested preset; a chosen preset starts on it. */
    s->gfx = hta_gfx_create_window_ex(s->app->window, s->video_auto ? NULL : &s->video,
                                      err, sizeof(err));
    if (!s->gfx) { hta_log("[gfx] init FAILED: %s", err); s->has_window = false; return; }
    if (s->video_auto) {
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        hta_quality q = hta_gfx_settings_suggest(hta_gfx_device_name(s->gfx), true, 0,
                                                 cores > 0 ? (uint32_t)cores : 0);
        hta_gfx_settings_preset(&s->video, q);
        /* The rest of the file (gore, weather) still applies. */
        hta_gfx_settings_parse(&s->video, s->video_cfg, s->video_cfg_len);
        s->video.preset = q;
        s->video_auto = false;
        hta_log("[video] auto: %s on '%s' (%ld cores)", hta_quality_name(q),
                hta_gfx_device_name(s->gfx), cores);
    }
    if (!hta_gfx_apply_settings(s->gfx, &s->video, err, sizeof(err))) {
        hta_log("[video] %s preset failed (%s): the original renderer instead",
                hta_quality_name(s->video.preset), err);
        /* Adopt what works, or every frame's look would retry the build. */
        uint32_t gib = s->video.gib_level;
        float wd = s->video.weather_density, pd = s->video.particle_density;
        uint32_t md = s->video.max_debris;
        hta_gfx_get_settings(s->gfx, &s->video);
        s->video.gib_level = gib; s->video.weather_density = wd;
        s->video.particle_density = pd; s->video.max_debris = md;
    }
    hta_log("[video] %s: %s, scale %.2f, msaa %u", hta_quality_name(s->video.preset),
            hta_gfx_is_composed(s->gfx) ? "composed" : "direct", hta_gfx_render_scale(s->gfx),
            hta_gfx_msaa(s->gfx));
    s->game.gore = (uint8_t)s->video.gib_level;
    if (s->wfx.ready && !hta_wfx_gpu_upload(&s->wfx, s->gfx))
        hta_log("[wfx] effect meshes failed to upload");
    s->has_window = true;
    s->win_w = ANativeWindow_getWidth(s->app->window);
    s->win_h = ANativeWindow_getHeight(s->app->window);

    uint32_t w, h;
    hta_gfx_extent(s->gfx, &w, &h);
    hta_log("[gfx] ready: %s, %ux%u", hta_gfx_device_name(s->gfx), w, h);
    s->cam.aspect = h ? (float)w / (float)h : 1.777f;
    menu_gpu_upload(s);

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
        }
        /* The world-space dynamic meshes. These are NOT conditional on
         * there being scorch marks -- they were, briefly, which meant
         * rockets and their smoke only appeared once you had shot a wall. */
        if (s->proj.loaded && s->proj.mesh.index_count)
            s->gpu_proj = hta_gfx_mesh_upload_dynamic_world(s->gfx, &s->proj.mesh,
                                                      err, sizeof(err));
        if (s->parts.loaded && s->parts.mesh.index_count)
            s->gpu_parts = hta_gfx_mesh_upload_dynamic(s->gfx, &s->parts.mesh,
                                                       err, sizeof(err));
        if (s->nades.loaded && s->nades.mesh.index_count)
            s->gpu_nades = hta_gfx_mesh_upload_dynamic_world(s->gfx, &s->nades.mesh,
                                                       err, sizeof(err));
        if (s->corpse.loaded && s->corpse.mesh.index_count) {
            s->gpu_corpse = hta_gfx_mesh_upload_dynamic_world(s->gfx, &s->corpse.mesh,
                                                        err, sizeof(err));
            if (!s->gpu_corpse) hta_log("[gfx] corpse upload FAILED: %s", err);
        }
        if (s->bot.loaded && s->bot.actor.mesh.index_count) {
            s->gpu_bot = hta_gfx_mesh_upload_dynamic_world(s->gfx, &s->bot.actor.mesh,
                                                     err, sizeof(err));
            if (!s->gpu_bot) hta_log("[gfx] bot upload FAILED: %s", err);
        }
        for (int slot=0;slot<2;slot++)
            if (s->remote[slot].loaded && s->remote[slot].mesh.index_count) {
                s->gpu_remote[slot]=hta_gfx_mesh_upload_dynamic_world(s->gfx,
                    &s->remote[slot].mesh,err,sizeof(err));
                if (!s->gpu_remote[slot]) hta_log("[gfx] remote upload FAILED: %s",err);
            }
        if (s->items.have_mesh && s->items.mesh.index_count) {
            s->gpu_items = hta_gfx_mesh_upload_dynamic_world(s->gfx, &s->items.mesh,
                                                       err, sizeof(err));
            if (!s->gpu_items) hta_log("[gfx] item upload FAILED: %s", err);
            s->items_upload = HTA_ITEMS_UPLOAD_FRAMES;
        }
        for (uint32_t t = 0; s->vehicles.loaded && t < s->vehicles.type_count; t++) {
            s->gpu_vtypes[t] = hta_gfx_mesh_upload(s->gfx, &s->vehicles.types[t].mesh,
                                                   err, sizeof(err));
            if (!s->gpu_vtypes[t]) hta_log("[gfx] vehicle %s upload FAILED: %s",
                                           s->vehicles.types[t].name, err);
        }
        game_gpu_upload(s);
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
    if (s->gpu_prev_body) { hta_gfx_mesh_free(s->gfx, s->gpu_prev_body); s->gpu_prev_body = NULL; }
    if (s->gpu_prev_weap) { hta_gfx_mesh_free(s->gfx, s->gpu_prev_weap); s->gpu_prev_weap = NULL; }
    s->prev_char = s->prev_weap = s->prev_roster = -2;
    game_gpu_free(s);
    menu_gpu_free(s);
    for (int slot=0;slot<2;slot++)
        if (s->gpu_remote[slot]) { hta_gfx_mesh_free(s->gfx,s->gpu_remote[slot]); s->gpu_remote[slot]=NULL; }
    for (uint32_t t = 0; t < HTA_VEHICLE_TYPES; t++)
        if (s->gpu_vtypes[t]) { hta_gfx_mesh_free(s->gfx, s->gpu_vtypes[t]); s->gpu_vtypes[t] = NULL; }
    if (s->gpu_bot) { hta_gfx_mesh_free(s->gfx, s->gpu_bot); s->gpu_bot = NULL; }
    if (s->gpu_items) { hta_gfx_mesh_free(s->gfx, s->gpu_items); s->gpu_items = NULL; }
    if (s->gpu_corpse) { hta_gfx_mesh_free(s->gfx, s->gpu_corpse); s->gpu_corpse = NULL; }
    if (s->gpu_hud) { hta_gfx_mesh_free(s->gfx, s->gpu_hud); s->gpu_hud = NULL; }
    if (s->gpu_fp) { hta_gfx_mesh_free(s->gfx, s->gpu_fp); s->gpu_fp = NULL; }
    if (s->gpu_nades) { hta_gfx_mesh_free(s->gfx, s->gpu_nades); s->gpu_nades = NULL; }
    if (s->gpu_parts) { hta_gfx_mesh_free(s->gfx, s->gpu_parts); s->gpu_parts = NULL; }
    if (s->gpu_proj) { hta_gfx_mesh_free(s->gfx, s->gpu_proj); s->gpu_proj = NULL; }
    if (s->gpu_fx) { hta_gfx_mesh_free(s->gfx, s->gpu_fx); s->gpu_fx = NULL; }
    if (s->gpu_sky) { hta_gfx_mesh_free(s->gfx, s->gpu_sky); s->gpu_sky = NULL; }
    if (s->gpu_mesh) { hta_gfx_mesh_free(s->gfx, s->gpu_mesh); s->gpu_mesh = NULL; }
    hta_wfx_gpu_free(&s->wfx, s->gfx);
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

/* NativeActivity intent is read once at startup. Only numeric IPv4 is
 * accepted by the UDP layer; there is no DNS lookup on the game thread. */
static void read_net_host(struct android_app *app, char host[64], bool *hosting)
{
    host[0]=0; *hosting=false;
    JavaVM *vm=app->activity->vm; JNIEnv *env=NULL;
    if ((*vm)->AttachCurrentThread(vm,&env,NULL)!=JNI_OK || !env) return;
    jobject activity=app->activity->clazz;
    jclass activity_class=(*env)->GetObjectClass(env,activity);
    jmethodID get_intent=(*env)->GetMethodID(env,activity_class,"getIntent","()Landroid/content/Intent;");
    jobject intent=get_intent ? (*env)->CallObjectMethod(env,activity,get_intent) : NULL;
    if (intent) {
        jclass intent_class=(*env)->GetObjectClass(env,intent);
        jmethodID get_string=(*env)->GetMethodID(env,intent_class,"getStringExtra",
                                                 "(Ljava/lang/String;)Ljava/lang/String;");
        jstring key=(*env)->NewStringUTF(env,"net_host");
        jstring value=get_string ? (jstring)(*env)->CallObjectMethod(env,intent,get_string,key) : NULL;
        if (value) {
            const char *utf=(*env)->GetStringUTFChars(env,value,NULL);
            if (utf) { snprintf(host,64,"%s",utf); (*env)->ReleaseStringUTFChars(env,value,utf); }
            (*env)->DeleteLocalRef(env,value);
        }
        (*env)->DeleteLocalRef(env,key);
        jmethodID get_bool=(*env)->GetMethodID(env,intent_class,"getBooleanExtra",
                                              "(Ljava/lang/String;Z)Z");
        jstring host_key=(*env)->NewStringUTF(env,"net_hosting");
        if (get_bool) *hosting=(*env)->CallBooleanMethod(env,intent,get_bool,host_key,JNI_FALSE);
        (*env)->DeleteLocalRef(env,host_key);
        (*env)->DeleteLocalRef(env,intent_class);
        (*env)->DeleteLocalRef(env,intent);
    }
    (*env)->DeleteLocalRef(env,activity_class);
}

/* An int extra on the launch intent, or `def`. The setup screen puts the
 * bot count and their skill there. */
static int intent_int(struct android_app *app, const char *key, int def)
{
    JavaVM *vm=app->activity->vm; JNIEnv *env=NULL;
    if ((*vm)->AttachCurrentThread(vm,&env,NULL)!=JNI_OK || !env) return def;
    int out=def;
    jobject activity=app->activity->clazz;
    jclass ac=(*env)->GetObjectClass(env,activity);
    jmethodID gi=(*env)->GetMethodID(env,ac,"getIntent","()Landroid/content/Intent;");
    jobject intent=gi ? (*env)->CallObjectMethod(env,activity,gi) : NULL;
    if (intent) {
        jclass ic=(*env)->GetObjectClass(env,intent);
        jmethodID get=(*env)->GetMethodID(env,ic,"getIntExtra","(Ljava/lang/String;I)I");
        jstring k=(*env)->NewStringUTF(env,key);
        if (get) out=(*env)->CallIntMethod(env,intent,get,k,(jint)def);
        (*env)->DeleteLocalRef(env,k);
        (*env)->DeleteLocalRef(env,ic);
        (*env)->DeleteLocalRef(env,intent);
    }
    (*env)->DeleteLocalRef(env,ac);
    return out;
}

static void net_action(hta_android *s, uint8_t kind)
{
    if (kind==HTA_NET_EVENT_MELEE) s->net_melee_count++;
    if (kind==HTA_NET_EVENT_GRENADE) s->net_grenade_count++;
    if (!s->net_enabled || !s->net.connected) return;
    if (s->net_hosting && kind==HTA_NET_EVENT_FIRE && s->game_on) {
        int32_t weapon=held_roster(s);
        if (weapon>=0) {
            hta_net_fx fx={.kind=HTA_NET_FX_FIRE,.entity=(uint8_t)s->me,
                           .weapon=(uint8_t)weapon};
            for (int k=0;k<3;k++) fx.pos[k]=s->cam.pos[k];
            hta_camera_forward(&s->cam,fx.dir);
            hta_net_server_fx(&s->host_server,&fx);
        }
    }
    hta_net_event e={s->net.id,kind,(uint8_t)s->held_slot,++s->net_event_id};
    hta_net_client_event(&s->net,&e);
}

/* A joiner's new unit needs its look on the GPU (hta_host_peers). */
static void host_unit_added(hta_session *session)
{
    game_gpu_upload((hta_android *)session);   /* the session is hta_android's first member */
}

static void net_host_peers(hta_android *s, double now)
{
    hta_host_peers(&s->session, now, host_unit_added);
}

static void mirror_local(hta_android *s) { hta_host_mirror_local(&s->session); }

/* Put the gun in hand on the ground: swapped for another, it stays. */
static void drop_held(hta_android *s)
{
    if (!s->game_on || (s->net_enabled && !s->net_hosting)) return;
    int32_t w=held_roster(s);
    if (w<0) return;
    float at[3]={s->player.pos[0],s->player.pos[1],s->player.pos[2]+0.3f};
    float vel[3]={cosf(s->cam.yaw)*0.6f,sinf(s->cam.yaw)*0.6f,0.8f};
    hta_game_drop_weapon(&s->game,w,&s->ammo,at,s->cam.yaw,vel);
}

static void net_host_world(hta_android *s) { hta_host_world(&s->session); }

/* The host's vehicles, as of its last snapshot and eased between them,
 * and who is sitting where. A client runs no vehicle physics. */
static void net_client_vehicles(hta_android *s, double now)
{
    if (!s->game_on || !s->vehicles.loaded) return;
    if (s->net.have_vehicles && s->net.last_vehicle_tick!=s->vehicles_applied_tick) {
        s->vehicles_applied_tick=s->net.last_vehicle_tick;
        for (uint32_t i=0;i<s->vehicles.count;i++)
            for (uint32_t k=0;k<HTA_VEHICLE_SEATS;k++) s->vehicles.cars[i].occupant[k]=-1;
        for (uint32_t u=0;u<s->game.unit_count;u++) {
            s->game.units[u].vehicle=-1; s->game.units[u].seat=-1;
        }
        for (uint8_t n=0;n<s->net.vehicles.count;n++) {
            const hta_net_vehicle *in=&s->net.vehicles.cars[n];
            if (in->index>=s->vehicles.count) continue;
            uint32_t i=in->index;
            hta_vehicle *v=&s->vehicles.cars[i];
            bool was=v->active;
            v->active=(in->flags&HTA_NET_VEHICLE_ACTIVE)!=0;
            v->grounded=(in->flags&HTA_NET_VEHICLE_GROUNDED)!=0;
            v->ctl.driven=(in->flags&HTA_NET_VEHICLE_DRIVEN)!=0;
            s->vfrom[i]=s->vhave[i] && was ? s->vto[i] : *in;
            s->vto[i]=*in; s->vhave[i]=true;
            uint32_t seats=hta_vehicles_seat_count(&s->vehicles,i);
            for (uint32_t k=0;k<seats && k<HTA_NET_VEHICLE_SEATS;k++) {
                uint8_t u=in->occupant[k];
                if (u==255 || u>=s->game.unit_count) continue;
                v->occupant[k]=(int8_t)u;
                s->game.units[u].vehicle=(int16_t)i;
                s->game.units[u].seat=(int8_t)k;
            }
        }
        s->vsnap_time=now;
    }
    /* Between snapshots, a straight line: 20 a second on a LAN. */
    float t=(float)((now-s->vsnap_time)/0.05);
    if (t<0.0f) t=0.0f;
    if (t>1.5f) t=1.5f;
    for (uint32_t i=0;i<s->vehicles.count;i++) {
        if (!s->vhave[i]) continue;
        hta_vehicle *v=&s->vehicles.cars[i];
        const hta_net_vehicle *a=&s->vfrom[i], *b=&s->vto[i];
        for (int k=0;k<3;k++) v->pos[k]=a->pos[k]+(b->pos[k]-a->pos[k])*t;
        #define LERP_ANGLE(x) (a->x+hta_angle_wrap(b->x-a->x)*t)
        v->yaw=LERP_ANGLE(yaw); v->pitch=LERP_ANGLE(pitch); v->roll=LERP_ANGLE(roll);
        v->bank=0.0f;
        v->aim_yaw=LERP_ANGLE(aim_yaw); v->aim_pitch=LERP_ANGLE(aim_pitch);
        v->steering=LERP_ANGLE(steering); v->wheel_spin=LERP_ANGLE(wheel_spin);
        v->barrel_spin=LERP_ANGLE(barrel_spin);
        #undef LERP_ANGLE
        v->speed=b->speed;
        unsigned w=0;
        for (uint32_t k=0;k<v->point_count && w<4;k++)
            if (v->points[k].wheel) { v->points[k].travel=a->travel[w]+(b->travel[w]-a->travel[w])*t; w++; }
    }
    hta_vehicles_sync(&s->vehicles);
}

static void net_client_projectiles(hta_android *s)
{
    if (!s->game_on || !s->net.have_projectiles ||
        s->net.last_projectile_tick==s->projectile_applied_tick) return;
    s->projectile_applied_tick=s->net.last_projectile_tick;
    for (uint32_t p=0;p<s->game.pool_count;p++)
        for (uint32_t slot=0;slot<HTA_PROJ_MAX;slot++)
            s->game.pools[p].live[slot].alive=false;
    for (uint32_t slot=0;slot<HTA_PROJ_MAX;slot++) {
        s->proj.live[slot].alive=false;
        s->nades.live[slot].alive=false;
    }
    for (uint8_t i=0;i<s->net.projectiles.count;i++) {
        const hta_net_projectile *in=&s->net.projectiles.live[i];
        if (in->slot>=HTA_PROJ_MAX) continue;
        hta_projectiles *pool=in->pool==HTA_NET_POOL_HOST_WEAPON ? &s->proj :
                              in->pool==HTA_NET_POOL_HOST_GRENADES ? &s->nades :
                              in->pool<s->game.pool_count ? &s->game.pools[in->pool] : NULL;
        if (!pool || !pool->loaded) continue;
        hta_projectile *q=&pool->live[in->slot];
        q->alive=true; q->speed=in->speed; q->fuse=-1.0f;
        for (int k=0;k<3;k++) { q->pos[k]=in->pos[k]; q->dir[k]=in->dir[k]; }
    }
    for (uint32_t p=0;p<s->game.pool_count;p++)
        hta_projectiles_update(&s->game.pools[p],NULL,0.0f);
    if (s->proj.loaded) hta_projectiles_update(&s->proj,NULL,0.0f);
    if (s->nades.loaded) hta_projectiles_update(&s->nades,NULL,0.0f);
}

static void net_client_world(hta_android *s)
{
    if (!s->game_on || !s->net.have_world ||
        s->world_applied_tick==s->net.last_world_tick) return;
    const hta_net_world *w=&s->net.world;
    int32_t mine=-1;
    for (uint8_t i=0;i<w->count;i++)
        if (w->entities[i].peer_id==s->net.id) mine=w->entities[i].id;
    if (mine<0 || mine>=HTA_GAME_MAX_UNITS) return;
    if (!s->world_local_bound) {
        if (mine!=s->me) {
            s->game.units[mine]=s->game.units[s->me];
            s->game.units[s->me].kind=HTA_UNIT_NONE;
            s->game.units[s->me].alive=false;
            s->me=mine;
            s->game.local=mine;
            s->vit=&s->game.units[mine].vitals;
        }
        s->world_local_bound=true;
    }
    if (s->world_round!=w->round) {
        s->world_round=w->round;
        memset(s->feed,0,sizeof(s->feed));
        s->banner[0]=0; s->over_timer=0.0f;
    }
    s->world_applied_tick=s->net.last_world_tick;
    s->bot_count=w->bot_count;
    bool was_over=s->game.over;
    s->game.time=w->time; s->game.over=w->over!=0;
    s->game.winner=w->winner==255 ? HTA_GAME_NONE : w->winner;
    s->game.score_limit=w->score_limit;
    s->game.time_limit=(float)w->time_limit*60.0f;
    s->game.respawn_time=w->respawn_time;
    s->respawn_delay=w->respawn_time;
    if (s->items.loaded && s->items.count==w->item_count) {
        for (uint8_t i=0;i<w->item_count;i++) {
            bool shown=(w->item_present[i>>3]&(1u<<(i&7u)))!=0;
            if (s->items.slot[i].present!=shown ||
                s->items.slot[i].choice!=w->item_choice[i]) {
                s->items.slot[i].present=shown;
                s->items.slot[i].choice=w->item_choice[i];
                s->items.dirty=true;
            }
        }
    }
    bool present[HTA_GAME_MAX_UNITS]={0};
    for (uint8_t i=0;i<w->count;i++) {
        const hta_net_entity *e=&w->entities[i];
        int32_t idx=e->id;
        present[idx]=true;
        if (s->game.unit_count<=(uint32_t)idx) s->game.unit_count=(uint32_t)idx+1u;
        hta_unit *u=&s->game.units[idx];
        bool was_alive=u->kind!=HTA_UNIT_NONE && u->alive;
        if (u->kind==HTA_UNIT_NONE) {
            memset(u,0,sizeof(*u));
            hta_player_init(&u->body);
            hta_player_apply_physics(&u->body,&s->game.phys);
            hta_camera_init(&u->eye);
            u->vitals=s->game.vitals_template;
        }
        bool fresh=u->kind==HTA_UNIT_NONE;
        u->kind=idx==s->me ? HTA_UNIT_LOCAL :
                e->kind==HTA_NET_ENTITY_BOT ? HTA_UNIT_BOT : HTA_UNIT_REMOTE;
        if (idx!=s->me)
            u->character=e->character && e->character<=s->game.character_count ? (int8_t)(e->character-1) : -1;
        else if ((e->flags & HTA_NET_ENTITY_CLASS_REJECT) && !s->choosing)
            class_hold(s);
        snprintf(u->name,sizeof(u->name),"%s",e->name);
        u->alive=(e->flags&HTA_NET_ENTITY_ALIVE)!=0;
        /* Back in: whole again. The KILL may beat the WORLD that shows the
         * death, so only a respawn clears it. */
        if (!was_alive && u->alive) u->gibbed=false;
        if (was_alive && !u->alive) {
            u->dead_for=0.0f;
            u->death_yaw=e->yaw;
        } else if (fresh && !u->alive) u->dead_for=1e4f;   /* not yet in: no body */
        else if (!u->alive) u->dead_for+=0.05f;
        u->score=e->score; u->kills=e->kills; u->deaths=e->deaths;
        u->fired=(e->flags&HTA_NET_ENTITY_FIRE)!=0;
        /* The motion tracker's "fired lately", kept here from the flag. */
        u->since_shot=u->fired ? 0.0f : u->since_shot+0.05f;
        u->meleed=(e->flags&HTA_NET_ENTITY_MELEE)!=0;
        u->threw=(e->flags&HTA_NET_ENTITY_GRENADE)!=0;
        u->team=(e->flags&HTA_NET_ENTITY_BLUE)!=0 ? HTA_TEAM_BLUE : HTA_TEAM_RED;
        u->body.on_ground=(e->flags&HTA_NET_ENTITY_GROUNDED)!=0;
        u->body.crouch_t=(e->flags&HTA_NET_ENTITY_CROUCH)!=0 ? 1.0f : 0.0f;
        u->slot=0;
        for (int slot=0;slot<2;slot++)
            u->carry[slot].weapon=e->carry[slot]==255 ? -1 : e->carry[slot];
        u->slot=e->slot;
        u->grenades=e->grenades;
        u->powerup=e->powerup;
        if (idx!=s->me) {
            for (int k=0;k<3;k++) u->body.pos[k]=e->pos[k];
            for (int k=0;k<2;k++) u->body.velocity[k]=e->velocity[k];
            u->eye.yaw=e->yaw; u->eye.pitch=e->pitch;
            for (int k=0;k<3;k++) u->eye.pos[k]=e->pos[k];
            u->eye.pos[2]+=u->body.eye_height;
        }
        if (idx==s->me && !u->alive && !s->dead) u->vitals.died=true;
        if (u->alive &&
            e->health+e->shield < u->vitals.health+u->vitals.shield-0.01f) {
            u->hurt=true;
            if (e->health>0.0f) character_voice(s,idx,false);
            if (idx==s->me) {
                u->vitals.took_damage=true;
                if (u->vitals.shield>0.0f && e->shield<=0.0f)
                    u->vitals.shield_broke=true;
            }
        }
        u->vitals.health=e->health; u->vitals.shield=e->shield;
        if (idx==s->me && u->alive) {
            if (s->dead) {
                respawn(s);
                s->dead=false;
                s->dead_timer=-HTA_DEATH_FADE_IN;
                s->cam.yaw=e->yaw; s->cam.pitch=e->pitch;
            }
            float dx=e->pos[0]-s->player.pos[0];
            float dy=e->pos[1]-s->player.pos[1];
            float dz=e->pos[2]-s->player.pos[2];
            float dist=sqrtf(dx*dx+dy*dy+dz*dz);
            float f=dist>0.5f || !s->net_spawned ? 1.0f : 0.12f;
            for (int k=0;k<3;k++) {
                float delta=(e->pos[k]-s->player.pos[k])*f;
                s->player.pos[k]+=delta;
                s->cam.pos[k]+=delta;
            }
            u->vitals.died=false;
        }
        if (idx==s->me) {
            uint32_t held[2]={0}; int32_t hasset[2]={-1,-1}; unsigned held_count=0;
            for (int slot=0;slot<2;slot++)
                if (u->carry[slot].weapon>=0 &&
                    (uint32_t)u->carry[slot].weapon<s->game.weapon_count) {
                    hasset[held_count]=s->game.weapons[u->carry[slot].weapon].asset ? u->carry[slot].weapon : -1;
                    held[held_count++]=s->game.weapons[u->carry[slot].weapon].tag;
                }
            if (held_count) {
                unsigned slot=e->slot<held_count ? e->slot : 0;
                bool change=s->held_count!=held_count || s->held_slot!=slot;
                for (unsigned k=0;k<held_count;k++)
                    if (s->held[k]!=held[k] || s->held_asset[k]!=hasset[k]) change=true;
                s->held_count=held_count; s->held_slot=slot;
                for (unsigned k=0;k<held_count;k++) { s->held[k]=held[k]; s->held_asset[k]=hasset[k]; }
                if (change) equip_weapon(s,s->held[slot]);
            }
            s->ammo.loaded=e->ammo_loaded;
            s->ammo.reserve=e->ammo_reserve;
            s->nade_count=e->grenades;
            s->powerup=e->powerup;
        }
    }
    for (uint32_t i=0;i<s->game.unit_count;i++)
        if (!present[i] && (int32_t)i!=s->me) {
            s->game.units[i].kind=HTA_UNIT_NONE;
            s->game.units[i].alive=false;
        }
    if (s->game.over && !was_over && s->game.teams && s->me>=0) {
        int mine=s->game.units[s->me].team;
        char buf[96];
        uint32_t tx=s->game.winner_team<0 ? 55 : s->game.winner_team==mine ? 58 : 56;
        const char *fb=s->game.winner_team<0 ? "Game ends in a draw" :
                       s->game.winner_team==mine ? "Your team won" : "Your team lost";
        if (!hta_ustr_get(&s->cache,s->game.text_tag,tx,buf,sizeof(buf)))
            snprintf(buf,sizeof(buf),"%s",fb);
        snprintf(s->banner,sizeof(s->banner),"%s",buf);
        s->banner_age=0.0f;
        if (s->line_snd[HTA_LINE_GAME_OVER]) play_tag(s,s->line_snd[HTA_LINE_GAME_OVER],1.0f);
    } else if (s->game.over && !was_over) {
        const char *winner="Nobody";
        if (s->game.winner>=0 && s->game.winner<(int32_t)s->game.unit_count)
            winner=s->game.units[s->game.winner].name;
        snprintf(s->banner,sizeof(s->banner),"%s",s->game.winner==s->me ?
                 "You won" : winner);
        s->banner_age=0.0f;
    }
    game_gpu_upload(s);
}

static void net_frame(hta_android *s, double now, float dt, const hta_player_input *in)
{
    if (!s->net_enabled) return;
    if (s->net_hosting) hta_net_server_pump(&s->host_server,now);
    hta_net_client_pump(&s->net,now);
    hta_net_fx fx;
    while (hta_net_client_pop_fx(&s->net,&fx)) {
        if (s->net_hosting || !s->game_on) continue;
        /* Our own on-foot shots we heard already; a vehicle gun we did not. */
        if (fx.entity==(uint8_t)s->me &&
            !(fx.kind==HTA_NET_FX_FIRE && fx.weapon<s->game.weapon_count &&
              (s->game.weapons[fx.weapon].vehicle || s->game.weapons[fx.weapon].hidden))) continue;
        if (fx.kind==HTA_NET_FX_FIRE && fx.weapon<s->game.weapon_count) {
            if (fx.entity<s->game.unit_count && s->game.weapons[fx.weapon].hero) {
                int c=s->game.units[fx.entity].character;
                if (c>=0 && (uint32_t)c<s->game.character_count) {
                    float interval=s->game.characters[c]->ability_interval;
                    s->gview.oal_power_time[fx.entity]=fmaxf(0.25f,fminf(interval*1.5f,0.5f));
                }
            }
            tracer(s,fx.entity==255 ? -1 : (int32_t)fx.entity,fx.weapon,fx.pos,fx.dir);
            if (!s->unit_fire_known[fx.weapon]) {
                s->unit_fire_known[fx.weapon]=1;
                s->unit_fire_snd[fx.weapon]=hta_effect_first_sound(&s->cache,
                    s->game.weapons[fx.weapon].def.firing_fx_id);
            }
            if (s->game.weapons[fx.weapon].hero) {}
            else if (imported_fire_sound(s, fx.weapon, fx.pos)) {}
            else if (s->unit_fire_snd[fx.weapon])
                play_tag_at(s,s->unit_fire_snd[fx.weapon],fx.pos,1.0f);
            if (s->game.weapons[fx.weapon].vehicle &&
                s->vfire_recipe[fx.weapon]!=HTA_PART_NO_RECIPE)
                hta_particles_burst(&s->parts,s->vfire_recipe[fx.weapon],fx.pos,fx.dir);
        } else if (fx.kind==HTA_NET_FX_IMPACT && fx.weapon<s->game.weapon_count) {
            uint32_t proj=s->game.weapons[fx.weapon].def.projectile_id;
            uint32_t sound=hta_projectile_impact_sound(&s->cache,proj,fx.material);
            if (sound) play_tag_at(s,sound,fx.pos,0.8f);
            hta_gun_add_mark(&s->gun,fx.pos,fx.dir,HTA_MARK_SIZE);
            /* The same round, on a joining phone: props chip and break here
             * too (host and client each run their own props; see HANDOFF). */
            hta_game_event hw={.kind=HTA_EV_HIT_WORLD,.a=-1,.material=fx.material};
            for (int k=0;k<3;k++) { hw.pos[k]=fx.pos[k]; hw.dir[k]=fx.dir[k]; }
            hta_wfx_game_event(&s->wfx,&hw,&s->game);
        } else if (fx.kind==HTA_NET_FX_WRECK) {
            wreck_fx(s,fx.pos);
            hta_wfx_net_fx(&s->wfx,HTA_WFX_NET_WRECK,fx.pos,fx.dir,0.0f);
        } else if (fx.kind==HTA_NET_FX_DETONATE && fx.weapon<s->game.pool_count) {
            const hta_projectiles *pool=&s->game.pools[fx.weapon];
            if (pool->detonation_snd)
                play_tag_at(s,pool->detonation_snd,fx.pos,1.0f);
            shake_effect(s,pool->det_effect,fx.pos);
            if (s->pool_recipe[fx.weapon]!=HTA_PART_NO_RECIPE)
                hta_particles_burst(&s->parts,s->pool_recipe[fx.weapon],fx.pos,fx.dir);
            if (pool->blast_radius>0.0f) {
                hta_gun_add_mark(&s->gun,fx.pos,fx.dir,pool->blast_radius);
                hta_wfx_net_fx(&s->wfx,HTA_WFX_NET_DETONATE,fx.pos,fx.dir,pool->blast_radius);
            }
        }
    }
    hta_net_kill kill;
    while (hta_net_client_pop_kill(&s->net,&kill)) {
        if (s->net_hosting) continue;
        /* The host blew this body apart: hide the corpse, throw the same
         * gibs it did (as hard, from the same blast). Our own gore setting
         * decides how much of it we draw. */
        if ((kill.flags&HTA_NET_KILL_GIBBED) && kill.victim<s->game.unit_count &&
            s->wfx.ready && s->wfx.gib_level>0) {
            s->game.units[kill.victim].gibbed=true;
            hta_game_event ge;
            memset(&ge,0,sizeof(ge));
            ge.kind=HTA_EV_KILL; ge.a=kill.victim; ge.b=kill.killer==255 ? -1 : kill.killer;
            ge.amount=kill.amount;
            memcpy(ge.pos,kill.pos,sizeof(ge.pos)); memcpy(ge.dir,kill.from,sizeof(ge.dir));
            hta_wfx_game_event(&s->wfx,&ge,&s->game);
        }
        character_voice(s,(int32_t)kill.victim,true);
        if (kill.killer==s->me && kill.victim!=s->me &&
            kill.victim<s->game.unit_count) {
            char line[96];
            snprintf(line,sizeof(line),"You killed %s",s->game.units[kill.victim].name);
            feed_push(s,line);
        } else feed_push(s,kill.text);
    }
    if (s->net_hosting) net_host_peers(s,now);
    if (!s->net.connected) {
        s->net_spawned=false; s->remote_visible=false;
        s->props_synced=false;
        if (!s->net_hosting) {
            s->world_applied_tick=0;
            s->projectile_applied_tick=0;
            s->world_local_bound=false;
            for (uint32_t i=0;i<s->game.unit_count;i++)
                if ((int32_t)i!=s->me) s->game.units[i].kind=HTA_UNIT_NONE;
            for (uint32_t p=0;p<s->game.pool_count;p++)
                for (uint32_t slot=0;slot<HTA_PROJ_MAX;slot++)
                    s->game.pools[p].live[slot].alive=false;
        }
    }
    atomic_store(&g_net_status, !s->net.connected ?
                 s->net.reject_reason==HTA_NET_REJECT_MAP ? 7 :
                 s->net.reject_reason==HTA_NET_REJECT_FULL ? 8 : 1 :
                 !s->net_hosting ? (s->net.have_world ? 2 : 6) :
                 hta_net_server_count(&s->host_server)>1 ? 4 : 3);
    if (s->net.connected && !s->net_spawned && s->spawn_count) {
        hta_spawn_point *sp=&s->spawn[(s->net.id-1u)%s->spawn_count];
        hta_player_spawn(&s->player,sp); s->cam.yaw=sp->facing;
        float z;
        if (s->col.built && hta_collision_ground(&s->col,s->player.pos[0],
                s->player.pos[1],s->player.pos[2]+spawn_lift(s),&z)) {
            s->player.pos[2]=z; s->player.on_ground=true;
        }
        s->net_spawned=true;
        hta_log("[net] joined as player %u",s->net.id);
    }
    if (!s->net_hosting) net_client_world(s);
    if (!s->net_hosting) net_client_projectiles(s);
    if (!s->net_hosting) net_client_vehicles(s,now);
    if (!s->net_hosting && s->game_on && s->net.have_game &&
        s->net.last_game_tick!=s->game_applied_tick) {
        s->game_applied_tick=s->net.last_game_tick;
        const hta_net_game *gm=&s->net.game;
        hta_game_flag_state fl[2];
        for (int t=0;t<2;t++) {
            fl[t].present=gm->flag[t].present!=0;
            fl[t].state=gm->flag[t].state;
            fl[t].carrier=gm->flag[t].carrier==255 ? -1 : gm->flag[t].carrier;
            for (int k=0;k<3;k++) fl[t].pos[k]=gm->flag[t].pos[k];
            fl[t].yaw=gm->flag[t].yaw;
        }
        int sc[2]={gm->team_score[0],gm->team_score[1]};
        hta_game_mode mode=(hta_game_mode)(gm->mode<HTA_MODE_COUNT ? gm->mode : 0);
        if (mode!=s->game.mode) hta_log("[net] the host plays mode %d",(int)mode);
        hta_game_mirror_rules(&s->game,mode,gm->score_limit,sc,
                              gm->winner_team==255 ? -1 : gm->winner_team,fl);
        /* The host's props: what it broke breaks here, what it rebuilt
         * comes back. The first sync after joining is quiet. */
        if (s->wfx.ready) {
            if (gm->prop_count!=s->wfx.props.count && !s->props_count_warned) {
                hta_log("[net] the host has %u props, we have %u",gm->prop_count,s->wfx.props.count);
                s->props_count_warned=true;
            }
            bool first=!s->props_synced;
            hta_props_apply_mask(&s->wfx.props,gm->prop_broken,gm->prop_count,
                                 first ? NULL : &s->wfx.rigid,first ? NULL : &s->wfx.fx);
            s->props_synced=true;
        }
        s->game.allow_duplicate_heroes = (gm->options & HTA_NET_GAME_DUPLICATES) != 0;
        s->allow_duplicate_heroes = s->game.allow_duplicate_heroes;
        s->game_mode=(int)s->game.mode;
        /* A custom-class game: pick one before the host lets you in. */
        if ((gm->options&HTA_NET_GAME_CLASSES) && !s->game.classes) {
            s->game.classes=true;
            class_apply(s);
            class_hold(s);
            hta_log("[net] the host plays with classes");
        }
        /* Hulls as the host has them, for our HULL readout and sparks. */
        for (uint32_t i=0;i<s->vehicles.count && i<HTA_NET_MAX_VEHICLES;i++) {
            s->game.vgun[i].hull_max=1.0f;
            s->game.vgun[i].hull=gm->hull[i] ? (float)(gm->hull[i]-1)/254.0f : 0.0f;
        }
    }
    if (!s->net_hosting && s->game_on && s->net.have_drops) {
        memset(s->game.drops,0,sizeof(s->game.drops));
        for (uint8_t i=0;i<s->net.drops.count && i<HTA_GAME_MAX_DROPS;i++) {
            hta_game_drop *d=&s->game.drops[i];
            d->live=s->net.drops.drop[i].weapon<s->game.weapon_count;
            d->weapon=s->net.drops.drop[i].weapon;
            for (int k=0;k<3;k++) d->pos[k]=s->net.drops.drop[i].pos[k];
            d->yaw=s->net.drops.drop[i].yaw; d->rest=true;
        }
    }
    if (s->net.connected && now-s->net_last_send>=0.05) {
        hta_net_control c={0};
        c.id=s->net.id; c.weapon_slot=(uint8_t)(s->held_slot&1u);
        c.forward=in->move_forward; c.right=in->move_right;
        c.yaw=s->cam.yaw; c.pitch=s->cam.pitch;
        if (in->jump) c.flags|=HTA_NET_JUMP;
        if (in->fire || s->veh_fire) c.flags|=HTA_NET_TRIGGER;
        if (in->crouch) c.flags|=HTA_NET_DUCK;
        if (s->hud_alt) c.flags|=HTA_NET_ALT;
        c.action_count=s->net_action_count;
        c.ability_count=s->net_ability_count;
        c.team=s->selected_team<0 ? 0 : (uint8_t)(s->selected_team+1);
        if(s->power_fly) c.flags|=HTA_NET_FLY;
        c.melee_count=s->net_melee_count;
        c.grenade_count=s->net_grenade_count;
        c.reload_count=s->net_reload_count;
        c.pickup_count=s->net_pickup_count;
        if (s->game_on && s->me>=0) {
            const hta_unit *mu=&s->game.units[s->me];
            for (int k=0;k<2;k++)
                c.loadout[k]=mu->loadout[k]>=0 && mu->loadout[k]<(int32_t)HTA_NET_MAX_WEAPONS ?
                    (uint8_t)mu->loadout[k] : 255;
            c.character=mu->character>=0 && mu->character<63 ? (uint8_t)(mu->character+1) : 0;
            /* Ready once the host's rules are known and no class is pending. */
            if (s->net.have_game && !s->choosing) c.flags|=HTA_NET_READY;
        } else c.loadout[0]=c.loadout[1]=255;
        hta_net_client_control(&s->net,&c);
        hta_net_player p={0}; p.id=s->net.id; p.weapon=(uint8_t)s->held_slot;
        for (int k=0;k<3;k++) { p.pos[k]=s->player.pos[k]; p.velocity[k]=s->player.velocity[k]; }
        p.yaw=s->cam.yaw; p.pitch=s->cam.pitch;
        if (s->player.on_ground) p.flags|=HTA_NET_GROUNDED;
        if (s->player.crouch_t>0.5f) p.flags|=HTA_NET_CROUCH;
        hta_net_client_state(&s->net,&p); s->net_last_send=now;
    }
    if (s->net.stats.snapshots_in!=s->net_last_snapshots) {
        s->net_last_snapshots=s->net.stats.snapshots_in;
        bool had_remote=s->remote_visible;
        s->remote_visible=false;
        for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) {
            if (!s->net.present[i] || i+1u==s->net.id) continue;
            hta_net_player next=s->net.players[i];
            s->remote_from=had_remote && s->remote_id==next.id
                ? s->remote_to : next;
            s->remote_to=next; s->remote_id=next.id;
            s->remote_snapshot_time=now; s->remote_visible=true;
            break; /* renderer currently has one remote actor slot */
        }
    }
    hta_net_event e;
    while (hta_net_client_pop_event(&s->net,&e)) {
        int slot=e.weapon==1 ? 1 : 0;
        hta_actor *a=&s->remote[slot];
        if (!a->loaded || e.actor!=s->remote_id) continue;
        const char *clip=NULL;
        if (e.kind==HTA_NET_EVENT_FIRE) clip=slot ? "stand pistol hp fire-1" : "stand rifle ar fire-1";
        if (e.kind==HTA_NET_EVENT_MELEE) clip=slot ? "stand pistol hp melee" : "stand rifle ar melee";
        if (e.kind==HTA_NET_EVENT_GRENADE) clip="stand rifle throw-grenade";
        if (clip && hta_actor_play(a,clip,false))
            s->remote_action_until=now+(e.kind==HTA_NET_EVENT_GRENADE ? 0.8 : 0.3);
        hta_log("[net] player %u event %u weapon %u",e.actor,e.kind,e.weapon);
    }
    if (s->remote_visible) {
        const hta_net_player *p=&s->remote_to;
        int slot=p->weapon==1 ? 1 : 0;
        hta_actor *a=&s->remote[slot];
        if (!a->loaded) goto net_after_remote;
        const char *clip=(p->flags&HTA_NET_CROUCH) ?
            (slot ? "crouch pistol idle" : "crouch rifle idle") :
            (slot ? "stand pistol idle" : "stand rifle idle");
        if (!(p->flags&HTA_NET_GROUNDED)) clip=(p->flags&HTA_NET_CROUCH)
             ? "crouch rifle airborne" : (slot ? "stand pistol airborne" : "stand rifle airborne");
        else if (hypotf(p->velocity[0],p->velocity[1])>0.2f)
            clip=(p->flags&HTA_NET_CROUCH) ?
                (slot ? "crouch pistol move-front" : "crouch rifle move-front") :
                (slot ? "stand pistol move-front" : "stand rifle move-front");
        if (now>=s->remote_action_until &&
            (a->clip<0 || strcmp(a->graph.anims[a->clip].name,clip)))
            hta_actor_play(a,clip,false);
        hta_net_player visible;
        if (hta_net_interpolate(&s->remote_from,p,
                (float)((now-s->remote_snapshot_time)/0.05),&visible)) {
            hta_actor_update(a,dt); hta_actor_place(a,visible.pos,visible.yaw);
        }
    }
net_after_remote:
    if (s->net_hosting) net_host_world(s);
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
            if (s->menu_mode && !s->menu.loaded && !menu_load(s)) {
                s->menu_mode = false;
                atomic_store(&g_menu_mode, 0);
            }
            if (!s->map_loaded && !s->menu_mode) {
                if (!(s->explore_external ? load_external_map(s) : load_map(s))) {
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

JNIEXPORT jint JNICALL
Java_net_hta_halotrial_GameActivity_nativeVehicleMode(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return atomic_load(&g_vehicle_mode);
}

JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeVehicleText(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, g_vehicle_text);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudAlt(JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    if (g_android) hud_button(&g_hud_in.alt, &g_hud_in.alt_pressed, down);
}

JNIEXPORT jint JNICALL
Java_net_hta_halotrial_GameActivity_nativeDamageFlash(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return atomic_load(&g_damage_flash);
}

JNIEXPORT jint JNICALL
Java_net_hta_halotrial_GameActivity_nativeNetStatus(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return atomic_load(&g_net_status);
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
    pthread_mutex_lock(&g_hud_lock);
    g_hud_in.move[0] = x;
    g_hud_in.move[1] = y;
    pthread_mutex_unlock(&g_hud_lock);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudLook(JNIEnv *env, jclass cls, jfloat dx, jfloat dy)
{
    (void)env; (void)cls;
    if (!g_android) return;
    pthread_mutex_lock(&g_hud_lock);
    g_hud_in.look[0] += -dx * LOOK_SENSITIVITY;
    g_hud_in.look[1] += -dy * LOOK_SENSITIVITY;
    pthread_mutex_unlock(&g_hud_lock);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudJump(JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    if (g_android) hud_button(&g_hud_in.jump, &g_hud_in.jump_pressed, down);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudFire(JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    if (g_android) hud_button(&g_hud_in.fire, &g_hud_in.fire_pressed, down);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudCrouch(JNIEnv *env, jclass cls, jboolean down)
{
    (void)env; (void)cls;
    if (g_android) hud_button(&g_hud_in.crouch, &g_hud_in.crouch_pressed, down);
}

/* A request, not a held button: the game loop consumes and clears it. */
JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudReload(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) hud_tap(&g_hud_in.reload);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudMelee(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) hud_tap(&g_hud_in.melee);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudSwap(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) hud_tap(&g_hud_in.swap);
}

/* The character's ability button, and how ready it is. */
JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudAbility(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) hud_tap(&g_hud_in.ability);
}

static _Atomic int g_ability_charge;     /* 0..1000 */
/* Package manifests stay loaded for the match. Publish a pointer to their
 * immutable label, so Java never reads a buffer while the game writes it. */
static _Atomic(const char *) g_ability_name;

JNIEXPORT jfloat JNICALL
Java_net_hta_halotrial_GameActivity_nativeAbilityCharge(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return (float)atomic_load(&g_ability_charge) / 1000.0f;
}

JNIEXPORT jstring JNICALL
Java_net_hta_halotrial_GameActivity_nativeAbilityName(JNIEnv *env, jclass cls)
{
    (void)cls;
    const char *name = atomic_load(&g_ability_name);
    return (*env)->NewStringUTF(env, name ? name : "");
}

/* FLY: take off or come down, for a character that flies by itself. */
JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudFly(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) hud_tap(&g_hud_in.fly);
}

/* Which HUD buttons mean anything now, for the Java HUD: 1 can fly, 2 in
 * the air, 4 the weapon zooms, 8 it reloads, 16 it is swung, 32 grenades
 * in hand, 64 an ability is ready, 128 an ability exists. */
static _Atomic int g_hud_caps;

JNIEXPORT jint JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudCaps(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return atomic_load(&g_hud_caps);
}

JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudZoom(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) hud_tap(&g_hud_in.zoom);
}
JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudGrenade(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_android) hud_tap(&g_hud_in.grenade);
}
JNIEXPORT void JNICALL
Java_net_hta_halotrial_GameActivity_nativeHudDebug(JNIEnv *env, jclass cls,
                                                   jint action)
{
    (void)env; (void)cls;
    if (g_android) { pthread_mutex_lock(&g_hud_lock); g_hud_in.debug = (int)action + 1; pthread_mutex_unlock(&g_hud_lock); }
}

/* ------------------------------ vehicles ------------------------------ */

/* The HUD that goes with where you sit: a gunner sees the vehicle gun's own
 * crosshair; everyone else the weapon they carry. */
static bool g_hud_vehicle;
static void seat_hud(hta_android *s, const hta_weapon_def *vdef)
{
    char err[HTA_ERRLEN];
    if (!vdef && !g_hud_vehicle) return;
    hta_resource_map *bm = s->bitmaps_ok ? &s->bitmaps_rm : NULL;
    if (s->gpu_hud) { hta_gfx_mesh_free(s->gfx, s->gpu_hud); s->gpu_hud = NULL; }
    hta_hud_free(&s->hud);
    hta_hud_load(&s->hud, &s->cache, bm, vdef ? vdef : &s->weap, err, sizeof(err));
    hta_hud_set_shield(&s->hud, s->vit ? hta_vitals_shield_fraction(s->vit) : 1.0f);
    hta_hud_set_health(&s->hud, s->vit ? hta_vitals_health_fraction(s->vit) : 1.0f);
    if (s->gfx && s->hud.elem_count)
        s->gpu_hud = hta_gfx_mesh_upload_dynamic(s->gfx, &s->hud.mesh, err, sizeof(err));
    g_hud_vehicle = vdef != NULL;
}

/* The Trial's own words for a vehicle and a seat: `hud_icon_messages`
 * says "Warthog", "Ghost", "driver", "gunner", "side". */
static void vehicle_words(hta_android *s, uint32_t car, uint32_t seat, char *out, size_t n)
{
    static uint32_t icons;
    if (!icons) icons = hta_ustr_find(&s->cache, "ui\\hud\\hud_icon_messages");
    const hta_vehicle *v = &s->vehicles.cars[car];
    const hta_vehicle_type *t = &s->vehicles.types[v->type];
    const hta_vehicle_seat *st = hta_vehicles_seat(&s->vehicles, car, seat);
    char name[32] = "", place[32] = "";
    if (!icons || t->hud_name < 0 || !hta_ustr_get(&s->cache, icons, (uint32_t)t->hud_name,
                                                    name, sizeof(name)))
        snprintf(name, sizeof(name), "%s", t->name);
    if (!st || !icons || st->hud_text < 0 ||
        !hta_ustr_get(&s->cache, icons, (uint32_t)st->hud_text, place, sizeof(place)))
        snprintf(place, sizeof(place), "%s", st ? st->label : "");
    /* The rocket Warthog is a Warthog to the HUD; say which. */
    bool rocket = v->kind == HTA_VK_JEEP && t->name[0] == 'r';
    snprintf(out, n, "%s%s %s", rocket ? "Rocket " : "", name, place);
}

/* Where you sit, and what the stick and the look ask of the vehicle.
 * Returns whether this seat lets you shoot what you carry. */
static bool vehicle_controls(hta_android *s, hta_player_input *in)
{
    hta_unit *u = &s->game.units[s->me];
    uint32_t car = (uint32_t)u->vehicle, seat = (uint32_t)u->seat;
    const hta_vehicle_seat *st = hta_vehicles_seat(&s->vehicles, car, seat);
    const hta_vehicle *v = &s->vehicles.cars[car];
    if (!st) return false;
    bool driver = (st->flags & HTA_SEAT_DRIVER) != 0;
    bool gunner = (st->flags & HTA_SEAT_GUNNER) != 0;
    bool armed = (st->flags & HTA_SEAT_ALLOWS_WEAPONS) != 0;
    /* A Warthog steers with the stick, so its driver's look swings with
     * the hull; everything else aims where you look, in the world. */
    if (driver && v->kind == HTA_VK_JEEP) {
        s->seat_look[0] += in->look_yaw;
        s->seat_look[1] += in->look_pitch;
        if (s->seat_look[0] > 2.6f) s->seat_look[0] = 2.6f;
        if (s->seat_look[0] < -2.6f) s->seat_look[0] = -2.6f;
        if (s->seat_look[1] > 0.5f) s->seat_look[1] = 0.5f;
        if (s->seat_look[1] < -0.8f) s->seat_look[1] = -0.8f;
        s->cam.yaw = v->yaw + s->seat_look[0];
        s->cam.pitch = s->seat_look[1];
    } else {
        hta_camera_look(&s->cam, in->look_yaw, in->look_pitch);
        if (s->cam.pitch > 1.2f) s->cam.pitch = 1.2f;
        if (s->cam.pitch < -1.2f) s->cam.pitch = -1.2f;
    }
    hta_vehicles_camera(&s->vehicles, &s->col, car, seat, s->cam.yaw, s->cam.pitch, &s->cam);
    hta_transform root;
    if (hta_game_seat_root(&s->game, s->me, &root))
        for (int k = 0; k < 3; k++) s->player.pos[k] = root.t[k];
    s->player.velocity[0] = cosf(v->yaw) * v->speed + v->lateral_vel[0];
    s->player.velocity[1] = sinf(v->yaw) * v->speed + v->lateral_vel[1];
    s->player.velocity[2] = 0.0f;
    s->player.footstep = s->player.landed = false;
    s->player.on_ground = true;
    if (!s->net_enabled || s->net_hosting) {
        u->in.move.move_forward = driver ? in->move_forward : 0.0f;
        u->in.move.move_right = driver ? in->move_right : 0.0f;
        u->in.move.jump = driver && in->jump;
        u->in.move.fire = gunner && in->fire;
        u->in.fire2 = gunner && s->hud_alt;
        u->in.move.look_yaw = u->in.move.look_pitch = 0.0f;
    }
    s->veh_fire = gunner && in->fire;
    if (!driver) { in->move_forward = in->move_right = 0.0f; }
    if (!armed) in->fire = false;
    in->jump = driver && in->jump;
    in->crouch = false;
    return armed;
}

/* After everything has moved this frame: the camera onto the seat again,
 * and whether our own body is in the picture. */
static void vehicle_camera(hta_android *s)
{
    s->show_self = false;
    if (!s->game_on || s->me < 0 || s->dead) return;
    hta_unit *u = &s->game.units[s->me];
    if (u->vehicle < 0 || !s->vehicles.loaded) return;
    uint32_t car = (uint32_t)u->vehicle, seat = (uint32_t)u->seat;
    const hta_vehicle *v = &s->vehicles.cars[car];
    const hta_vehicle_seat *st = hta_vehicles_seat(&s->vehicles, car, seat);
    if (st && (st->flags & HTA_SEAT_DRIVER) && v->kind == HTA_VK_JEEP)
        s->cam.yaw = v->yaw + s->seat_look[0];
    hta_vehicles_camera(&s->vehicles, &s->col, car, seat, s->cam.yaw, s->cam.pitch, &s->cam);
    hta_transform root;
    if (hta_game_seat_root(&s->game, s->me, &root))
        for (int k = 0; k < 3; k++) s->player.pos[k] = root.t[k];
    s->show_self = hta_vehicles_third_person(&s->vehicles, car, seat) &&
                   !(v->kind == HTA_VK_TANK && st && (st->flags & HTA_SEAT_DRIVER));
}

/* On a broom: out of your body and behind it, the way a Banshee's camera
 * rides, kept out of walls. Shots still leave from your eyes. Ours. */
#define BROOM_CAM_BACK  1.25f
#define BROOM_CAM_UP    0.28f
#define BROOM_CAM_RIGHT 0.28f   /* over the shoulder: you do not hide the aim */
static void broom_camera(hta_android *s)
{
    if (!s->player.fly || s->dead) return;
    s->show_self = true;
    /* Superman and Goku need room in frame at titan flight speeds; the
     * broom keeps its closer shoulder view. */
    /* Far enough back, and low enough, that the head stays in frame. */
    float back = s->power_fly ? 2.9f : BROOM_CAM_BACK;
    float up = s->power_fly ? 0.08f : BROOM_CAM_UP;
    float shoulder = s->power_fly ? 0.62f : BROOM_CAM_RIGHT;
    float fwd[3];
    hta_camera_forward(&s->cam, fwd);
    float eye[3] = { s->cam.pos[0], s->cam.pos[1], s->cam.pos[2] };
    float right[3];
    hta_camera_right(&s->cam, right);
    float d[3] = { -fwd[0] * back + right[0] * shoulder,
                   -fwd[1] * back + right[1] * shoulder,
                   -fwd[2] * back + up };
    float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (len < 1e-4f) return;
    for (int k = 0; k < 3; k++) d[k] /= len;
    float t = 0.0f, hit[3], nrm[3];
    if (s->col.built && hta_collision_ray(&s->col, eye, d, len, &t, hit, nrm)) len = t * 0.85f;
    for (int k = 0; k < 3; k++) s->cam.pos[k] = eye[k] + d[k] * len;
}

/* Engines: each running vehicle's own looping sound, from where it is,
 * faster as it goes faster. Idle below the rate. */
#define HTA_LOOP_ENGINE 40u
static void vehicle_sounds(hta_android *s)
{
    if (!s->vehicles.loaded || !s->audio_ok) return;
    for (uint32_t i = 0; i < s->vehicles.count && i < HTA_VEHICLE_MAX; i++) {
        const hta_vehicle *v = &s->vehicles.cars[i];
        uint32_t snd = v->type < HTA_VEHICLE_TYPES ? s->veh_engine[v->type] : 0;
        float d[3] = { v->pos[0]-s->cam.pos[0], v->pos[1]-s->cam.pos[1], v->pos[2]-s->cam.pos[2] };
        float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        bool on = snd && v->active && v->ctl.driven && dist < HTA_SOUND_FAR * 0.6f;
        if (!on) {
            if (s->veh_engine_on[i]) hta_audio_loop_stop(&s->audio, HTA_LOOP_ENGINE + i);
            s->veh_engine_on[i] = false;
            continue;
        }
        int b = bank_get(s, snd);
        if (b < 0) continue;
        float speed = hta_vehicles_speed(&s->vehicles, i);
        float frac = v->forward > 0.1f ? speed / v->forward : 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        float g = s->veh_engine_gain[v->type] * (0.55f + 0.45f * frac);
        if (dist > HTA_SOUND_NEAR) g *= (HTA_SOUND_NEAR / dist) * (1.0f - dist / (HTA_SOUND_FAR * 0.6f));
        float pan = 0.0f;
        if (dist > 0.01f) {
            float right[3];
            hta_camera_right(&s->cam, right);
            pan = (d[0]*right[0] + d[1]*right[1] + d[2]*right[2]) / dist;
        }
        /* Revs: idle as recorded, up to half again at full speed. Ours. */
        hta_audio_loop_ex(&s->audio, HTA_LOOP_ENGINE + i, s->bank[b].clip[0], g, pan,
                          0.85f + 0.5f * frac);
        s->veh_engine_on[i] = true;
    }
}

/* Harry on a broom. Loud beside him, still a tune when he is a speck.
 * One loop, the nearest flyer. Power flight is not a broom. */
#define HTA_LOOP_FLIGHT 120u
#define FLIGHT_HEAR     80.0f
static void flight_music(hta_android *s)
{
    if (!s->audio_ok || s->flight_clip == HTA_AUDIO_NO_CLIP || !s->game_on) {
        if (s->audio_ok) hta_audio_loop_stop(&s->audio, HTA_LOOP_FLIGHT);
        return;
    }
    int best = -1;
    float best_d = FLIGHT_HEAR;
    for (uint32_t i = 0; i < s->game.unit_count; i++) {
        const hta_unit *u = &s->game.units[i];
        if (!u->alive || !u->riding || u->flying) continue;
        if (u->character < 0 || (uint32_t)u->character >= s->game.character_count) continue;
        const hta_oal_asset *hero = s->game.characters[u->character];
        if (!hero || strcmp(hero->name, "harry")) continue;
        float d[3] = { u->body.pos[0] - s->cam.pos[0], u->body.pos[1] - s->cam.pos[1],
                       u->body.pos[2] - s->cam.pos[2] };
        float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (dist < best_d) { best_d = dist; best = (int)i; }
    }
    if (best < 0) { hta_audio_loop_stop(&s->audio, HTA_LOOP_FLIGHT); return; }
    const hta_unit *u = &s->game.units[best];
    float d[3] = { u->body.pos[0] - s->cam.pos[0], u->body.pos[1] - s->cam.pos[1],
                   u->body.pos[2] + 0.6f - s->cam.pos[2] };
    float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    float g = dist < 10.0f ? 1.35f : 1.35f * (10.0f / dist) * (1.0f - dist / FLIGHT_HEAR);
    if (g < 0.08f) g = 0.08f;
    if ((int32_t)best == s->me) g = 1.55f;
    float pan = 0.0f;
    if (dist > 0.05f) {
        float right[3];
        hta_camera_right(&s->cam, right);
        pan = (d[0]*right[0] + d[1]*right[1] + d[2]*right[2]) / dist;
        if (pan > 1.0f) pan = 1.0f;
        if (pan < -1.0f) pan = -1.0f;
    }
    hta_audio_loop_ex(&s->audio, HTA_LOOP_FLIGHT, s->flight_clip, g, pan, 1.0f);
}

/* In, out, or across: what changes for this device when its seat does. */
static void vehicle_transition(hta_android *s)
{
    if (!s->game_on || s->me < 0 || s->me >= (int32_t)s->game.unit_count) return;
    hta_unit *u = &s->game.units[s->me];
    int32_t car = u->alive && !s->dead ? u->vehicle : -1;
    int32_t seat = car >= 0 ? u->seat : -1;
    if (car == s->my_car && seat == s->my_seat) return;
    bool was = s->my_car >= 0;
    s->my_car = car;
    s->my_seat = seat;
    if (car >= 0) {
        s->zoom_level = 0;
        apply_zoom(s);
        s->hud_fire = false;
        s->throwing = false;
        fire_loop(s, false);
        vm_play(s, HTA_VM_IDLE);
        const hta_vehicle *v = &s->vehicles.cars[car];
        if (!was) {
            s->seat_look[0] = 0.0f;
            s->seat_look[1] = -0.15f;
            s->cam.yaw = v->yaw;
            s->cam.pitch = -0.1f;
        }
        const hta_vehicle_seat *st = hta_vehicles_seat(&s->vehicles, (uint32_t)car, (uint32_t)seat);
        int32_t wi = st && (st->flags & HTA_SEAT_GUNNER) && v->type < HTA_VEHICLE_TYPES
                   ? s->game.vweapon[v->type][0] : -1;
        seat_hud(s, wi >= 0 ? &s->game.weapons[wi].def : NULL);
        char words[64];
        vehicle_words(s, (uint32_t)car, (uint32_t)seat, words, sizeof(words));
        hta_log("[vehicles] in %s (car %d seat %d)", words, car, seat);
    } else {
        /* Out: stand where the host put us, facing the way we looked. */
        if (!s->net_enabled || s->net_hosting) {
            for (int k = 0; k < 3; k++) s->player.pos[k] = u->body.pos[k];
            for (int k = 0; k < 3; k++) s->player.velocity[k] = u->body.velocity[k];
            s->player.on_ground = u->body.on_ground;
        }
        s->player.landed = false;
        s->player.crouch_t = 0.0f;
        s->player.eye_height = s->player.phys.cam_stand;
        for (int k = 0; k < 3; k++) s->cam.pos[k] = s->player.pos[k];
        s->cam.pos[2] += s->player.eye_height;
        if (s->cam.pitch > 1.2f || s->cam.pitch < -1.2f) s->cam.pitch = 0.0f;
        /* A held BRAKE is not a jump on the way out. */
        s->hud_jump = s->jump_held = false;
        s->hud_alt = false;
        seat_hud(s, NULL);
        hta_log("[vehicles] out at (%.2f %.2f %.2f)", s->player.pos[0], s->player.pos[1],
                s->player.pos[2]);
    }
}

/* What the HUD tells you about vehicles: a free seat in reach, or the
 * seat you are in. */
static void vehicle_status(hta_android *s, bool seated, int32_t near_car, int32_t near_seat)
{
    int mode = 0;
    char text[96] = "";
    if (seated) {
        const hta_unit *u = &s->game.units[s->me];
        const hta_vehicle *v = &s->vehicles.cars[u->vehicle];
        const hta_vehicle_seat *st = hta_vehicles_seat(&s->vehicles, (uint32_t)u->vehicle,
                                                       (uint32_t)u->seat);
        uint32_t f = st ? st->flags : 0;
        mode = (f & HTA_SEAT_DRIVER) ? 2 : (f & HTA_SEAT_GUNNER) ? 3 :
               (f & HTA_SEAT_ALLOWS_WEAPONS) ? 4 : 5;
        if ((f & HTA_SEAT_GUNNER) && v->type < HTA_VEHICLE_TYPES &&
            s->game.vweapon[v->type][1] >= 0) mode |= 16;
        char words[64];
        vehicle_words(s, (uint32_t)u->vehicle, (uint32_t)u->seat, words, sizeof(words));
        snprintf(text, sizeof(text), "%s", words);
    } else if (near_car >= 0 && near_seat >= 0) {
        mode = 1;
        char words[64];
        vehicle_words(s, (uint32_t)near_car, (uint32_t)near_seat, words, sizeof(words));
        snprintf(text, sizeof(text), "GET IN: %s", words);
    }
    snprintf(g_vehicle_text, sizeof(g_vehicle_text), "%s", text);
    atomic_store(&g_vehicle_mode, mode);
}

void android_main(struct android_app *app)
{
    static hta_android state;
    memset(&state, 0, sizeof(state));
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) state.peer_unit[i]=-1;
    state.app = app;
    android_fs_init(&state);
    state.vit = &state.vitals;
    state.move_pointer = state.look_pointer = -1;
    for (unsigned k = 0; k < HTA_CARRY_MAX; k++) state.held_asset[k] = state.start_asset[k] = -1;
    state.ivm_weapon = -1;
    state.ding_clip = state.kill_clip = state.freeze_clip = state.snap_clip = HTA_AUDIO_NO_CLIP;
    state.flight_clip = HTA_AUDIO_NO_CLIP;
    state.killcam_unit = -1;
    state.prev_char = state.prev_weap = state.prev_roster = -2;
    state.next_character = -2;
    g_android = &state;
    state.started_at = hta_time_seconds();
    crash_guard_install(app->activity->externalDataPath);
    state.hud_ready = g_hud_wanted;
    char net_host[64]; bool net_hosting=false;
    read_net_host(app,net_host,&net_hosting);
    /* The setup screen asks for the menu first; a LAN launch goes straight in. */
    state.menu_mode = intent_int(app, "menu", 0) != 0 && !net_host[0];
    state.explore_external = intent_int(app, "explore_external", 0) != 0;
    if (state.explore_external) state.menu_mode = false;
    atomic_store(&g_paused, 0);
    atomic_store(&g_damage_flash, 0);
    atomic_store(&g_net_status, 0);
    atomic_store(&g_menu_mode, state.menu_mode ? 1 : 0);
    /* Three bots at normal unless the setup screen says otherwise. Ours. */
    state.bot_count = intent_int(app, "bots", 3);
    state.bot_skill = intent_int(app, "skill", 1);
    if (state.bot_count < 0) state.bot_count = 0;
    if (state.bot_count > 7) state.bot_count = 7;
    if (state.bot_skill < 0) state.bot_skill = 0;
    if (state.bot_skill > 3) state.bot_skill = 3;
    state.score_limit = HTA_SLAYER_SCORE_LIMIT;
    state.respawn_delay = HTA_RESPAWN_DELAY;
    atomic_store(&g_shell_screen, 0);
    atomic_store(&g_match_ready, 0);
    /* The setup screen's own LAN buttons, for a build with no ui.map. */
    if (net_host[0]) net_begin(&state, net_host, net_hosting, 32270, NULL);

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
    state.my_car = state.my_seat = -1;
    state.vehicle_roster = intent_int(app, "vehicles", HTA_VROSTER_ALL);
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

        if (state.menu_mode) {
            if (!state.menu_go) { menu_frame(&state, dt); continue; }
            state.menu_go = false;
            menu_leave(&state);
            state.last_time = hta_time_seconds();   /* the load is not a frame */
            continue;
        }

        if (state.explore_external) {
            hta_input raw;
            hta_player_input walk;
            android_read_input(&state, &raw, dt);
            hta_session_input(&state.session, &raw, &walk);
            if (atomic_load(&g_paused)) {
                memset(&walk, 0, sizeof(walk));
                dt = 0.0f;
            }
            if (state.map_loaded && state.col.built) {
                hta_player_update(&state.player, &state.cam, &state.col, &walk,
                                  dt > 0.05f ? 0.05f : dt);
                snprintf(g_debug_text, sizeof(g_debug_text),
                         "external map  %.2f %.2f %.2f  %s",
                         state.player.pos[0], state.player.pos[1], state.player.pos[2],
                         state.player.on_ground ? "ground" : "air");
            } else snprintf(g_debug_text, sizeof(g_debug_text), "%s", state.status);
            if (state.gfx) {
                if (!hta_gfx_draw(state.gfx, &state.cam, &state.scene, state.gpu_mesh,
                                  NULL, NULL, NULL, 0, NULL, NULL)) {
                    hta_log("[external] surface lost; rebuilding renderer");
                    stop_gfx(&state);
                    if (app->window) start_gfx(&state);
                }
                state.frames++;
            }
            continue;
        }

        hta_input raw;
        hta_player_input in;
        android_read_input(&state, &raw, dt);
        hta_session_input(&state.session, &raw, &in);
        if (atomic_load(&g_paused)) {
            /* Solo holds the world. A live LAN match keeps simulating while
             * this player's input is blank, like an online pause menu. */
            memset(&in, 0, sizeof(in));
            if (!state.net_enabled) dt = 0.0f;
            state.hud_swap = state.hud_zoom = state.hud_melee = false;
            state.hud_reload = state.hud_grenade = false;
            state.hud_debug = 0;
            fire_loop(&state, false);
        }
        /* A corpse does not steer, shoot or jump. The body still falls --
         * hta_player_update with a blank input keeps gravity and the ground
         * query -- so dying on a slope still slides you down it. */
        if (state.dead) memset(&in, 0, sizeof(in));

        /* The flag: the game puts it in this player's hands, and takes it
         * away on a capture, a death or a drop. The gun goes to the belt
         * with what was in it, and the flag's own first-person model comes
         * up; nothing but a swing works until it is gone, and the swap
         * button puts it down. */
        if (state.game_on && state.me >= 0) {
            int8_t fl = state.game.units[state.me].flag;
            if ((fl >= 0) != (state.carried_flag >= 0) && state.game.flag_weapon >= 0) {
                if (fl >= 0) {
                    state.held_ammo[state.held_slot] = state.ammo;
                    state.held_ammo_set[state.held_slot] = true;
                    fire_loop(&state, false);
                    state.zoom_level = 0;
                    apply_zoom(&state);
                    equip_weapon(&state, state.game.weapons[state.game.flag_weapon].tag);
                } else if (state.held_count) {
                    equip_weapon(&state, state.held[state.held_slot]);
                    if (state.held_ammo_set[state.held_slot]) {
                        state.ammo = state.held_ammo[state.held_slot];
                        hta_ammo_cancel_reload(&state.ammo);
                    }
                }
            }
            state.carried_flag = fl;
            if (fl >= 0) {
                if (state.hud_swap && !state.dead) {
                    if (state.net_enabled && !state.net_hosting) state.net_pickup_count++;
                    else hta_game_drop_flag(&state.game, state.me);
                }
                state.hud_swap = state.hud_zoom = false;
                state.hud_reload = state.hud_grenade = false;
                in.fire = false;
            }
        }

        /* Vehicles: get in when a free seat is in reach, out when seated.
         * The game decides -- on a client, the host does. */
        hta_unit *mine = state.game_on && state.me >= 0 &&
                         state.me < (int32_t)state.game.unit_count
                       ? &state.game.units[state.me] : NULL;
        bool seated = mine && mine->vehicle >= 0 && mine->alive && !state.dead &&
                      state.vehicles.loaded;
        int32_t near_seat = -1;
        int32_t near_car = mine && !seated && !state.dead && state.vehicles.loaded
                         ? hta_game_seat_near(&state.game, state.me, &near_seat) : -1;
        if (!state.dead && state.hud_swap && (seated || near_car >= 0)) {
            state.hud_swap = false;
            if (state.net_enabled && !state.net_hosting) state.net_action_count++;
            else mine->in.action = true;
        }
        bool armed_seat = true;
        state.veh_fire = false;
        if (seated) {
            armed_seat = vehicle_controls(&state, &in);
            state.hud_swap = state.hud_melee = state.hud_grenade = false;
            state.hud_debug = 0;
            if (!armed_seat) state.hud_zoom = state.hud_reload = false;
        } else {
            /* A broom in hand: you fly (hta_player.fly), seated on it. */
            const hta_game_weapon *mount = held_imported(&state);
            bool broom = mount && mount->mount && !state.dead && state.game_on;
            hta_body_attr body = hta_game_body(&state.game, state.me);
            if (state.hud_fly) {
                state.hud_fly = false;
                if (body.can_fly && !broom && !state.dead) state.power_fly = !state.power_fly;
            }
            if (!body.can_fly || state.dead) state.power_fly = false;
            if (state.game_on && state.me >= 0 && state.game.units[state.me].stagger > 0.0f)
                state.power_fly = false;
            bool knocked = state.game_on && state.me >= 0 && state.game.units[state.me].stagger > 0.0f;
            bool riding = (broom && !knocked) || state.power_fly;
            state.player.fly = riding;
            state.player.fly_speed = broom ? mount->asset->fly_speed : riding ? body.fly_speed : 0.0f;
            if (state.game_on && state.me >= 0) {
                state.game.units[state.me].riding = riding;
                state.game.units[state.me].flying = state.power_fly;
            }
            {
                /* What the HUD should offer (nativeHudCaps). */
                const hta_game_weapon *hw = held_imported(&state);
                bool swung = hw && hw->melee_only;
                /* The ability: fire it on its button, show how ready. */
                float charge = state.game_on ? hta_game_ability_charge(&state.game, state.me) : -1.0f;
                if (state.net_enabled && !state.net_hosting && state.me>=0 && state.game_on) {
                    hta_unit *mu=&state.game.units[state.me];
                    if(mu->ability_cool>0.0f) mu->ability_cool-=dt;
                }
                if (state.hud_ability) {
                    state.hud_ability = false;
                    if (charge >= 1.0f && !state.dead) {
                        if (!state.net_enabled || state.net_hosting) hta_game_ability(&state.game,state.me);
                        else {
                            state.net_ability_count++;
                            hta_unit *mu=&state.game.units[state.me];
                            const hta_oal_asset *hero=state.game.characters[mu->character];
                            mu->ability_cool=hta_game_ability_cooldown(&state.game,state.me) +
                                fmaxf(0.0f,fminf(hero->ability_duration,4.0f));
                        }
                    }
                }
                if (charge >= 0.0f) {
                    atomic_store(&g_ability_charge, (int)(charge * 1000.0f));
                    const hta_unit *mu = &state.game.units[state.me];
                    atomic_store(&g_ability_name, state.game.characters[mu->character]->ability_name);
                } else {
                    atomic_store(&g_ability_charge, 0);
                    atomic_store(&g_ability_name, NULL);
                }
                /* A blow's push, onto your own body. */
                if (state.game_on && state.me >= 0) {
                    hta_unit *mu = &state.game.units[state.me];
                    if (mu->knock[0] != 0.0f || mu->knock[1] != 0.0f || mu->knock[2] != 0.0f) {
                        float kx = mu->knock[0], ky = mu->knock[1], kz = mu->knock[2];
                        for (int k = 0; k < 3; k++) { state.player.velocity[k] += mu->knock[k]; mu->knock[k] = 0.0f; }
                        state.player.on_ground = false;
                        if (kx * kx + ky * ky + kz * kz > 16.0f) {
                            state.power_fly = false;
                            state.player.fly = false;
                            if (mu->stagger < 0.75f) mu->stagger = 0.75f;
                        }
                    }
                }
                int caps = (charge >= 0.0f ? 128 : 0) | (charge >= 1.0f ? 64 : 0) |
                           (body.can_fly && !broom ? 1 : 0) | (state.player.fly ? 2 : 0) |
                           (state.weap.zoom_levels > 0 ? 4 : 0) |
                           (!swung && state.ammo.recharge <= 0.0f && state.ammo.reserve_max > 0 ? 8 : 0) |
                           (swung ? 16 : 0) | (state.nade_count > 0 ? 32 : 0);
                atomic_store(&g_hud_caps, caps);
            }
            if (state.dead) {
                hta_player_corpse_update(&state.player,state.col.built ? &state.col : NULL,state.player.gravity,dt);
                for (int k=0;k<3;k++) state.cam.pos[k]=state.player.pos[k];
                state.cam.pos[2]+=state.player.eye_height;
            } else hta_player_update(&state.player, &state.cam,
                state.col.built ? &state.col : NULL, &in, dt);
        }
        vehicle_status(&state, seated, near_car, near_seat);
        bool driving = seated;
        /* Everyone else sees, aims at and is hit by where you are now. */
        if (state.game_on) {
            hta_game_sync_local(&state.game, &state.player, &state.cam, held_roster(&state));
            if (!state.net_enabled || state.net_hosting) mirror_local(&state);
        }
        if (state.player.footstep && state.col.built) {
            uint8_t mat = hta_collision_ground_material(&state.col,
                                                        state.player.pos[0],
                                                        state.player.pos[1],
                                                        state.player.pos[2] + 0.1f);
            play_footstep(&state, mat);
        }
        hta_gun_update(&state.gun, dt);

        /* What the fall cost, and the shield growing back afterwards. */
        if (state.player.landed && state.vit->loaded &&
            (!state.net_enabled || state.net_hosting)) {
            float cost = hta_vitals_land(state.vit, state.player.land_speed);
            if (cost > 0.0f)
                hta_log("[player] landed at %.1f wu/s for %.0f damage "
                        "(%.0f shield, %.0f health left)",
                        state.player.land_speed, cost,
                        state.vit->shield, state.vit->health);
        }
        /* The one-shots have to be read BEFORE the update clears them. */
        if (state.vit->loaded && !state.dead) {
            if (state.vit->took_damage) state.damage_flash_left = HTA_DAMAGE_FLASH_TIME;
            if (state.vit->shield_broke)
                play_tag(&state, state.shield_empty_snd, 1.0f);
            else if (state.vit->took_damage)
                play_tag(&state, state.shield_hit_snd, 1.0f);
        }
        if (!state.net_enabled || state.net_hosting || !state.net.have_world)
            hta_vitals_update(state.vit, dt);
        else {
            state.vit->took_damage=false;
            state.vit->shield_broke=false;
        }
        if (state.damage_flash_left > 0.0f) {
            state.damage_flash_left -= dt;
            if (state.damage_flash_left < 0.0f) state.damage_flash_left = 0.0f;
        }
        atomic_store(&g_damage_flash, (int)(255.0f * state.damage_flash_left / HTA_DAMAGE_FLASH_TIME));
        if (state.game_on && state.me >= 0) {
            hta_game_contact con[HTA_HUD_MAX_BLIPS];
            hta_hud_blip blips[HTA_HUD_MAX_BLIPS];
            uint32_t nc = state.dead ? 0 : hta_game_sensor(&state.game, state.me, con, HTA_HUD_MAX_BLIPS);
            for (uint32_t i = 0; i < nc; i++) {
                blips[i].x = con[i].x; blips[i].y = con[i].y;
                blips[i].friendly = con[i].friendly;
                blips[i].size = con[i].vehicle ? 1.8f : 1.0f;
            }
            hta_hud_set_blips(&state.hud, blips, nc);
            /* The red reticle: an enemy under the crosshair, or inside the
             * weapon's own autoaim cone and range. A gunner's is the
             * vehicle gun's. */
            int32_t aimw = held_roster(&state);
            if (state.my_car >= 0 && (uint32_t)state.my_car < state.vehicles.count) {
                const hta_vehicle_seat *st = hta_vehicles_seat(&state.vehicles,
                    (uint32_t)state.my_car, (uint32_t)state.my_seat);
                uint16_t ty = state.vehicles.cars[state.my_car].type;
                aimw = st && (st->flags & HTA_SEAT_GUNNER) && ty < HTA_VEHICLE_TYPES
                     ? state.game.vweapon[ty][0]
                     : (st && (st->flags & HTA_SEAT_ALLOWS_WEAPONS) ? aimw : -1);
            }
            float fwd[3];
            hta_camera_forward(&state.cam, fwd);
            bool on = !state.dead && aimw >= 0 &&
                hta_game_aim_target(&state.game, state.me, aimw, state.cam.pos, fwd, NULL) >= 0;
            if (on != state.hud.cross_on_target) hta_hud_set_on_target(&state.hud, on);
        }
        if (state.vit->loaded) {
            hta_hud_set_shield(&state.hud, hta_vitals_shield_fraction(state.vit));
            hta_hud_set_health(&state.hud, hta_vitals_health_fraction(state.vit));

            /* The recharge hum. Its condition is the tag's own and nothing
             * of ours: the shield is growing back exactly when the delay
             * since the last hit has elapsed and it is not yet full. */
            float sf = hta_vitals_shield_fraction(state.vit);
            float hf = hta_vitals_health_fraction(state.vit);
            bool charging = !state.dead &&
                            state.vit->since_damage >= state.vit->recharge_delay &&
                            state.vit->shield < state.vit->max_shield;
            hud_loop(&state, HTA_LOOP_SHIELD_CHARGE, state.shield_charge_snd,
                     charging, &state.shield_charge_on);
            hud_loop(&state, HTA_LOOP_SHIELD_LOW, state.shield_low_snd,
                     !state.dead && !charging && sf <= HTA_VITALS_LOW && sf > 0.0f,
                     &state.shield_low_on);
            hud_loop(&state, HTA_LOOP_HEALTH_LOW, state.health_low_snd,
                     !state.dead && hf <= HTA_VITALS_LOW,
                     &state.health_low_on);
        }

        /* Dying, and coming back. */
        if (state.vit->loaded && state.vit->died && !state.dead) {
            state.dead = true;
            atomic_store(&g_vehicle_mode, 0);
            state.dead_timer = state.respawn_delay;
            for (int k = 0; k < 3; k++) state.death_pos[k] = state.player.pos[k];
            fire_loop(&state, false);
            hud_loop(&state, HTA_LOOP_SHIELD_CHARGE, state.shield_charge_snd,
                     false, &state.shield_charge_on);
            hud_loop(&state, HTA_LOOP_SHIELD_LOW, state.shield_low_snd,
                     false, &state.shield_low_on);
            hud_loop(&state, HTA_LOOP_HEALTH_LOW, state.health_low_snd,
                     false, &state.health_low_on);
            state.zoom_level = 0;
            apply_zoom(&state);
            /* A fall is its own kind of death and the tag has a line for
             * it; a blast is violent; anything else is the quiet one. */
            uint32_t snd = state.death_quiet_snd;
            if (state.player.landed && state.player.land_speed >= state.vit->fall_fatal)
                snd = state.death_falling_snd ? state.death_falling_snd
                                              : state.death_violent_snd;
            else if (state.vit->shield <= 0.0f && state.vit->health <= 0.0f)
                snd = state.death_violent_snd ? state.death_violent_snd : snd;
            play_tag(&state, snd, 1.0f);
            /* The body stays where it fell and the camera goes to look at
             * it. Halo does this and it is the whole reason dying reads as
             * an event rather than a fade. */
            if (state.corpse.loaded &&
                hta_actor_play_death(&state.corpse, &state.spawn_rng)) {
                state.corpse_up = true;
                hta_actor_place(&state.corpse, state.death_pos, state.cam.yaw);
                hta_log("[player] body playing '%s'",
                        state.corpse.graph.anims[state.corpse.clip].name);
            }
            hta_log("[player] died at (%.2f %.2f %.2f)",
                    state.death_pos[0], state.death_pos[1], state.death_pos[2]);
        }
        float fade = 0.0f;
        class_poll(&state);
        if (state.dead) {
            state.dead_timer -= dt;
            /* Clear while you watch; black only over the last moment. */
            if (state.dead_timer < HTA_DEATH_FADE_OUT) {
                fade = 1.0f - state.dead_timer / HTA_DEATH_FADE_OUT;
                if (fade > 1.0f) fade = 1.0f;
                if (fade < 0.0f) fade = 0.0f;
            }
            if (state.dead_timer <= 0.0f &&
                (!state.net_enabled || state.net_hosting)) {
                respawn(&state);
                state.dead = false;
                state.dead_timer = -HTA_DEATH_FADE_IN;   /* counts the fade back */
            }
        } else if (state.dead_timer < 0.0f) {
            state.dead_timer += dt;
            if (state.dead_timer > 0.0f) state.dead_timer = 0.0f;
            fade = -state.dead_timer / HTA_DEATH_FADE_IN;
            if (fade < 0.0f) fade = 0.0f;
        }
        hta_hud_set_fade(&state.hud, fade);
        /* Outside yourself, watching the body. */
        if (state.dead && state.me >= 0 && state.me < (int32_t)state.game.unit_count &&
            state.game.units[state.me].gibbed)
            state.corpse_up = false;
        if (state.dead) {
            /* Still travelling: the camera stays on the body instead of the
             * spot the hit landed. */
            float spd = fabsf(state.player.velocity[2]) +
                        hypotf(state.player.velocity[0], state.player.velocity[1]);
            if (!state.player.on_ground || spd > 0.6f)
                for (int k = 0; k < 3; k++) state.death_pos[k] = state.player.pos[k];
        }
        if (state.dead && state.corpse_up) {
            hta_actor_update(&state.corpse, dt);
            hta_actor_place(&state.corpse, state.death_pos, state.corpse.yaw);

            float gone = state.respawn_delay - state.dead_timer;
            float f = gone / HTA_DEATH_PULLBACK;
            if (f > 1.0f) f = 1.0f;

            /* Aim at the chest rather than the feet, and pull back along
             * the way the body is facing so you see its front. */
            float look[3] = { state.death_pos[0], state.death_pos[1],
                              state.death_pos[2] + HTA_DEATH_LOOK_AT };
            float back = HTA_DEATH_CAM_BACK * f;
            float want[3] = {
                look[0] + cosf(state.corpse.yaw) * back,
                look[1] + sinf(state.corpse.yaw) * back,
                look[2] + HTA_DEATH_CAM_UP * f
            };
            /* Do not go through a wall to get there. The ray query is a
             * grid walk now, so this costs nothing. */
            if (state.col.built) {
                float d[3] = { want[0]-look[0], want[1]-look[1], want[2]-look[2] };
                float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
                if (len > 1e-4f) {
                    for (int k = 0; k < 3; k++) d[k] /= len;
                    float t = 0.0f, hit[3], nrm[3];
                    if (hta_collision_ray(&state.col, look, d, len, &t, hit, nrm)) {
                        float keep = t * 0.8f;   /* stop short of the surface */
                        for (int k = 0; k < 3; k++) want[k] = look[k] + d[k] * keep;
                    }
                }
            }
            for (int k = 0; k < 3; k++) state.cam.pos[k] = want[k];

            float to[3] = { look[0]-state.cam.pos[0], look[1]-state.cam.pos[1],
                            look[2]-state.cam.pos[2] };
            float flat = sqrtf(to[0]*to[0] + to[1]*to[1]);
            if (flat > 1e-4f || fabsf(to[2]) > 1e-4f) {
                state.cam.yaw = atan2f(to[1], to[0]);
                state.cam.pitch = atan2f(to[2], flat);
            }
        } else if (state.dead) {
            /* No body to watch -- sink and tip forward instead. */
            float gone = state.respawn_delay - state.dead_timer;
            float f = gone / HTA_DEATH_PULLBACK;
            if (f > 1.0f) f = 1.0f;
            state.cam.pos[2] -= HTA_DEATH_EYE_DROP * f;
            float want = -1.2f;
            state.cam.pitch += (want - state.cam.pitch) * f;
        }
        /* Then, if someone did it, the swoop to them and the still frame. */
        if (state.dead) killcam(&state, dt);
        /* Ammo gates the shot: hta_gun_fire spends the cooldown whether or
         * not the magazine could pay, so ask before pulling. */
        hta_ammo_update(&state.ammo, dt);
        /* A shell-at-a-time reload chains on its own, and every shell after
         * the first was being loaded silently with no animation -- which is
         * most of a shotgun reload from empty. Each one replays the clip. */
        if (state.ammo.reload_began && (state.vm.loaded || held_imported(&state)))
            vm_play(&state, HTA_VM_RELOAD);
        if (state.dry_cooldown > 0.0f) state.dry_cooldown -= dt;

        /* What you are standing on.
         *
         * Halo takes grenades, health and powerups as you walk over them
         * and makes you ASK for a weapon, which is the difference between
         * topping up and losing the gun you wanted. SWAP is that ask: on an
         * item it picks it up, and off one it cycles as before. */
        if (state.items.loaded && (!state.net_enabled || state.net_hosting))
            hta_pickups_update(&state.items, dt);
        if (state.items.loaded && state.net_enabled && !state.net_hosting &&
            state.hud_swap && !state.dead) {
            int32_t slot=hta_pickups_at_kind(&state.items,state.player.pos,HTA_ITEM_WEAPON);
            const hta_item_choice *item=hta_pickups_item(&state.items,slot);
            bool carrying=false;
            for (uint32_t i=0;item && i<state.held_count;i++)
                if (state.held[i]==item->tag_id) carrying=true;
            /* Or one somebody dropped: the host decides. */
            int32_t dr=state.game_on ? hta_game_drop_near(&state.game,state.player.pos,HTA_DROP_REACH) : -1;
            bool drop_new=false;
            if (dr>=0) {
                drop_new=true;
                for (uint32_t i=0;i<state.held_count;i++)
                    if (hta_game_weapon_index(&state.game,state.held[i])==state.game.drops[dr].weapon)
                        drop_new=false;
            }
            if ((item && !carrying) || drop_new) {
                state.net_pickup_count++;
                state.hud_swap=false;
            }
        }
        if (state.items.loaded && (!state.net_enabled || state.net_hosting) &&
            !state.dead && !driving) {
            const float *feet = state.player.pos;

            int32_t got = hta_pickups_at(&state.items, feet);
            const hta_item_choice *item = hta_pickups_item(&state.items, got);
            if (item) {
                bool taken = false;
                switch (item->kind) {
                case HTA_ITEM_GRENADE:
                    if (state.nade_count < state.nade_max) {
                        state.nade_count++;
                        taken = true;
                    }
                    break;
                case HTA_ITEM_HEALTH:
                    if (state.vit->loaded &&
                        state.vit->health < state.vit->max_health) {
                        state.vit->health = state.vit->max_health;
                        taken = true;
                    }
                    break;
                case HTA_ITEM_OVERSHIELD:
                    if (state.vit->loaded) {
                        state.vit->shield = state.vit->max_shield *
                                              HTA_OVERSHIELD_MULT;
                        state.powerup = HTA_ITEM_OVERSHIELD;
                        state.powerup_timer = item->powerup_time;
                        taken = true;
                    }
                    break;
                case HTA_ITEM_CAMOUFLAGE:
                    state.powerup = HTA_ITEM_CAMOUFLAGE;
                    state.powerup_timer = item->powerup_time;
                    taken = true;
                    break;
                default:
                    break;   /* a weapon waits to be asked for */
                }
                if (taken) {
                    play_tag(&state, item->pickup_snd, 1.0f);
                    item_message(&state, item->tag_id, 0);
                    hta_log("[items] picked up %s", item->path);
                    hta_pickups_take(&state.items, got);
                }
            }

            /* A dropped weapon: its ammunition if we carry one like it,
             * and the gun itself -- with what was left in it -- on SWAP. */
            if (state.game_on) {
                int32_t dr = hta_game_drop_near(&state.game, feet, HTA_DROP_REACH);
                if (dr >= 0) {
                    hta_game_drop *d = &state.game.drops[dr];
                    int32_t in_hand = held_roster(&state);
                    bool carried = false;
                    for (uint32_t i = 0; i < state.held_count; i++)
                        if (hta_game_weapon_index(&state.game, state.held[i]) == d->weapon) carried = true;
                    if (carried && d->weapon == in_hand && state.ammo.reserve < state.ammo.reserve_max) {
                        state.ammo.reserve += d->ammo.loaded + d->ammo.reserve;
                        if (state.ammo.reserve > state.ammo.reserve_max)
                            state.ammo.reserve = state.ammo.reserve_max;
                        d->live = false;
                        play_tag(&state, state.weap.pickup_snd_id, 0.8f);
                    } else if (!carried && state.hud_swap) {
                        state.hud_swap = false;
                        int32_t wi;
                        hta_ammo am;
                        hta_game_take_drop(&state.game, dr, &wi, &am);
                        uint32_t tag = state.game.weapons[wi].tag;
                        int32_t asset = state.game.weapons[wi].asset ? wi : -1;
                        if (state.held_count < HTA_CARRY_MAX) {
                            state.held_ammo[state.held_slot] = state.ammo;
                            state.held_ammo_set[state.held_slot] = true;
                            state.held_slot = state.held_count;
                            state.held_asset[state.held_count] = asset;
                            state.held[state.held_count++] = tag;
                        } else {
                            drop_held(&state);
                            state.held[state.held_slot] = tag;
                            state.held_asset[state.held_slot] = asset;
                        }
                        state.held_ammo_set[state.held_slot] = false;
                        equip_weapon(&state, tag);
                        state.ammo.loaded = am.loaded;
                        state.ammo.reserve = am.reserve;
                        play_tag(&state, state.weap.pickup_snd_id, 1.0f);
                        item_message(&state, tag, 0);
                        hta_log("[items] picked up a dropped %s (%d/%d)", state.weap.path,
                                am.loaded, am.reserve);
                    }
                }
            }
            /* A map weapon we already carry: its ammunition, as Halo gives
             * it, for whichever hand has that gun. */
            {
                int32_t ws = hta_pickups_at_kind(&state.items, feet, HTA_ITEM_WEAPON);
                const hta_item_choice *w = hta_pickups_item(&state.items, ws);
                for (uint32_t k = 0; w && k < state.held_count; k++) {
                    if (state.held[k] != w->tag_id) continue;
                    hta_weapon_def fd;
                    if (!hta_weapon_load_id(&state.cache, NULL, w->tag_id, &fd, NULL, NULL, 0)) break;
                    hta_ammo fresh;
                    hta_ammo_init(&fresh, &fd);
                    hta_ammo *a = k == state.held_slot ? &state.ammo : &state.held_ammo[k];
                    if (k != state.held_slot && !state.held_ammo_set[k]) break;   /* full already */
                    if (a->reserve >= a->reserve_max) break;
                    int before = a->reserve;
                    a->reserve += fresh.loaded + fresh.reserve;
                    if (a->reserve > a->reserve_max) a->reserve = a->reserve_max;
                    hta_pickups_take(&state.items, ws);
                    play_tag(&state, w->pickup_snd ? w->pickup_snd : state.pickup_snd_ammo, 0.8f);
                    item_message(&state, w->tag_id, a->reserve - before);
                    break;
                }
            }
            /* SWAP on a weapon PICKS IT UP; off one it switches between the
             * two you are carrying. Halo splits these across two actions and
             * we have one button, so standing on a gun means you want it. */
            if (state.hud_swap) {
                int32_t wslot = hta_pickups_at_kind(&state.items, feet,
                                                    HTA_ITEM_WEAPON);
                const hta_item_choice *w = hta_pickups_item(&state.items, wslot);
                bool already = false;
                for (uint32_t i = 0; w && i < state.held_count; i++)
                    if (state.held[i] == w->tag_id) already = true;
                if (w && !already) {
                    state.hud_swap = false;
                    if (state.held_count < HTA_CARRY_MAX) {
                        /* A free hand: take it and hold it. */
                        state.held_ammo[state.held_slot] = state.ammo;
                        state.held_ammo_set[state.held_slot] = true;
                        state.held_slot = state.held_count;
                        state.held_asset[state.held_count] = -1;
                        state.held[state.held_count++] = w->tag_id;
                    } else {
                        /* Full: it replaces the one you are holding, which
                         * is the one you were looking at when you chose --
                         * and that one goes on the ground. */
                        drop_held(&state);
                        state.held[state.held_slot] = w->tag_id;
                        state.held_asset[state.held_slot] = -1;
                    }
                    state.held_ammo_set[state.held_slot] = false;
                    equip_weapon(&state, w->tag_id);
                    hta_pickups_take(&state.items, wslot);
                    play_tag(&state, w->pickup_snd, 1.0f);
                    item_message(&state, w->tag_id, 0);
                    hta_log("[items] picked up %s (holding %u)",
                            w->path, state.held_count);
                }
            }

        }
        if (state.items.loaded && hta_pickups_dirty(&state.items)) {
            hta_pickups_pose(&state.items);
            state.items_upload = HTA_ITEMS_UPLOAD_FRAMES;
        }

        /* A powerup running out. The overshield BLEEDS down rather than
         * vanishing: in Halo you watch the extra bars drain, and a cliff
         * edge at sixty seconds would make it impossible to judge. */
        if (state.powerup_timer > 0.0f &&
            (!state.net_enabled || state.net_hosting || !state.net.have_world)) {
            float was = state.powerup_timer;
            state.powerup_timer -= dt;
            if (state.powerup == HTA_ITEM_OVERSHIELD && state.vit->loaded &&
                state.vit->shield > state.vit->max_shield && was > 0.0f) {
                float extra = state.vit->max_shield *
                              (HTA_OVERSHIELD_MULT - 1.0f);
                state.vit->shield -= extra * (dt / was);
                if (state.vit->shield < state.vit->max_shield)
                    state.vit->shield = state.vit->max_shield;
            }
            if (state.powerup_timer <= 0.0f) {
                state.powerup_timer = 0.0f;
                if (state.powerup == HTA_ITEM_OVERSHIELD &&
                    state.vit->loaded &&
                    state.vit->shield > state.vit->max_shield)
                    state.vit->shield = state.vit->max_shield;
                hta_log("[items] powerup over");
                state.powerup = HTA_ITEM_NONE;
            }
        }

        /* The debug pad. Not part of the game: it exists because the map's
         * own item layout is the authority now, and Blood Gulch places
         * neither the needler nor the plasma pistol. */
        if (state.hud_debug) {
            int action = state.hud_debug - 1;
            state.hud_debug = 0;
            if (action == 0 && state.weapon_count && !state.dead) {
                /* Hand over the next weapon in the cache's roster, into the
                 * hand you are using. Walking the whole roster one tap at a
                 * time reaches everything without breaking the two-weapon
                 * rule the rest of the game plays by. */
                state.debug_weapon = (state.debug_weapon + 1u) % state.weapon_count;
                uint32_t give = state.weapons[state.debug_weapon];
                if (state.held_count < HTA_CARRY_MAX) {
                    state.held_slot = state.held_count;
                    state.held_asset[state.held_count] = -1;
                    state.held[state.held_count++] = give;
                } else {
                    state.held[state.held_slot] = give;
                    state.held_asset[state.held_slot] = -1;
                }
                equip_weapon(&state, give);
                hta_log("[debug] gave %s (%u of %u)", state.weap.path,
                        state.debug_weapon + 1u, state.weapon_count);
            }
        }

        /* None of the buttons do anything to a corpse. They are consumed
         * rather than left pending, or every press made while dead would
         * fire at once on respawn. */
        if (state.dead) {
            state.hud_swap = state.hud_zoom = state.hud_melee = false;
            state.hud_reload = state.hud_grenade = false;
            state.hud_debug = 0;
        }

        /* Swapping rebuilds the viewmodel and the HUD, so do it before
         * anything this frame reads either. */
        if (state.hud_swap) {
            state.hud_swap = false;
            if (state.held_count > 1) {
                state.held_ammo[state.held_slot] = state.ammo;
                state.held_ammo_set[state.held_slot] = true;
                state.held_slot = (state.held_slot + 1u) % state.held_count;
                equip_weapon(&state, state.held[state.held_slot]);
                if (state.held_ammo_set[state.held_slot]) {
                    state.ammo = state.held_ammo[state.held_slot];
                    hta_ammo_cancel_reload(&state.ammo);
                }
                net_action(&state,HTA_NET_EVENT_WEAPON);
            }
        }

        if (state.hud_zoom) {
            state.hud_zoom = false;
            cycle_zoom(&state);
        }

        /* A swing takes the weapon out of the fight until it finishes, so
         * the rest of this frame's trigger work has to know about it. */
        bool swinging = state.vm.loaded && state.vm.state == HTA_VM_MELEE;
        /* A bat in hand: the trigger is the swing. */
        {
            const hta_game_weapon *mw = held_imported(&state);
            if (mw && mw->melee_only && in.fire) {
                if (!swinging) state.hud_melee = true;
                in.fire = false;
            }
        }
        if (state.hud_melee) {
            state.hud_melee = false;
            if (!swinging && state.ammo.phase != HTA_AMMO_RELOADING) {
                /* A swing connects with whatever is within arm's reach in
                 * front of you. The damage is the cyborg's own `melee
                 * damage` tag -- 1000, at a x1.00 multiplier against both
                 * armour and shield, so it kills outright. Halo's front /
                 * back distinction is engine logic, not tag data. */
                if (state.game_on && (!state.net_enabled || state.net_hosting)) {
                    /* The held weapon's own `player melee damage` -- 56 --
                     * and a kill from behind, for everyone alike. */
                    if (hta_game_melee(&state.game, state.me) >= 0)
                        hta_log("[game] melee connected");
                } else if (state.bot.loaded && state.melee_damage > 0.0f) {
                    float fwd[3];
                    hta_camera_forward(&state.cam, fwd);
                    float reach[3];
                    for (int k = 0; k < 3; k++)
                        reach[k] = state.player.pos[k] + fwd[k] * HTA_MELEE_REACH;
                    if (hta_bot_near(&state.bot, reach, HTA_MELEE_REACH)) {
                        float ctr[3];
                        hta_bot_centre(&state.bot, ctr);
                        hta_bot_damage(&state.bot, state.melee_damage, ctr);
                        hta_log("[bot] melee connected");
                    }
                }
                vm_play(&state, HTA_VM_MELEE);
                net_action(&state,HTA_NET_EVENT_MELEE);
                swinging = state.vm.state == HTA_VM_MELEE;
            }
        }

        if (state.hud_reload) {
            state.hud_reload = false;
            if (!swinging && hta_ammo_reload(&state.ammo)) {
                state.net_reload_count++;
                vm_play(&state, HTA_VM_RELOAD);
            }
        }
        if (in.fire && !swinging && hta_gun_ready(&state.gun)) {
            if (hta_ammo_shoot(&state.ammo)) {
                shake_fire(&state, state.weap.firing_damage_id);
                /* Autoaim: the weapon's own cone bends the round toward an
                 * enemy near the crosshair (leading one that moves, for a
                 * round that flies). */
                hta_camera aimcam = state.cam;
                if (state.game_on && state.me >= 0) {
                    float fwd[3], pt[3];
                    hta_camera_forward(&state.cam, fwd);
                    if (hta_game_aim_target(&state.game, state.me, held_roster(&state),
                                            state.cam.pos, fwd, pt) >= 0) {
                        float d[3] = { pt[0]-state.cam.pos[0], pt[1]-state.cam.pos[1],
                                       pt[2]-state.cam.pos[2] };
                        aimcam.yaw = atan2f(d[1], d[0]);
                        aimcam.pitch = atan2f(d[2], hypotf(d[0], d[1]));
                    }
                }
                if (state.proj.loaded) {
                    /* An object round does its own collision on the way, so
                     * there is no hitscan to trace and no impact yet. */
                    float dir[3];
                    if (hta_gun_launch(&state.gun, &aimcam, dir)) {
                        float muzzle[3];
                        for (int k = 0; k < 3; k++)
                            muzzle[k] = state.cam.pos[k] + dir[k] * 0.35f;
                        hta_projectiles_fire(&state.proj, muzzle, dir);
                    }
                } else {
                    /* Two halves, so a body can stop the round before the
                     * wall does: aim picks the direction out of the error
                     * cone, then whatever is nearest takes it. */
                    float dir[3];
                    if (hta_gun_aim(&state.gun, &aimcam, dir)) {
                        float bt = -1.0f, bhit[3];
                        int32_t who = -1;
                        bool onbot;
                        bool wall = false;
                        float wh[3], wn[3];
                        if (state.game_on) {
                            float wt = HTA_GUN_RANGE;
                            wall = state.col.built &&
                                hta_collision_ray(&state.col, state.cam.pos, dir,
                                                  HTA_GUN_RANGE, &wt, wh, wn);
                            who = hta_game_ray(&state.game, state.cam.pos, dir,
                                               wall ? wt : HTA_GUN_RANGE, state.me, &bt, bhit);
                            onbot = who >= 0;
                            /* A round into the world may have struck a prop:
                             * tell the props, as the game does for bots'
                             * rounds (HTA_EV_HIT_WORLD), once per pellet.
                             * On a LAN client the host decides (props.remote). */
                            if (wall && !onbot && state.wfx.props.count) {
                                hta_game_event he = { .kind = HTA_EV_HIT_WORLD, .a = state.me, .b = -1 };
                                for (int k = 0; k < 3; k++) { he.pos[k] = wh[k]; he.dir[k] = wn[k]; }
                                int pellets = state.weap.projectiles_per_shot > 0
                                            ? state.weap.projectiles_per_shot : 1;
                                if (pellets > 32) pellets = 32;
                                for (int r = 0; r < pellets; r++)
                                    hta_wfx_game_event(&state.wfx, &he, &state.game);
                            }
                        } else {
                            onbot = hta_bot_ray(&state.bot, state.cam.pos, dir,
                                                HTA_GUN_RANGE, &bt, bhit);
                        }
                        hta_gun_impact(&state.gun,
                                       state.col.built ? &state.col : NULL,
                                       &state.cam, dir, onbot ? bt : -1.0f);
                        /* Our own tracer leaves from just under the eye,
                         * where the barrel is, toward what we hit. */
                        int32_t mine_w = held_roster(&state);
                        if (state.trails.loaded && mine_w >= 0 &&
                            state.wtrail[mine_w] != HTA_CONT_NONE &&
                            tracer_due(&state, -1, state.weap.between_contrails)) {
                            float fwd[3], right[3], up[3], from[3], end[3];
                            hta_camera_forward(&state.cam, fwd);
                            hta_camera_right(&state.cam, right);
                            hta_camera_up(&state.cam, up);
                            float reach = onbot ? bt : HTA_GUN_RANGE;
                            float wt;
                            if (!onbot && state.col.built &&
                                hta_collision_ray(&state.col, state.cam.pos, dir, HTA_GUN_RANGE,
                                                  &wt, NULL, NULL)) reach = wt;
                            if (reach > 100.0f) reach = 100.0f;
                            for (int k = 0; k < 3; k++) {
                                from[k] = state.cam.pos[k] + fwd[k] * 0.4f + right[k] * 0.08f - up[k] * 0.1f;
                                end[k] = state.cam.pos[k] + dir[k] * reach;
                            }
                            hta_contrails_tracer(&state.trails, state.wtrail[mine_w], from, end, 300.0f);
                        }
                        if (onbot && who >= 0 && (!state.net_enabled || state.net_hosting)) {
                            int pellets = state.weap.projectiles_per_shot > 0
                                        ? state.weap.projectiles_per_shot : 1;
                            const hta_game_weapon *iw = held_imported(&state);
                            hta_game_hurt_jpt_scaled(&state.game, who, state.me,
                                              state.impact_jpt, pellets, bhit, iw ? iw->damage_scale : 1.0f);
                        } else if (onbot && !state.game_on) {
                            /* What a round does depends on WHAT it hits:
                             * the same shotgun pellet is 8 into armour and
                             * 4 into a shield, and a plasma bolt is the
                             * other way round. The tag knows. */
                            uint8_t mat = state.bot.vitals.shield > 0.0f
                                        ? HTA_MATERIAL_CYBORG_SHIELD
                                        : HTA_MATERIAL_CYBORG_ARMOR;
                            float dmg = hta_damage_vs(&state.cache,
                                                      state.impact_jpt, mat);
                            /* PROJECTILES per shot, not rounds: the
                             * shotgun spends one shell and throws eight
                             * pellets, and at 4 a pellet into a shield the
                             * difference is a weapon that works and one
                             * that does not. */
                            int pellets = state.weap.projectiles_per_shot > 0
                                        ? state.weap.projectiles_per_shot : 1;
                            if (pellets > 32) pellets = 32;
                            for (int r = 0; r < pellets; r++)
                                hta_bot_damage(&state.bot, dmg, bhit);
                        }
                    }
                    play_impact_at(&state, state.gun.hit_material,
                                   state.gun.last_hit);
                    if (state.net_hosting && state.gun.hit_material<33u) {
                        int32_t weapon=held_roster(&state);
                        if (weapon>=0) {
                            hta_net_fx fx={.kind=HTA_NET_FX_IMPACT,
                                .entity=(uint8_t)state.me,.weapon=(uint8_t)weapon,
                                .material=state.gun.hit_material};
                            for (int k=0;k<3;k++) {
                                fx.pos[k]=state.gun.last_hit[k];
                                fx.dir[k]=state.gun.last_nrm[k];
                            }
                            hta_net_server_fx(&state.host_server,&fx);
                        }
                    }
                    /* And the dust the round kicks off that surface. */
                    if (state.gun.hit_material < 33u &&
                        state.impact_recipe[state.gun.hit_material]
                            != HTA_PART_NO_RECIPE)
                        hta_particles_burst(&state.parts,
                                            state.impact_recipe[state.gun.hit_material],
                                            state.gun.last_hit, state.gun.last_nrm);
                }
                vm_play(&state, HTA_VM_FIRE);
                hta_viewmodel_flash(&state.vm);
                /* Eject the spent casing from the gun's own marker. The
                 * viewmodel poses it in its own space, so it takes the
                 * same basis the renderer builds the weapon with. */
                if (state.casing_recipe != HTA_PART_NO_RECIPE &&
                    state.vm.have_eject) {
                    float fwd[3], right[3], up[3];
                    hta_camera_forward(&state.cam, fwd);
                    hta_camera_right(&state.cam, right);
                    hta_camera_up(&state.cam, up);
                    const float *e = state.vm.eject_pos;
                    float at[3], dir[3];
                    for (int k = 0; k < 3; k++) {
                        at[k] = state.cam.pos[k] + fwd[k]*e[0]
                              - right[k]*e[1] + up[k]*e[2];
                        /* Out to the right and a little up, which is where
                         * every one of these guns throws it. */
                        dir[k] = right[k] + up[k] * 0.35f;
                    }
                    hta_particles_burst(&state.parts, state.casing_recipe, at, dir);
                }
                if (!imported_fire_sound(&state, held_roster(&state), NULL))
                    play_tag(&state, state.fire_snd, 1.0f);
                net_action(&state,HTA_NET_EVENT_FIRE);
            } else if (state.ammo.dry && state.dry_cooldown <= 0.0f) {
                /* Click, then reload by itself, the way Halo does. */
                play_tag(&state, state.empty_snd, 1.0f);
                state.dry_cooldown = 0.35f;
                if (hta_ammo_reload(&state.ammo)) {
                    state.net_reload_count++;
                    vm_play(&state, HTA_VM_RELOAD);
                }
            }
        }
        /* A continuous weapon sounds while the trigger is actually doing
         * something, and goes quiet the moment it is released, the magazine
         * runs out, or a swing takes the weapon out of the fight. */
        bool spraying = in.fire && !swinging &&
                        state.ammo.phase == HTA_AMMO_READY &&
                        state.ammo.loaded >= state.ammo.per_shot;
        fire_loop(&state, spraying);

        /* The jet comes out of the marker the muzzle flash hangs off, which
         * on the flamethrower is `spawn fire` itself, and goes where the
         * player is looking. Emitting is rate-based, not per-shot: the
         * flamethrower's 0.1 s between rounds would otherwise give it a
         * stutter the real weapon does not have. */
        if (spraying && state.jet_recipe != HTA_PART_NO_RECIPE) {
            float fwd[3], right[3], up[3];
            hta_camera_forward(&state.cam, fwd);
            hta_camera_right(&state.cam, right);
            hta_camera_up(&state.cam, up);
            const float *m = state.vm.flash_pos;
            float at[3];
            for (int k = 0; k < 3; k++)
                at[k] = state.cam.pos[k] + fwd[k]*m[0]
                      - right[k]*m[1] + up[k]*m[2];
            hta_particles_emit(&state.parts, state.jet_recipe, at, fwd, dt);
        }

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
        ivm_update(&state, dt);
        damage_numbers(&state, dt);
        /* Throw a grenade.
         *
         * The arm goes first. Every weapon carries a `first-person
         * throw-grenade` clip of about 1.2 s, and the grenade leaves at the
         * clip's key frame -- so the button STARTS the throw and the
         * projectile appears when your hand does. Letting it go on the press
         * put a grenade out of the player's chest with the weapon still
         * sitting there, which is what it looked like. */
        if (state.hud_grenade) {
            state.hud_grenade = false;
            if (state.nades.loaded && state.nade_count > 0 && !swinging &&
                !state.throwing && state.ammo.phase != HTA_AMMO_RELOADING) {
                if (state.vm.loaded && state.vm.clip[HTA_VM_THROW] >= 0) {
                    vm_play(&state, HTA_VM_THROW);
                    state.throwing = true;
                } else {
                    state.throwing = true;
                    state.vm.key_frame_hit = true;   /* no clip: go at once */
                }
            }
        }
        if (state.throwing &&
            (state.vm.key_frame_hit || state.vm.state != HTA_VM_THROW)) {
            /* Either the hand reached the release, or the clip was
             * interrupted -- a throw that is cut short still throws, the
             * same way Halo will not swallow the grenade. */
            state.throwing = false;
            if (state.nades.loaded && state.nade_count > 0) {
                float fwd[3], up[3];
                hta_camera_forward(&state.cam, fwd);
                hta_camera_up(&state.cam, up);
                float at[3], dir[3];
                for (int k = 0; k < 3; k++) {
                    at[k] = state.cam.pos[k] + fwd[k] * 0.4f;
                    dir[k] = fwd[k] + up[k] * 0.25f;
                }
                hta_projectiles_throw(&state.nades, at, dir, HTA_GRENADE_THROW);
                net_action(&state,HTA_NET_EVENT_GRENADE);
                state.nade_count--;
                hta_log("[player] grenade away, %d left", state.nade_count);
            }
        }
        if (state.nades.loaded &&
            (!state.net_enabled || state.net_hosting || !state.net.have_world)) {
            hta_projectiles_update(&state.nades,
                                   state.col.built ? &state.col : NULL, dt);
            if (state.nades.detonated) {
                if (state.net_hosting && state.game.grenade_pool>=0) {
                    hta_net_fx fx={.kind=HTA_NET_FX_DETONATE,
                        .entity=(uint8_t)state.me,
                        .weapon=(uint8_t)state.game.grenade_pool,
                        .material=state.nades.hit_material};
                    for (int k=0;k<3;k++) {
                        fx.pos[k]=state.nades.hit[k];
                        fx.dir[k]=state.nades.hit_normal[k];
                    }
                    hta_net_server_fx(&state.host_server,&fx);
                }
                hta_gun_add_mark(&state.gun, state.nades.hit,
                                 state.nades.hit_normal,
                                 state.nades.blast_radius);
                if (state.nade_snd)
                    play_tag_at(&state, state.nade_snd, state.nades.hit, 1.0f);
                shake_effect(&state, state.nades.det_effect, state.nades.hit);
                if (state.nade_recipe != HTA_PART_NO_RECIPE)
                    hta_particles_burst(&state.parts, state.nade_recipe,
                                        state.nades.hit, state.nades.hit_normal);
                if (state.game_on && (!state.net_enabled || state.net_hosting) &&
                    state.nades.blast_damage > 0.0f) {
                    /* Everyone in it, you included, and it is yours. */
                    hta_game_blast(&state.game, state.me, state.nades.hit,
                                   state.nades.blast_damage, state.nades.blast_core,
                                   state.nades.blast_damage_radius);
                }
                if (!state.game_on && state.bot.loaded && state.nades.blast_damage > 0.0f) {
                    float ctr[3];
                    hta_bot_centre(&state.bot, ctr);
                    float f = blast_falloff(state.nades.hit, ctr,
                                            state.nades.blast_core,
                                            state.nades.blast_damage_radius);
                    if (f > 0.0f)
                        hta_bot_damage(&state.bot,
                                       state.nades.blast_damage * f, ctr);
                }
                if (!state.game_on && state.vit->loaded && state.nades.blast_damage > 0.0f) {
                    float dx = state.cam.pos[0] - state.nades.hit[0];
                    float dy = state.cam.pos[1] - state.nades.hit[1];
                    float dz = state.cam.pos[2] - state.nades.hit[2];
                    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                    float r = state.nades.blast_damage_radius;
                    if (dist < r) {
                        float core = state.nades.blast_core;
                        float f = 1.0f;
                        if (dist > core && r > core)
                            f = 1.0f - (dist - core) / (r - core);
                        if (f > 0.0f)
                            hta_vitals_damage(state.vit,
                                              state.nades.blast_damage * f);
                    }
                }
            }
        }

        /* The body standing out there: animation, dying, coming back. */
        if (state.bot.loaded) hta_bot_update(&state.bot, dt);

        /* Rounds in flight. A detonation leaves the same scorch and plays
         * the same material impact a hitscan round would. */
        if (state.proj.loaded &&
            (!state.net_enabled || state.net_hosting || !state.net.have_world)) {
            hta_projectiles_update(&state.proj,
                                   state.col.built ? &state.col : NULL, dt);
            /* A round that reaches the body stops there. The projectile
             * layer only knows about the world, so this is the one place a
             * flying round is asked whether it has hit somebody. */
            if (state.game_on) {
                for (uint32_t q = 0; q < HTA_PROJ_MAX; q++) {
                    hta_projectile *pr = &state.proj.live[q];
                    if (!pr->alive) continue;
                    int32_t who = hta_game_near(&state.game, pr->pos, 0.02f, state.me);
                    if (who < 0) continue;
                    if (!state.net_enabled || state.net_hosting)
                        hta_game_hurt_jpt_scaled(&state.game, who, state.me, state.impact_jpt, 1, pr->pos,
                                                 held_imported(&state) ? held_imported(&state)->damage_scale : 1.0f);
                    if ((!state.net_enabled || state.net_hosting) && state.proj.blast_damage > 0.0f)
                        hta_game_blast(&state.game, state.me, pr->pos, state.proj.blast_damage,
                                       state.proj.blast_core, state.proj.blast_damage_radius);
                    float up[3] = { 0.0f, 0.0f, 1.0f };
                    if (state.det_recipe != HTA_PART_NO_RECIPE)
                        hta_particles_burst(&state.parts, state.det_recipe, pr->pos, up);
                    if (state.proj.detonation_snd)
                        play_tag_at(&state, state.proj.detonation_snd, pr->pos, 1.0f);
                    if (state.net_hosting)
                        for (uint32_t pool=0;pool<state.game.pool_count;pool++)
                            if (state.game.pools[pool].proj_tag_id==state.proj.proj_tag_id) {
                                hta_net_fx fx={.kind=HTA_NET_FX_DETONATE,
                                    .entity=(uint8_t)state.me,.weapon=(uint8_t)pool};
                                for (int k=0;k<3;k++) fx.pos[k]=pr->pos[k];
                                fx.dir[2]=1.0f;
                                hta_net_server_fx(&state.host_server,&fx);
                                break;
                            }
                    pr->alive = false;
                }
            } else if (state.bot.loaded && state.bot.state == HTA_BOT_ALIVE) {
                for (uint32_t q = 0; q < HTA_PROJ_MAX; q++) {
                    hta_projectile *pr = &state.proj.live[q];
                    if (!pr->alive) continue;
                    if (!hta_bot_near(&state.bot, pr->pos, state.bot.radius))
                        continue;
                    uint8_t mat = state.bot.vitals.shield > 0.0f
                                ? HTA_MATERIAL_CYBORG_SHIELD
                                : HTA_MATERIAL_CYBORG_ARMOR;
                    float dmg = hta_damage_vs(&state.cache, state.impact_jpt, mat);
                    hta_bot_damage(&state.bot, dmg, pr->pos);
                    /* It goes off where it stopped, not where it would
                     * have reached. */
                    float up[3] = { 0.0f, 0.0f, 1.0f };
                    if (state.det_recipe != HTA_PART_NO_RECIPE)
                        hta_particles_burst(&state.parts, state.det_recipe,
                                            pr->pos, up);
                    if (state.proj.detonation_snd)
                        play_tag_at(&state, state.proj.detonation_snd,
                                    pr->pos, 1.0f);
                    pr->alive = false;
                }
            }
            if (state.proj.detonated) {
                if (state.net_hosting) {
                    for (uint32_t pool=0;pool<state.game.pool_count;pool++)
                        if (state.game.pools[pool].proj_tag_id==state.proj.proj_tag_id) {
                            hta_net_fx fx={.kind=HTA_NET_FX_DETONATE,
                                .entity=(uint8_t)state.me,.weapon=(uint8_t)pool,
                                .material=state.proj.hit_material};
                            for (int k=0;k<3;k++) {
                                fx.pos[k]=state.proj.hit[k];
                                fx.dir[k]=state.proj.hit_normal[k];
                            }
                            hta_net_server_fx(&state.host_server,&fx);
                            break;
                        }
                }
                hta_gun_add_mark(&state.gun, state.proj.hit, state.proj.hit_normal,
                                 state.proj.blast_radius);
                /* An explosion has a bang of its own; a round that does not
                 * falls back to what the surface it hit sounds like. */
                if (state.proj.detonation_snd)
                    play_tag_at(&state, state.proj.detonation_snd,
                                state.proj.hit, 1.0f);
                else
                    play_impact_at(&state, state.proj.hit_material,
                                   state.proj.hit);
                /* Thrown out along the surface it hit. */
                hta_particles_burst(&state.parts, state.det_recipe,
                                    state.proj.hit, state.proj.hit_normal);
                shake_effect(&state, state.proj.det_effect, state.proj.hit);

                /* And it can catch you. A rocket is 80 at the centre,
                 * full inside 0.6 world units and gone by 2.0 -- which is
                 * why firing one at your own feet is a bad idea in Halo
                 * and now here too. */
                if (state.game_on && (!state.net_enabled || state.net_hosting) &&
                    state.proj.blast_damage > 0.0f)
                    hta_game_blast(&state.game, state.me, state.proj.hit,
                                   state.proj.blast_damage, state.proj.blast_core,
                                   state.proj.blast_damage_radius);
                if (!state.game_on && state.bot.loaded && state.proj.blast_damage > 0.0f) {
                    float ctr[3];
                    hta_bot_centre(&state.bot, ctr);
                    float f = blast_falloff(state.proj.hit, ctr,
                                            state.proj.blast_core,
                                            state.proj.blast_damage_radius);
                    if (f > 0.0f)
                        hta_bot_damage(&state.bot,
                                       state.proj.blast_damage * f, ctr);
                }
                if (!state.game_on && state.vit->loaded && state.proj.blast_damage > 0.0f) {
                    float dx = state.cam.pos[0] - state.proj.hit[0];
                    float dy = state.cam.pos[1] - state.proj.hit[1];
                    float dz = state.cam.pos[2] - state.proj.hit[2];
                    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                    float r = state.proj.blast_damage_radius;
                    if (dist < r) {
                        float core = state.proj.blast_core;
                        float f = 1.0f;
                        if (dist > core && r > core)
                            f = 1.0f - (dist - core) / (r - core);
                        if (f > 0.0f) {
                            hta_vitals_damage(state.vit,
                                              state.proj.blast_damage * f);
                            hta_log("[player] caught the blast at %.1f wu for %.0f",
                                    dist, state.proj.blast_damage * f);
                        }
                    }
                }
            }
        }

        /* The weapon sways with the walk, from the weapon's own `moving`
         * overlay, scaled by how fast the player is actually going. */
        if (state.vm.loaded) {
            float run = state.player.phys.run_forward > 0.1f
                      ? state.player.phys.run_forward : 2.25f;
            float vx = state.player.velocity[0], vy = state.player.velocity[1];
            float speed = sqrtf(vx * vx + vy * vy);
            hta_viewmodel_set_move(&state.vm, speed / run);
        }

        /* Everybody else: the bots think and fight, their rounds fly, the
         * dead come back and the score is kept. Before the particles, so a
         * bot's explosion bursts this frame. */
        if (state.game_on) {
            if (!state.net_enabled || state.net_hosting) {
                hta_game_update(&state.game, dt);
                game_events(&state);
            }
            hta_game_view_update(&state.gview, &state.game, view_skip(&state), dt);
            if (state.net_enabled && !state.net_hosting)
                for (uint32_t i=0;i<state.game.unit_count;i++)
                    state.game.units[i].fired=state.game.units[i].meleed=
                    state.game.units[i].threw=state.game.units[i].hurt=false;
            if ((!state.net_enabled || state.net_hosting) && state.over_timer > 0.0f) {
                state.over_timer -= dt;
                if (state.over_timer <= 0.0f) {
                    nav_props(&state);
                    hta_game_start(&state.game);
                    if (state.net_hosting) state.world_round++;
                    if (!state.dead) respawn(&state);
                    hta_log("[game] a new game");
                }
            }
            game_text(&state, dt);
        }
        hta_particles_update(&state.parts, state.col.built ? &state.col : NULL,
                             &state.cam, dt);
        net_frame(&state,now,dt,&in);
        hero_occupancy(&state);
        vehicle_transition(&state);
        vehicle_camera(&state);
        broom_camera(&state);
        vehicle_sounds(&state);
        flight_music(&state);
        if (state.trails.loaded) {
            for (uint32_t p = 0; p < state.game.pool_count; p++) {
                if (state.ptrail[p] == HTA_CONT_NONE) continue;
                for (uint32_t k = 0; k < HTA_PROJ_MAX; k++) {
                    const hta_projectile *q = &state.game.pools[p].live[k];
                    if (q->alive) hta_contrails_feed(&state.trails, state.ptrail[p], p * 16u + k,
                                                     q->pos, q->age);
                }
            }
            uint32_t lt = state.proj.loaded
                ? hta_contrails_for_projectile(&state.trails, &state.cache, NULL, state.proj.proj_tag_id)
                : HTA_CONT_NONE;
            for (uint32_t k = 0; lt != HTA_CONT_NONE && k < HTA_PROJ_MAX; k++)
                if (state.proj.live[k].alive)
                    hta_contrails_feed(&state.trails, lt, 1000u + k, state.proj.live[k].pos,
                                       state.proj.live[k].age);
            hta_contrails_update(&state.trails, &state.cam, dt);
        }
        hta_shake_update(&state.shake, dt);
        /* A hull on its last third throws sparks, faster as it goes. */
        if (state.game_on && state.spark_recipe != HTA_PART_NO_RECIPE &&
            state.vehicles.loaded) {
            state.spark_clock += dt;
            if (state.spark_clock >= 0.12f) {
                state.spark_clock = 0.0f;
                for (uint32_t i = 0; i < state.vehicles.count && i < HTA_VEHICLE_MAX; i++) {
                    const hta_vehicle *c = &state.vehicles.cars[i];
                    float hull = hta_game_hull(&state.game, (int32_t)i);
                    if (!c->active || hull > 0.35f) continue;
                    if ((float)(rand() % 100) / 100.0f > 0.35f + (0.35f - hull) * 2.0f) continue;
                    float a = (float)(rand() % 628) / 100.0f;
                    float at[3] = { c->pos[0] + cosf(a) * c->body_radius * 0.4f,
                                    c->pos[1] + sinf(a) * c->body_radius * 0.4f,
                                    c->pos[2] + 0.35f };
                    float up[3] = { cosf(a) * 0.3f, sinf(a) * 0.3f, 0.9f };
                    hta_particles_burst(&state.parts, state.spark_recipe, at, up);
                }
            }
        }

        if (state.gun.dirty && state.gfx) {
            char err[HTA_ERRLEN];
            hta_gun_build_mesh(&state.gun);
            if (state.gpu_fx) { hta_gfx_mesh_free(state.gfx, state.gpu_fx); state.gpu_fx = NULL; }
            if (state.gun.mesh.index_count)
                state.gpu_fx = hta_gfx_mesh_upload(state.gfx, &state.gun.mesh, err, sizeof(err));
        }

        /* A class screen up: its character preview instead of the world. */
        if (atomic_load(&g_preview_on) && state.has_window && state.gfx) {
            preview_draw(&state, dt);
        } else
        /* A frozen killcam keeps the last frame on screen: nothing drawn. */
        if (state.has_window && state.gfx &&
            !(state.killcam_phase == 2 && state.dead && state.dead_timer > HTA_DEATH_FADE_OUT)) {
            rebuild_gfx_if_size_changed(&state);
            if (!state.gfx) continue;
            hta_gfx_viewmodel vmdraw;
            memset(&vmdraw, 0, sizeof(vmdraw));
            hta_gfx_overlay huddraw;
            memset(&huddraw, 0, sizeof(huddraw));
            if (state.gpu_hud) {
                uint32_t ew = 0, eh = 0;
                hta_gfx_extent(state.gfx, &ew, &eh);
                /* An imported weapon's own crosshair opens with the spread. */
                if (held_imported(&state)) hta_hud_set_cross_bloom(&state.hud, state.gun.error);
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
            /* And a corpse is not holding it either. */
            bool fp_weapon = (!driving || (armed_seat && !state.show_self)) && !state.player.fly;
            const hta_game_weapon *iw = held_imported(&state);
            if (iw && state.gpu_ivm && state.ivm_posed && state.ivm_weapon == held_roster(&state) &&
                state.zoom_level == 0 && !state.dead && fp_weapon) {
                /* An imported weapon's own first-person model and hands. */
                vmdraw.mesh = state.gpu_ivm;
                vmdraw.vertices = state.ivm_posed;
                vmdraw.vertex_count = iw->asset->models[1].mesh.vertex_count;
            } else if (!iw && state.gpu_fp && state.zoom_level == 0 && !state.dead && fp_weapon) {
                vmdraw.mesh = state.gpu_fp;
                vmdraw.vertices = state.vm.posed;
                vmdraw.vertex_count = state.vm.mesh.vertex_count;
                for (int k = 0; k < 3; k++) vmdraw.offset[k] = state.weap.fp_offset[k];
            }
            hta_gfx_dynamic dynlist[HTA_GFX_MAX_DYNAMIC];
            memset(dynlist, 0, sizeof(dynlist));   /* `lit` defaults off */
            uint32_t dyncount = 0;
            if (state.gpu_proj) {
                dynlist[dyncount].mesh = state.gpu_proj;
                dynlist[dyncount].vertices = state.proj.mesh.vertices;
                dynlist[dyncount].vertex_count = state.proj.mesh.vertex_count;
                dyncount++;
            }
            if (state.gpu_nades) {
                dynlist[dyncount].mesh = state.gpu_nades;
                dynlist[dyncount].vertices = state.nades.mesh.vertices;
                dynlist[dyncount].vertex_count = state.nades.mesh.vertex_count;
                dyncount++;
            }
            if (state.gpu_parts) {
                dynlist[dyncount].mesh = state.gpu_parts;
                dynlist[dyncount].vertices = state.parts.mesh.vertices;
                dynlist[dyncount].vertex_count = state.parts.mesh.vertex_count;
                dyncount++;
            }
            if (state.bot.loaded && state.gpu_bot &&
                dyncount < HTA_GFX_MAX_DYNAMIC) {
                dynlist[dyncount].mesh = state.gpu_bot;
                dynlist[dyncount].vertices = state.bot.actor.posed;
                dynlist[dyncount].vertex_count = state.bot.actor.mesh.vertex_count;
                dynlist[dyncount].lit = true;     /* a body, not a spark */
                dyncount++;
            }
            if (state.gpu_items && dyncount < HTA_GFX_MAX_DYNAMIC) {
                dynlist[dyncount].mesh = state.gpu_items;
                /* NULL skips the copy. The items do not move, so they are
                 * only written into the vertex slots after something is
                 * taken or comes back -- 23,000 vertices every frame to
                 * keep thirty-seven still objects still would be 900 KB a
                 * frame of nothing. */
                dynlist[dyncount].vertices =
                    state.items_upload > 0 ? state.items.posed : NULL;
                dynlist[dyncount].vertex_count = state.items.mesh.vertex_count;
                if (state.items_upload > 0) state.items_upload--;
                dyncount++;
            }
            if (state.corpse_up && state.gpu_corpse && !mine_imported(&state) &&
                dyncount < HTA_GFX_MAX_DYNAMIC) {
                dynlist[dyncount].mesh = state.gpu_corpse;
                dynlist[dyncount].vertices = state.corpse.posed;
                dynlist[dyncount].vertex_count = state.corpse.mesh.vertex_count;
                dynlist[dyncount].lit = true;
                if (state.game_on && state.game.teams && state.me >= 0) {
                    dynlist[dyncount].change = true;
                    hta_game_team_color(state.game.units[state.me].team,
                                        dynlist[dyncount].change_color);
                }
                dyncount++;
            }
            int remote_slot=state.remote_to.weapon==1 ? 1 : 0;
            if (state.remote_visible && !state.net_hosting && !state.world_applied_tick &&
                state.gpu_remote[remote_slot] &&
                dyncount < HTA_GFX_MAX_DYNAMIC) {
                dynlist[dyncount].mesh = state.gpu_remote[remote_slot];
                dynlist[dyncount].vertices = state.remote[remote_slot].posed;
                dynlist[dyncount].vertex_count = state.remote[remote_slot].mesh.vertex_count;
                dynlist[dyncount].lit = true;
                dyncount++;
            }
            if (state.gpu_trails && dyncount < HTA_GFX_MAX_DYNAMIC) {
                dynlist[dyncount].mesh = state.gpu_trails;
                dynlist[dyncount].vertices = state.trails.mesh.vertices;
                dynlist[dyncount].vertex_count = state.trails.mesh.vertex_count;
                dynlist[dyncount].vertex_color = true;
                dyncount++;
            }
            /* World effects: set up once the map's collision exists (and
             * again for a new map), stepped every frame, drawn after the
             * engine's own dynamic meshes. */
            if (state.map_loaded && state.wfx_world != (const void *)state.mesh.vertices) {
                if (!state.wfx.ready) {
                    if (hta_wfx_init(&state.wfx, &state.col, &state.video)) {
                        hta_wfx_choose_weather(&state.wfx,
                            hta_wfx_parse_weather(state.video_cfg, state.video_cfg_len));
                        hta_wfx_gpu_upload(&state.wfx, state.gfx);
                    }
                    /* The sounds Halo has none for: breaking props, debris
                     * landing, gibs, rain and wind. Synthesised, no assets. */
                    if (state.audio_ok && !state.wfx_audio.ready &&
                        hta_wfx_audio_init(&state.wfx_audio, &state.audio, 0x5A7Du))
                        hta_log("[wfx] %.1f MB of procedural sound", (double)state.wfx_audio.bank.bytes / 1048576.0);
                } else {
                    hta_wfx_reset(&state.wfx);
                }
                /* An imported map's breakables and weather. Props come back
                 * after 30 s (ours) so a long match keeps its cover. */
                hta_wfx_load_map(&state.wfx, state.world_loaded ? &state.world_ext : NULL, 30.0f);
                state.props_synced = state.props_count_warned = false;
                if (state.wfx.props.count)
                    hta_log("[wfx] %u breakable props, weather %s", state.wfx.props.count,
                            hta_weather_name(state.wfx.weather.kind));
                state.wfx_world = state.mesh.vertices;
            }
            /* A LAN client breaks and rebuilds props only as the host says. */
            state.wfx.props.remote = state.net_enabled && !state.net_hosting;
            nav_props(&state);
            /* Props are solid while whole: their instances ride with the
             * vehicles' in the grid everyone collides with. */
            if (state.wfx.ready && state.wfx.props.count) {
                uint32_t nv = state.vehicles.loaded ? state.vehicles.count : 0u;
                state.col.instance_count = hta_props_instances(&state.wfx.props, state.vehicles.inst, nv,
                    state.col_merged, (uint32_t)(sizeof(state.col_merged) / sizeof(state.col_merged[0])));
                state.col.instances = state.col_merged;
                /* A broad phase over them: every ray, ground probe and
                 * debris contact looks at the few near it, not all. */
                hta_collision_index_instances(&state.col, &state.col_index, 0.25f);
            }
            /* Cars smash props they drive into (they do not collide). */
            for (uint32_t i = 0; state.wfx.props.count && state.vehicles.loaded && i < state.vehicles.count; i++) {
                const hta_vehicle *car = &state.vehicles.cars[i];
                if (!car->active) continue;
                float sp = hta_vehicles_speed(&state.vehicles, i);
                float v3[3] = { cosf(car->yaw) * sp, sinf(car->yaw) * sp, 0.0f };
                hta_wfx_ram(&state.wfx, car->pos, v3, car->body_radius > 0.1f ? car->body_radius : 1.0f);
            }
            /* What broke or came back: hide or show its triangles, and an
             * explosive one is a real blast (the host's game hurts people). */
            {
                hta_prop_event pe;
                while (state.wfx.ready && hta_props_pop(&state.wfx.props, &pe)) {
                    uint32_t tag = state.wfx.props.props[pe.prop].user;
                    for (uint32_t i = 0; tag && state.gpu_mesh && i < state.mesh.submesh_count &&
                                         state.world_loaded && state.world_ext.submesh_breakable; i++)
                        if (state.world_ext.submesh_breakable[i] == tag)
                            hta_gfx_mesh_set_draw_mode(state.gpu_mesh, i,
                                pe.kind == HTA_PROP_EV_RESPAWNED ? state.mesh.submeshes[i].draw_mode : HTA_DRAW_SKIP);
                    if (pe.kind == HTA_PROP_EV_EXPLODED) {
                        if (state.game_on && (!state.net_enabled || state.net_hosting))
                            hta_game_blast(&state.game, -1, pe.pos, pe.damage, pe.radius * 0.3f, pe.radius);
                        hta_props_blast(&state.wfx.props, pe.pos, pe.damage, pe.radius,
                                        &state.wfx.rigid, &state.wfx.fx);
                        hta_fx_burst(&state.wfx.fx, HTA_BURST_SPARKS, pe.pos, NULL, 40);
                        shake_thunder(&state, 0.8f);
                    }
                }
            }
            hta_scene drawscene = state.scene;
            if (state.wfx.ready) {
                hta_wfx_update(&state.wfx, dt, &state.cam);
                /* Halo plays its own detonations and wrecks: skip those echoes. */
                hta_wfx_audio_update(&state.wfx_audio, &state.wfx, &state.cam, dt, true);
                hta_gfx_settings look;
                hta_wfx_frame_look(&state.wfx, &state.video, &look,
                                   drawscene.ambient, drawscene.light_color);
                hta_gfx_apply_settings(state.gfx, &look, NULL, 0);
                hta_wfx_gpu_frame(&state.wfx, state.gfx, &state.video, dt);
                dyncount = hta_wfx_gpu_draw(&state.wfx, &state.cam, dynlist, dyncount,
                                            HTA_GFX_MAX_DYNAMIC);
                if (state.wfx.weather.thunder_ready) {
                    float tg;
                    if (hta_weather_thunder(&state.wfx.weather, &tg)) shake_thunder(&state, tg);
                }
            }

            if (!state.gpu_gibs && hta_game_view_gib_mesh(&state.gview)) {
                char err[HTA_ERRLEN];
                state.gpu_gibs = hta_gfx_mesh_upload_dynamic(state.gfx,
                    hta_game_view_gib_mesh(&state.gview), err, sizeof(err));
            }
            if (state.gpu_gibs && dyncount < HTA_GFX_MAX_DYNAMIC) {
                const hta_bsp_mesh *gm = hta_game_view_gib_mesh(&state.gview);
                dynlist[dyncount].mesh = state.gpu_gibs;
                dynlist[dyncount].vertices = gm ? gm->vertices : NULL;
                dynlist[dyncount].vertex_count = gm ? gm->vertex_count : 0;
                dynlist[dyncount].vertex_color = true;
                dyncount++;
            }
            g_inst_count = 0;
            dyncount = game_draw(&state, dynlist, dyncount);
            vehicles_draw(&state);
            hta_gfx_set_instances(state.gfx, g_inst, g_inst_count);
            hta_camera drawcam = state.cam;
            hta_shake_apply(&state.shake, &drawcam);
            g_phase = "draw";
            if (!hta_gfx_draw(state.gfx, &drawcam, &drawscene, state.gpu_mesh,
                              state.gpu_sky, state.gpu_fx,
                              dynlist, dyncount,
                              vmdraw.mesh ? &vmdraw : NULL,
                              state.gpu_hud ? &huddraw : NULL)) {
                hta_log("[app] surface lost; rebuilding renderer");
                stop_gfx(&state);
                if (app->window) start_gfx(&state);
            }
            state.frames++;
            hta_frame_stats_add(&state.frame_stats, dt * 1000.0f);
            g_phase = "frame";
            state.fps_accum += dt;
            state.fps_frames++;
            if (driving && state.my_car >= 0) {
                /* World units are ten feet: wu/s x 3.048 x 3.6 is km/h. */
                float kmh = hta_vehicles_speed(&state.vehicles, (uint32_t)state.my_car)
                          * 3.048f * 3.6f;
                const hta_game_vgun *gun = &state.game.vgun[state.my_car];
                bool loading = (!state.net_enabled || state.net_hosting) &&
                               (gun->chamber[0] > 0.0f || gun->chamber[1] > 0.0f);
                float hull = hta_game_hull(&state.game, state.my_car);
                char hulls[16] = "";
                if (hull < 0.995f)
                    snprintf(hulls, sizeof(hulls), "  HULL %d%%", (int)(hull * 100.0f + 0.5f));
                snprintf(g_ammo_text, sizeof(g_ammo_text), "%.0f km/h%s%s", kmh,
                         loading ? "  LOADING" : "", hulls);
            }
            else if (state.ammo.phase == HTA_AMMO_RELOADING)
                snprintf(g_ammo_text, sizeof(g_ammo_text), "-- / %d", state.ammo.reserve);
            else
                snprintf(g_ammo_text, sizeof(g_ammo_text), "%d / %d",
                         state.ammo.loaded, state.ammo.reserve);
            snprintf(g_debug_text, sizeof(g_debug_text),
                     "%.2f %.2f %.2f  %s  %.0f fps  net:%u/%u %.0fms",
                     state.player.pos[0], state.player.pos[1], state.player.pos[2],
                     state.player.on_ground ? "ground" : "air",
                     state.fps_accum > 0.05 ? state.fps_frames / state.fps_accum : 0.0,
                     state.net_enabled ? state.net.id : 0,
                     state.remote_visible ? state.remote_id : 0,
                     state.net_enabled ? state.net.stats.ping_ms : 0.0);
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
                if (state.net_enabled) {
                    const hta_net_stats *n=&state.net.stats, *old=&state.net_stats_prev;
                    double seconds=state.fps_accum;
                    hta_log("[net] id %u remote %u ping %.1f ms | %.1f/%.1f pkt/s "
                            "%.0f/%.0f B/s in/out | %.1f snapshots/s | invalid %llu dropped %llu",
                            state.net.id,state.remote_visible ? state.remote_id : 0,
                            n->ping_ms,
                            (n->packets_in-old->packets_in)/seconds,
                            (n->packets_out-old->packets_out)/seconds,
                            (n->bytes_in-old->bytes_in)/seconds,
                            (n->bytes_out-old->bytes_out)/seconds,
                            (n->snapshots_in-old->snapshots_in)/seconds,
                            (unsigned long long)n->invalid,
                            (unsigned long long)n->dropped);
                    state.net_stats_prev=*n;
                }
                state.fps_accum = 0.0;
                state.fps_frames = 0;
            }
        }
    }

done:
    hta_log("[app] shutting down after %llu frames", (unsigned long long)state.frames);
    /* Stop the stream before freeing the PCM its voices point at. */
    hta_audio_android_stop();
    if (state.net_enabled) hta_net_client_close(&state.net);
    if (state.net_hosting) hta_net_server_close(&state.host_server);
    hta_hud_free(&state.hud);
    for (uint32_t i = 0; i < state.bank_count; i++)
        for (uint32_t k = 0; k < state.bank[i].count; k++)
            free(state.bank[i].pcm[k]);
    state.bank_count = 0;
    stop_gfx(&state);
    hta_game_view_free(&state.gview);
    hta_game_free(&state.game);
    hta_nav_free(&state.nav);
    hta_external_map_free(&state.world_ext);
    for (uint32_t k = 0; k < HTA_MAX_IMPORTED; k++) { hta_oal_free(&state.imp_char[k]); hta_oal_free(&state.imp_weap[k]); }
    free(state.ivm_posed);
    free(state.world_playable);
    hta_collision_free(&state.col);
    hta_vehicles_free(&state.vehicles);
    hta_contrails_free(&state.trails);
    hta_gun_free(&state.gun);
    hta_projectiles_free(&state.proj);
    hta_projectiles_free(&state.nades);
    hta_particles_free(&state.parts);
    hta_bsp_free(&state.mesh);
    hta_bsp_free(&state.sky);
    hta_bsp_free(&state.coll_mesh);
    hta_viewmodel_free(&state.vm);
    for (int slot=0;slot<2;slot++) hta_actor_free(&state.remote[slot]);
    for (uint32_t i = 0; i < state.menu_pcm_count; i++) free(state.menu_pcm[i]);
    hta_menu_free(&state.menu);
    for (uint32_t i = 0; i < state.mapped_count; i++)
        munmap(state.mapped_base[i], state.mapped_len[i]);
}
