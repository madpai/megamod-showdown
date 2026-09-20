/* Portable voice mixer. No platform headers: the device backend hands it an
 * output buffer, and the host test drives the same call directly.
 *
 * Clips are decoded Trial samples (22050 Hz mono, on this data). The output
 * rate is whatever the device gives us, so each voice carries its own
 * resampling step. Clip PCM is owned by the caller and must outlive the mixer.
 *
 * Threading: hta_audio_play is called from the game thread, hta_audio_mix from
 * the audio callback. They share only a single-producer/single-consumer ring
 * of play requests, so the audio thread never blocks and never allocates.
 */
#ifndef HTA_AUDIO_H
#define HTA_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#define HTA_AUDIO_MAX_CLIPS   64
#define HTA_AUDIO_MAX_VOICES  12
#define HTA_AUDIO_REQ_RING    32   /* power of two */

typedef struct {
    const int16_t *samples;   /* interleaved; not owned */
    uint32_t       frames;
    uint32_t       rate;
    uint8_t        channels;
} hta_audio_clip;

typedef struct {
    uint32_t clip;
    float    gain;
    /* 0 for a one-shot. Non-zero names a continuous sound the caller owns;
     * a request whose clip is HTA_AUDIO_NO_CLIP stops that one. */
    uint32_t loop;
} hta_audio_req;

typedef struct {
    uint32_t clip;
    uint64_t phase;     /* 32.32 fixed point, in source frames */
    uint64_t step;
    float    gain;
    uint32_t loop;      /* 0 for a one-shot voice */
    bool     active;
} hta_audio_voice;

typedef struct {
    hta_audio_clip  clips[HTA_AUDIO_MAX_CLIPS];
    uint32_t        clip_count;

    hta_audio_voice voices[HTA_AUDIO_MAX_VOICES];
    uint32_t        out_rate;
    uint8_t         out_channels;
    float           master_gain;

    hta_audio_req       ring[HTA_AUDIO_REQ_RING];
    _Atomic uint32_t    wr, rd;
    _Atomic uint32_t    dropped;   /* requests lost to a full ring */
    uint32_t            started;   /* voices actually started, for logging */
    uint32_t            stolen;    /* voices cut short to make room */
} hta_audio;

void hta_audio_init(hta_audio *a, uint32_t out_rate, uint8_t out_channels);

/* Registers a clip. Returns its index, or HTA_AUDIO_NO_CLIP if full. */
#define HTA_AUDIO_NO_CLIP 0xFFFFFFFFu
uint32_t hta_audio_add_clip(hta_audio *a, const int16_t *samples, uint32_t frames,
                            uint32_t rate, uint8_t channels);

/* Game thread. Never blocks; drops the request if the ring is full. */
void hta_audio_play(hta_audio *a, uint32_t clip, float gain);

/* Starts a continuous sound, or leaves it running if `id` already sounds --
 * calling this every frame while a trigger is held is the intended use. The
 * id is any non-zero value the caller picks, one per continuous sound.
 *
 * Halo needs this for the flamethrower: its roar is a looping sound attached
 * to the weapon object, not a shot fired once per round. */
void hta_audio_loop(hta_audio *a, uint32_t id, uint32_t clip, float gain);

/* Stops it. Harmless if that id is not playing. */
void hta_audio_loop_stop(hta_audio *a, uint32_t id);

/* Audio thread. Writes `frames` interleaved frames, overwriting `out`. */
void hta_audio_mix(hta_audio *a, int16_t *out, uint32_t frames);

/* Voices currently sounding. For tests and logging, not for the audio thread. */
uint32_t hta_audio_active_voices(const hta_audio *a);

#endif
