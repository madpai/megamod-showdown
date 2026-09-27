/* The world's sound resources in the mixer (world_sounds.h). */
#include "world_sounds.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int32_t find(const hta_world_sounds *ws, const hta_asset_sound *s)
{
    size_t bytes = (size_t)s->frames * s->channels * sizeof(int16_t);
    for (uint32_t i = 0; i < ws->slot_count; i++) {
        const hta_wsound_slot *o = &ws->slot[i];
        if (o->digest == s->digest && o->rate == s->rate && o->channels == s->channels && o->frames == s->frames &&
            !memcmp(o->pcm, s->samples, bytes)) return (int32_t)i;
    }
    return -1;
}

uint32_t hta_world_sounds_bind(hta_world_sounds *ws, hta_audio *a, const hta_asset_table *assets)
{
    if (!ws) return 0;
    ws->bound = 0;
    for (uint32_t i = 0; i < sizeof(ws->clip_of) / sizeof(ws->clip_of[0]); i++) ws->clip_of[i] = HTA_AUDIO_NO_CLIP;
    for (uint32_t i = 0; a && assets && i < assets->sound_count && i < sizeof(ws->clip_of) / sizeof(ws->clip_of[0]); i++) {
        const hta_asset_sound *s = &assets->sound[i];
        if (!s->samples) continue;
        int32_t k = find(ws, s);
        if (k < 0 && ws->slot_count < HTA_WSOUNDS_MAX) {
            size_t bytes = (size_t)s->frames * s->channels * sizeof(int16_t);
            int16_t *copy = malloc(bytes);
            if (!copy) continue;
            memcpy(copy, s->samples, bytes);
            uint32_t clip = hta_audio_add_clip(a, copy, s->frames, s->rate, (uint8_t)s->channels);
            if (clip == HTA_AUDIO_NO_CLIP) { free(copy); continue; }
            hta_wsound_slot *o = &ws->slot[ws->slot_count];
            o->digest = s->digest; o->rate = s->rate; o->channels = s->channels; o->frames = s->frames;
            o->pcm = copy; o->clip = clip;
            k = (int32_t)ws->slot_count++;
        }
        if (k < 0) continue;
        ws->clip_of[i] = ws->slot[k].clip;
        ws->bound++;
    }
    return ws->bound;
}

bool hta_world_sounds_place(const float pos[3], const float ear[3], const float right[3], float *gain, float *pan)
{
    float d[3] = { pos[0] - ear[0], pos[1] - ear[1], pos[2] - ear[2] };
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dist < HTA_WSOUNDS_FAR)) return false;
    float g = dist <= HTA_WSOUNDS_NEAR ? 1.0f : 1.0f - (dist - HTA_WSOUNDS_NEAR) / (HTA_WSOUNDS_FAR - HTA_WSOUNDS_NEAR);
    *gain = g * g;
    *pan = dist > 0.01f ? (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / dist : 0.0f;
    if (*pan > 1.0f) *pan = 1.0f;
    if (*pan < -1.0f) *pan = -1.0f;
    return true;
}

bool hta_world_sounds_play_at(hta_world_sounds *ws, hta_audio *a, uint32_t sound, const float pos[3], const float ear[3],
                              const float right[3])
{
    if (!ws) return false;
    uint32_t clip = sound < sizeof(ws->clip_of) / sizeof(ws->clip_of[0]) ? ws->clip_of[sound] : HTA_AUDIO_NO_CLIP;
    float gain, pan;
    if (!a || clip == HTA_AUDIO_NO_CLIP || !hta_world_sounds_place(pos, ear, right, &gain, &pan)) { ws->unheard++; return false; }
    hta_audio_play_pan(a, clip, gain, pan);
    ws->played++;
    return true;
}

uint32_t hta_world_sounds_play(hta_world_sounds *ws, hta_audio *a, const hta_world_entities *w,
                               const float ear[3], const float right[3])
{
    uint32_t n = 0;
    for (uint32_t i = 0; ws && w && w->loaded && i < w->cue_count; i++)
        n += hta_world_sounds_play_at(ws, a, w->cues[i].sound, w->cues[i].pos, ear, right);
    return n;
}

void hta_world_sounds_free(hta_world_sounds *ws)
{
    if (!ws) return;
    for (uint32_t i = 0; i < ws->slot_count; i++) free(ws->slot[i].pcm);
    memset(ws, 0, sizeof(*ws));
}
