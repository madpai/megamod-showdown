/* Weather: rain, storms, snow, ash, sandstorms. Portable.
 *
 * Drops live in a box that travels with the camera and wraps, so a storm
 * costs the same anywhere on the map and never runs out. Each drop is a
 * streak (rain) or a tumbling flake (snow, ash, sand) in its own dynamic
 * mesh, sharing the fx atlas.
 *
 * Rain does not fall indoors. A coarse ROOF MAP around the camera records,
 * per 2 wu column, the height of the first thing overhead; a drop below
 * its column's roof is not drawn, and splashes only happen in the open.
 * Columns are probed lazily, a few per frame, as the camera moves.
 *
 * A storm flashes lightning: `flash` rises to 1 and decays, for the
 * caller to add to the scene's light and fog, and a THUNDER event follows
 * after the time sound takes to arrive. The weather also suggests fog:
 * rain thickens it, a sandstorm turns it brown and close. */
#ifndef HTA_WEATHER_H
#define HTA_WEATHER_H

#include <stdbool.h>
#include <stdint.h>
#include "../asset/bsp.h"
#include "camera.h"
#include "fx.h"
#include "player.h"

typedef enum {
    HTA_WEATHER_CLEAR = 0,
    HTA_WEATHER_RAIN,
    HTA_WEATHER_STORM,       /* heavy rain, wind, lightning */
    HTA_WEATHER_SNOW,
    HTA_WEATHER_ASH,         /* slow grey flakes, a burning map */
    HTA_WEATHER_SANDSTORM,   /* fast horizontal grit, brown fog */
    HTA_WEATHER_COUNT
} hta_weather_kind;

#define HTA_ROOF_N 24u        /* columns per side */
#define HTA_ROOF_CELL 2.0f    /* wu per column */

typedef struct { float pos[3]; float phase; } hta_drop;

typedef struct {
    hta_weather_kind kind;
    float intensity;          /* 0..1 of the kind's full strength */
    float wind[2];            /* wu/s, horizontal */
    float density;            /* the weather_density setting */
    /* drops */
    hta_drop *drops;
    uint32_t  max_drops, live;
    hta_bsp_mesh mesh;        /* template; textures borrowed from an fx atlas */
    hta_vertex  *verts;
    float box[3];             /* half extents of the travelling box */
    /* roof map */
    float    roof[HTA_ROOF_N][HTA_ROOF_N];
    int32_t  roof_key[HTA_ROOF_N][HTA_ROOF_N][2];
    bool     roof_known[HTA_ROOF_N][HTA_ROOF_N];
    uint32_t roof_probes;     /* diagnostics */
    /* lightning */
    float    flash;           /* 0..1, decays */
    float    next_strike;     /* seconds */
    float    thunder_in;      /* seconds until THUNDER; < 0 none pending */
    float    thunder_gain;
    bool     thunder_ready;
    float    splash_acc;
    float    time;
    uint32_t rng;
    const hta_collision *world;
} hta_weather;

bool hta_weather_init(hta_weather *w, uint32_t max_drops, const hta_bsp_texture *atlas,
                      const hta_collision *world);
void hta_weather_free(hta_weather *w);
void hta_weather_set(hta_weather *w, hta_weather_kind kind, float intensity, const float wind[2]);

/* Advance; `fx` (optional) receives splashes. */
void hta_weather_update(hta_weather *w, float dt, const hta_camera *cam, hta_fx *fx);
/* This frame's vertices for w->mesh. */
void hta_weather_build(hta_weather *w, const hta_camera *cam);
/* Whether the column over (x, y) is open to the sky at height z.
 * Unknown columns count as open. */
bool hta_weather_open(const hta_weather *w, float x, float y, float z);

/* A thunder clap that has just arrived: its loudness 0..1. */
bool hta_weather_thunder(hta_weather *w, float *gain);

/* How the weather wants the fog: a colour to lean toward (weight 0..1),
 * and a density multiplier. */
void hta_weather_atmosphere(const hta_weather *w, float color[3], float *weight, float *density_mul);

const char *hta_weather_name(hta_weather_kind k);
bool        hta_weather_from_name(const char *name, hta_weather_kind *out);

#endif
