#include "audio.h"

#include <string.h>
#include <math.h>

void hta_audio_init(hta_audio *a, uint32_t out_rate, uint8_t out_channels)
{
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->out_rate = out_rate ? out_rate : 48000u;
    a->out_channels = (out_channels == 1 || out_channels == 2) ? out_channels : 2u;
    a->master_gain = 1.0f;
    atomic_store(&a->wr, 0u);
    atomic_store(&a->rd, 0u);
    atomic_store(&a->dropped, 0u);
}

uint32_t hta_audio_add_clip(hta_audio *a, const int16_t *samples, uint32_t frames,
                            uint32_t rate, uint8_t channels)
{
    if (!a || !samples || !frames || !rate) return HTA_AUDIO_NO_CLIP;
    if (channels != 1 && channels != 2) return HTA_AUDIO_NO_CLIP;
    if (a->clip_count >= HTA_AUDIO_MAX_CLIPS) return HTA_AUDIO_NO_CLIP;
    uint32_t i = a->clip_count++;
    a->clips[i].samples = samples;
    a->clips[i].frames = frames;
    a->clips[i].rate = rate;
    a->clips[i].channels = channels;
    return i;
}

static void push_ex(hta_audio *a, uint32_t clip, float gain, float pan, uint32_t loop,
                    float pitch);
static void push(hta_audio *a, uint32_t clip, float gain, float pan, uint32_t loop)
{
    push_ex(a, clip, gain, pan, loop, 1.0f);
}
static void push_ex(hta_audio *a, uint32_t clip, float gain, float pan, uint32_t loop,
                    float pitch)
{
    uint32_t w = atomic_load_explicit(&a->wr, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&a->rd, memory_order_acquire);
    if (w - r >= HTA_AUDIO_REQ_RING) {
        /* Full. Dropping a shot is better than blocking the game thread or
         * letting the audio thread read a half-written request. */
        atomic_fetch_add_explicit(&a->dropped, 1u, memory_order_relaxed);
        return;
    }
    a->ring[w & (HTA_AUDIO_REQ_RING - 1u)].clip = clip;
    a->ring[w & (HTA_AUDIO_REQ_RING - 1u)].gain = gain;
    a->ring[w & (HTA_AUDIO_REQ_RING - 1u)].pan = pan;
    a->ring[w & (HTA_AUDIO_REQ_RING - 1u)].loop = loop;
    a->ring[w & (HTA_AUDIO_REQ_RING - 1u)].pitch = pitch > 0.05f && pitch < 8.0f ? pitch : 1.0f;
    atomic_store_explicit(&a->wr, w + 1u, memory_order_release);
}

void hta_audio_play(hta_audio *a, uint32_t clip, float gain)
{
    if (!a || clip >= a->clip_count) return;
    push(a, clip, gain, 0.0f, 0u);
}

void hta_audio_play_pan(hta_audio *a, uint32_t clip, float gain, float pan)
{
    if (!a || clip >= a->clip_count) return;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    push(a, clip, gain, pan, 0u);
}

void hta_audio_loop(hta_audio *a, uint32_t id, uint32_t clip, float gain)
{
    if (!a || !id || clip >= a->clip_count) return;
    push(a, clip, gain, 0.0f, id);
}

void hta_audio_loop_ex(hta_audio *a, uint32_t id, uint32_t clip, float gain,
                       float pan, float pitch)
{
    if (!a || !id || clip >= a->clip_count) return;
    push_ex(a, clip, gain, pan, id, pitch);
}

void hta_audio_loop_stop(hta_audio *a, uint32_t id)
{
    if (!a || !id) return;
    push(a, HTA_AUDIO_NO_CLIP, 0.0f, 0.0f, id);
}

/* A free voice, or else the one with the least left to play -- stealing the
 * sound closest to finishing is the least audible cut. */
static uint32_t pick_voice(hta_audio *a)
{
    for (uint32_t i = 0; i < HTA_AUDIO_MAX_VOICES; i++)
        if (!a->voices[i].active) return i;

    uint32_t best = 0;
    uint64_t least_left = ~0ull;
    for (uint32_t i = 0; i < HTA_AUDIO_MAX_VOICES; i++) {
        const hta_audio_voice *v = &a->voices[i];
        /* A continuous sound has no end to be close to, and cutting one is
         * far more audible than clipping the tail off a gunshot. */
        if (v->loop) continue;
        uint64_t played = v->phase >> 32;
        uint64_t total = a->clips[v->clip].frames;
        uint64_t left = (played >= total) ? 0ull : total - played;
        if (left <= least_left) { least_left = left; best = i; }
    }
    a->stolen++;
    return best;
}

