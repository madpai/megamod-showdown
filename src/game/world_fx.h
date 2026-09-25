/* The world-effects director: one object a platform loop owns to get
 * debris, gibs, splats, weather and breakable props, driven by the game's
 * own events. Portable; the Android loop and the desktop client make the
 * same handful of calls:
 *
 *     hta_wfx_init(&w, &col, &settings);         once the map's collision exists
 *     hta_wfx_game_event(&w, &event, &game);     for every event the game pops
 *     hta_wfx_net_fx(&w, kind, pos, dir);        on a client, for replicated fx
 *     hta_wfx_update(&w, dt, &cam);              once per frame
 *     hta_wfx_gpu_upload / _draw / _free         (world_fx_gpu.c: the renderer side)
 *
 * Everything it simulates is cosmetic and local to the device, so it needs
 * no network traffic of its own; what breaks a prop is decided by the host
 * and arrives as an event like any other. */
#ifndef HTA_WORLD_FX_H
#define HTA_WORLD_FX_H

#include <stdbool.h>
#include <stdint.h>
#include "../engine/fx.h"
#include "../engine/gore.h"
#include "../engine/props.h"
#include "../engine/rigid.h"
#include "../engine/weather.h"
#include "../gfx/gfx_settings.h"
#include "game.h"
#include "../asset/external_map.h"

struct hta_gfx_mesh;

/* Sounds the effects call for, as cues a platform plays (world_fx_audio.c
 * with the procedural bank, or anything else). `echo` marks one the game
 * already makes a sound for (a Halo grenade's own detonation), so a
 * platform with game sounds skips it and one without plays it. */
typedef enum {
    HTA_WFX_CUE_BREAK = 1,   /* a prop broke; material says what it was */
    HTA_WFX_CUE_KNOCK,       /* debris landed; strength = closing speed, wu/s */
    HTA_WFX_CUE_GIB,
    HTA_WFX_CUE_BLAST,       /* strength = radius, wu */
    HTA_WFX_CUE_THUNDER,     /* strength = loudness 0..1 */
} hta_wfx_cue_kind;
typedef struct {
    uint8_t kind, material;  /* hta_wfx_cue_kind, hta_rigid_material */
    bool    echo;
    float   pos[3];
    float   strength;
} hta_wfx_cue;
#define HTA_WFX_MAX_CUES 32u
#define HTA_WFX_KNOCKS_PER_FRAME 3u

/* Weather choice from the SETTINGS screen: AUTO follows the map. */
#define HTA_WFX_WEATHER_AUTO (-1)

typedef struct {
    bool ready;
    hta_rigid_world rigid;
    hta_fx          fx;
    hta_weather     weather;
    hta_props       props;
    uint32_t        gib_level;
    int             weather_choice;     /* hta_weather_kind or AUTO */
    hta_weather_kind map_weather;       /* what the map asks for */
    float           map_weather_intensity;
    /* renderer side (world_fx_gpu.c) */
    struct hta_gfx_mesh *gpu_debris, *gpu_sprites, *gpu_weather;
    uint32_t weather_hw;                /* most drops live since upload: what to copy */
    /* dynamic resolution */
    float frame_ms;                     /* smoothed */
    float dynres_timer;
    hta_gfx_settings dynres;            /* a scratch copy whose scale moves */
    /* sound cues since the last pop */
    hta_wfx_cue cues[HTA_WFX_MAX_CUES];
    uint32_t    cue_count;
    uint8_t    *prop_was_broken;        /* per prop, to hear it break */
    uint32_t    prop_seen_cap;
    bool        thunder_heard;
    /* diagnostics */
    uint32_t gibbed, broken;
} hta_world_fx;

/* Sizes pools from the settings (debris budget, particle density, gore). */
bool hta_wfx_init(hta_world_fx *w, const hta_collision *col, const hta_gfx_settings *s);
void hta_wfx_free(hta_world_fx *w);
/* New settings: resizes the debris pool, rescales density, gore level. */
void hta_wfx_settings(hta_world_fx *w, const hta_gfx_settings *s);
/* The map changed (same collision grid object, new contents): clear it all. */
void hta_wfx_reset(hta_world_fx *w);

/* An imported map's breakables become props (whole, solid, each its own
 * collision instance) and its weather the map weather. Props respawn after
 * `respawn` seconds (0: never). Replaces any props already set. */
void hta_wfx_load_map(hta_world_fx *w, const hta_external_map *m, float respawn);
/* A vehicle at `pos` (bounding radius, speed in wu/s) runs into props:
 * vehicles do not collide with props (their terrain queries leave
 * instances out), so a prop they overlap breaks instead, thrown ahead.
 * Below a walking pace nothing happens. */
void hta_wfx_ram(hta_world_fx *w, const float pos[3], const float vel[3], float radius);
/* Damage per bullet to a prop, ours. */
#define HTA_WFX_BULLET_DAMAGE 12.0f

/* The map's own weather (from its package) and the player's choice. */
void hta_wfx_set_map_weather(hta_world_fx *w, hta_weather_kind k, float intensity);
void hta_wfx_choose_weather(hta_world_fx *w, int choice);

/* React to a game event: gibs on a blast death, clods and dust from a
 * detonation, torn panels from a wrecked vehicle, a little blood on a hit,
 * chips off a struck surface. */
void hta_wfx_game_event(hta_world_fx *w, const hta_game_event *e, const hta_game *g);
/* A joining device has only the replicated effects: same reactions. */
typedef enum { HTA_WFX_NET_DETONATE = 1, HTA_WFX_NET_WRECK, HTA_WFX_NET_GIB } hta_wfx_net_kind;
void hta_wfx_net_fx(hta_world_fx *w, hta_wfx_net_kind kind, const float pos[3], const float dir[3],
                    float amount);

void hta_wfx_update(hta_world_fx *w, float dt, const hta_camera *cam);

/* A cue from the caller, for effects it made itself (gibs it spawned). */
void hta_wfx_push_cue(hta_world_fx *w, hta_wfx_cue_kind k, uint8_t material, const float pos[3],
                      float strength, bool echo);
/* The next sound cue, oldest first. */
bool hta_wfx_pop_cue(hta_world_fx *w, hta_wfx_cue *out);
/* How loud the weather is where the listener stands, 0..1 each: rain (on
 * the roof, muffled, when under one) and wind. */
void hta_wfx_ambience(const hta_world_fx *w, const float ear[3], float *rain, float *wind);

/* How this frame's scene should differ: fog pulled toward the weather's,
 * and a lightning flash. Writes a per-frame copy of the settings (for
 * hta_gfx_apply_settings, which only rebuilds on structural change) and
 * brightens the scene's ambient and light in place. */
void hta_wfx_frame_look(const hta_world_fx *w, const hta_gfx_settings *base,
                        hta_gfx_settings *out, float scene_ambient[3], float scene_light[3]);

/* Reads `weather = ...` from a video.cfg text: a kind, AUTO, or AUTO when
 * absent. */
int  hta_wfx_parse_weather(const char *text, size_t len);

#endif
