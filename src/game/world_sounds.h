/* The world's sound resources in the mixer (X5): what a mover definition's
 * `sound` (a package-backed asset resource, asset/asset_res.h) plays when
 * the mover starts to move (engine/world_entities.h cues). Both platforms
 * make the same calls:
 *
 *     hta_world_sounds_bind(&ws, &audio, &world_ext.assets);   once the world is loaded
 *     hta_world_sounds_play(&ws, &audio, &went, ear, right);   after each hta_went_step
 *     hta_world_sounds_free(&ws);                              once the mixer has stopped
 *
 * Ownership: the mixer's clips are append-only and read on the audio
 * thread (engine/audio.h), so it never borrows PCM from a world that may be
 * unloaded under it. Each DISTINCT sound (by content: rate, channels,
 * frames and every sample) is copied once into this bank and registered as
 * one clip for the bank's life; a later world with the same sound reuses it.
 * The world's own asset table still owns its samples and frees them with
 * the world. At most HTA_WSOUNDS_MAX distinct sounds per bank.
 *
 * Portable C11; no platform headers. */
#ifndef HTA_WORLD_SOUNDS_H
#define HTA_WORLD_SOUNDS_H

#include "../asset/asset_res.h"
#include "../engine/audio.h"
#include "../engine/world_entities.h"
#include <stdbool.h>
#include <stdint.h>

#define HTA_WSOUNDS_MAX   64u
#define HTA_WSOUNDS_NEAR  2.0f     /* wu: full volume within (ours) */
#define HTA_WSOUNDS_FAR   40.0f    /* wu: silent beyond (ours) */

typedef struct {
    uint64_t digest;
    uint32_t rate, channels, frames;
    int16_t *pcm;            /* the bank's copy */
    uint32_t clip;           /* in the mixer */
} hta_wsound_slot;

typedef struct {
    hta_wsound_slot slot[HTA_WSOUNDS_MAX];
    uint32_t slot_count;
    /* The bound world's sounds: asset table index -> mixer clip
     * (HTA_AUDIO_NO_CLIP when it could not be registered). */
    uint32_t clip_of[HTA_RES_MAX];
    uint32_t bound;
    uint32_t played, unheard;   /* diagnostics */
} hta_world_sounds;

/* Registers the world's sounds (reusing any the bank already holds). The
 * count bound. `a` may be NULL (no audio: every sound unbound). */
uint32_t hta_world_sounds_bind(hta_world_sounds *ws, hta_audio *a, const hta_asset_table *assets);
/* How loud and where a sound at `pos` is for a listener at `ear` whose
 * right is `right` (unit): false when out of hearing. For tests. */
bool hta_world_sounds_place(const float pos[3], const float ear[3], const float right[3], float *gain, float *pan);
/* Plays this step's cues (w->cues). Returns how many started. */
uint32_t hta_world_sounds_play(hta_world_sounds *ws, hta_audio *a, const hta_world_entities *w,
                               const float ear[3], const float right[3]);
void hta_world_sounds_free(hta_world_sounds *ws);

#endif