static void drain_requests(hta_audio *a)
{
    uint32_t r = atomic_load_explicit(&a->rd, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&a->wr, memory_order_acquire);
    while (r != w) {
        hta_audio_req req = a->ring[r & (HTA_AUDIO_REQ_RING - 1u)];
        r++;
        if (req.loop) {
            hta_audio_voice *have = NULL;
            for (uint32_t i = 0; i < HTA_AUDIO_MAX_VOICES; i++)
                if (a->voices[i].active && a->voices[i].loop == req.loop)
                    have = &a->voices[i];
            if (req.clip >= a->clip_count) {          /* a stop request */
                if (have) { have->active = false; have->loop = 0u; }
                continue;
            }
            if (have) {                               /* already running */
                if (have->clip == req.clip) {
                    have->gain = req.gain;
                    float t = (req.pan + 1.0f) * 0.25f * 3.14159265f;
                    have->gain_l = cosf(t);
                    have->gain_r = sinf(t);
                    const hta_audio_clip *hc = &a->clips[have->clip];
                    have->step = (uint64_t)((double)(((uint64_t)hc->rate << 32) / (uint64_t)a->out_rate) *
                                            (double)req.pitch);
                    continue;
                }
                have->active = false; have->loop = 0u;
            }
        } else if (req.clip >= a->clip_count) {
            continue;
        }
        const hta_audio_clip *c = &a->clips[req.clip];
        uint32_t vi = pick_voice(a);
        hta_audio_voice *v = &a->voices[vi];
        v->clip = req.clip;
        v->phase = 0;
        v->step = (uint64_t)((double)(((uint64_t)c->rate << 32) / (uint64_t)a->out_rate) *
                             (double)(req.pitch > 0.0f ? req.pitch : 1.0f));
        v->gain = req.gain;
        /* Constant power: a sound panned hard to one side is as loud as one
         * in the middle, which is what keeps a shot sweeping past from
         * dipping in the centre. */
        {
            float t = (req.pan + 1.0f) * 0.25f * 3.14159265f;   /* 0 .. pi/2 */
            v->gain_l = cosf(t);
            v->gain_r = sinf(t);
        }
        v->loop = req.loop;
        v->active = true;
        a->started++;
    }
    atomic_store_explicit(&a->rd, r, memory_order_release);
}

void hta_audio_mix(hta_audio *a, int16_t *out, uint32_t frames)
{
    if (!a || !out) return;
    uint8_t oc = a->out_channels;
    memset(out, 0, (size_t)frames * oc * sizeof(int16_t));
    drain_requests(a);

    for (uint32_t vi = 0; vi < HTA_AUDIO_MAX_VOICES; vi++) {
        hta_audio_voice *v = &a->voices[vi];
        if (!v->active) continue;
        const hta_audio_clip *c = &a->clips[v->clip];
        float g = v->gain * a->master_gain;

        for (uint32_t f = 0; f < frames; f++) {
            uint64_t idx = v->phase >> 32;
            if (idx >= c->frames) {
                if (!v->loop) { v->active = false; break; }
                v->phase %= (uint64_t)c->frames << 32;
                idx = v->phase >> 32;
            }

            /* Linear interpolation between source frames. The last frame has
             * no successor, so hold it rather than reading past the clip. */
            uint32_t i0 = (uint32_t)idx;
            /* The last frame of a loop is followed by its first. */
            uint32_t i1 = (i0 + 1u < c->frames) ? i0 + 1u : (v->loop ? 0u : i0);
            float t = (float)(uint32_t)(v->phase & 0xFFFFFFFFu) / 4294967296.0f;

            for (uint8_t ch = 0; ch < oc; ch++) {
                /* Mono clips feed both output channels. */
                uint8_t sc = (c->channels == 2 && ch < 2) ? ch : 0;
                float s0 = c->samples[(size_t)i0 * c->channels + sc];
                float s1 = c->samples[(size_t)i1 * c->channels + sc];
                float pan = (oc == 2) ? (ch == 0 ? v->gain_l : v->gain_r)
                                      : 0.70710678f;
                float s = (s0 + (s1 - s0) * t) * g * pan * 1.4142136f;

                int32_t acc = out[(size_t)f * oc + ch] + (int32_t)s;
                if (acc > 32767) acc = 32767;
                else if (acc < -32768) acc = -32768;
                out[(size_t)f * oc + ch] = (int16_t)acc;
            }
            v->phase += v->step;
        }
    }
}

uint32_t hta_audio_active_voices(const hta_audio *a)
{
    if (!a) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < HTA_AUDIO_MAX_VOICES; i++)
        if (a->voices[i].active) n++;
    return n;
}
