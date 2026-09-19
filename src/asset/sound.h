/* `snd!` tags: find a permutation and decode it to 16-bit PCM.
 *
 * On Trial every sample byte lives in sounds.map, not in the cache — the same
 * external-resource pattern bitmaps use, with resource type 2 instead of 1.
 * The user supplies that file; nothing audio ships in the APK.
 *
 * Layout from Invader's sound.json. Sound 164, SoundPitchRange 72,
 * SoundPermutation 124 — all three reconcile, which is the check that catches
 * a mis-ordered field.
 */
#ifndef HTA_SOUND_H
#define HTA_SOUND_H

#include "cache.h"
#include "bitmap.h"   /* hta_resource_map */

/* SoundFormat */
#define HTA_SND_FMT_PCM16   0u
#define HTA_SND_FMT_XBOX    1u   /* what every Trial sound effect is */
#define HTA_SND_FMT_IMA     2u
#define HTA_SND_FMT_OGG     3u   /* announcer dialogue; not decoded yet */

typedef struct {
    int16_t *samples;      /* interleaved; free with hta_pcm_free */
    uint32_t frame_count;  /* frames, i.e. samples per channel */
    uint32_t sample_rate;
    uint8_t  channels;
} hta_pcm;

void hta_pcm_free(hta_pcm *p);

typedef struct {
    uint16_t format;         /* HTA_SND_FMT_* at tag level */
    uint16_t sample_rate;    /* 0 = 22050, 1 = 44100 */
    uint16_t channels;       /* 0 = mono, 1 = stereo */
    uint16_t sound_class;
    uint32_t permutations;   /* in pitch range 0 */
} hta_sound_info;

/* Reads the tag header and counts permutations in pitch range 0. */
bool hta_sound_info_load(const hta_cache *c, uint32_t tag_id,
                         hta_sound_info *out, char *err, size_t errlen);

/* Decode one permutation of pitch range 0 to PCM.
 * `sounds` must be an opened sounds.map (resource type 2). */
bool hta_sound_decode(const hta_cache *c, const hta_resource_map *sounds,
                      uint32_t tag_id, uint32_t permutation,
                      hta_pcm *out, char *err, size_t errlen);

/* Xbox ADPCM: 36 bytes per channel per block -> 64 frames.
 * Exposed so a test can drive it without a cache file. */
#define HTA_XBOX_ADPCM_BLOCK   36u
#define HTA_XBOX_ADPCM_FRAMES  64u
bool hta_xbox_adpcm_decode(const uint8_t *src, uint32_t src_len, uint8_t channels,
                           int16_t *dst, uint32_t dst_frames);

#endif
