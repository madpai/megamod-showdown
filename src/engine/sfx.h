/* Procedural sound effects: every sound the world-effects layer needs,
 * synthesised at start-up from noise, resonators and envelopes. No assets,
 * so an imported map's crates, windows and barrels, the debris, the gibs and
 * the weather sound the same on every build and every game.
 *
 * Portable: PCM out, nothing else. hta_sfx_register hands the clips to the
 * mixer (engine/audio.h); game/world_fx_audio decides what plays when.
 *
 * Each kind has a few variants synthesised from different seeds, so a
 * burst of breaks does not machine-gun one sample. */
#ifndef HTA_SFX_H
#define HTA_SFX_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "audio.h"

typedef enum {
    HTA_SFX_BREAK_WOOD = 0,   /* splintering crack and a thump */
    HTA_SFX_BREAK_METAL,      /* clang with a ringing tail */
    HTA_SFX_BREAK_CONCRETE,   /* gritty crunch, low thud */
    HTA_SFX_BREAK_GLASS,      /* crash and a shower of tinkles */
    HTA_SFX_GIB,              /* wet burst */
    HTA_SFX_KNOCK_WOOD,       /* debris landing, by material */
    HTA_SFX_KNOCK_METAL,
    HTA_SFX_KNOCK_STONE,
    HTA_SFX_KNOCK_SOFT,
    HTA_SFX_EXPLOSION,        /* crack, boom, rolling tail */
    HTA_SFX_THUNDER,          /* long rumble */
    HTA_SFX_RAIN,             /* seamless loop */
    HTA_SFX_WIND,             /* seamless loop */
    HTA_SFX_COUNT
} hta_sfx_kind;

#define HTA_SFX_VARIANTS 3u
#define HTA_SFX_RATE 22050u    /* the mixer resamples to the device */

typedef struct {
    int16_t *pcm[HTA_SFX_COUNT][HTA_SFX_VARIANTS];
    uint32_t frames[HTA_SFX_COUNT][HTA_SFX_VARIANTS];
    uint32_t variants[HTA_SFX_COUNT];                 /* how many were made */
    uint32_t clip[HTA_SFX_COUNT][HTA_SFX_VARIANTS];   /* mixer clip ids */
    bool     registered;
    size_t   bytes;
} hta_sfx_bank;

/* Synthesises the whole bank (about 2 MB, tens of milliseconds). */
bool hta_sfx_build(hta_sfx_bank *b, uint32_t seed);
void hta_sfx_free(hta_sfx_bank *b);
/* Adds every clip to the mixer. The PCM must outlive the mixer's use. */
bool hta_sfx_register(hta_sfx_bank *b, hta_audio *a);
/* A registered clip of `kind`, a variant picked with `rng`. */
uint32_t hta_sfx_clip(const hta_sfx_bank *b, hta_sfx_kind kind, uint32_t *rng);
const char *hta_sfx_name(hta_sfx_kind kind);

/* Where a sound at `at` sits for a listener at `ear` facing with `right`:
 * a gain (inverse distance past `near`, reaching zero at `far`) and a
 * constant-power pan. False when it is too far to hear. */
bool hta_sfx_spatial(const float ear[3], const float right[3], const float at[3],
                     float near, float far, float *gain, float *pan);

#endif
