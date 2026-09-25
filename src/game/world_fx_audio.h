/* The world-effects layer's sound: its cues (breaks, knocks, gibs, blasts,
 * thunder) and its weather (rain, wind) played through the portable mixer
 * with the procedural bank (engine/sfx.h). Both platforms make the same
 * two calls:
 *
 *     hta_wfx_audio_init(&wa, &audio, seed);                 once the mixer exists
 *     hta_wfx_audio_update(&wa, &wfx, &cam, dt, game_sounds); once per frame
 *
 * `game_sounds` says the game already plays its own detonation and wreck
 * sounds (Halo's tags), so cues marked `echo` are skipped. */
#ifndef HTA_WORLD_FX_AUDIO_H
#define HTA_WORLD_FX_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include "../engine/audio.h"
#include "../engine/camera.h"
#include "../engine/sfx.h"
#include "world_fx.h"

/* Loop ids in the mixer, ours: nothing else may use them. */
#define HTA_WFX_AUDIO_LOOP_RAIN 0x5F780001u
#define HTA_WFX_AUDIO_LOOP_WIND 0x5F780002u

typedef struct {
    hta_sfx_bank bank;
    hta_audio   *audio;
    uint32_t     rng;
    float        volume;          /* master for all of it, 0..1 */
    float        near, far;       /* hearing range, wu */
    float        rain, wind;      /* smoothed loop gains */
    float        knock_gap;       /* seconds until another knock may sound */
    float        blast_at[3], blast_age;   /* the last blast heard: one bang per barrel */
    bool         ready;
    uint32_t     played;          /* diagnostics */
} hta_wfx_audio;

bool hta_wfx_audio_init(hta_wfx_audio *wa, hta_audio *a, uint32_t seed);
void hta_wfx_audio_free(hta_wfx_audio *wa);
void hta_wfx_audio_update(hta_wfx_audio *wa, hta_world_fx *w, const hta_camera *cam, float dt,
                          bool game_sounds);
/* Silence the loops (pause, map change). */
void hta_wfx_audio_hush(hta_wfx_audio *wa);

/* Which sound a cue plays and how loud, before distance. For tests. */
bool hta_wfx_audio_choose(const hta_wfx_cue *c, hta_sfx_kind *kind, float *gain);

#endif
