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

struct hta_gfx_mesh;

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
