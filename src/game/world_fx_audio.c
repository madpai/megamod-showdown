#include "world_fx_audio.h"
#include <math.h>
#include <string.h>

bool hta_wfx_audio_init(hta_wfx_audio *wa, hta_audio *a, uint32_t seed)
{
    if (!wa || !a) return false;
    memset(wa, 0, sizeof(*wa));
    if (!hta_sfx_build(&wa->bank, seed)) return false;
    if (!hta_sfx_register(&wa->bank, a)) { hta_sfx_free(&wa->bank); return false; }
    wa->audio = a;
    wa->rng = seed | 1u;
    wa->volume = 1.0f;
    /* Ours: a crate heard across a room, a barrel across a field. */
    wa->near = 2.0f;
    wa->blast_age = 1e9f;
    wa->far = 60.0f;
    wa->ready = true;
    return true;
}

void hta_wfx_audio_free(hta_wfx_audio *wa)
{
    if (!wa) return;
    hta_wfx_audio_hush(wa);
    hta_sfx_free(&wa->bank);
    memset(wa, 0, sizeof(*wa));
}

void hta_wfx_audio_hush(hta_wfx_audio *wa)
{
    if (!wa || !wa->ready) return;
    hta_audio_loop_stop(wa->audio, HTA_WFX_AUDIO_LOOP_RAIN);
    hta_audio_loop_stop(wa->audio, HTA_WFX_AUDIO_LOOP_WIND);
    wa->rain = wa->wind = 0.0f;
}

bool hta_wfx_audio_choose(const hta_wfx_cue *c, hta_sfx_kind *kind, float *gain)
{
    hta_sfx_kind k;
    float g;
    switch (c->kind) {
    case HTA_WFX_CUE_BREAK:
        k = c->material == HTA_RMAT_METAL ? HTA_SFX_BREAK_METAL :
            c->material == HTA_RMAT_CONCRETE ? HTA_SFX_BREAK_CONCRETE :
            c->material == HTA_RMAT_GLASS ? HTA_SFX_BREAK_GLASS :
            c->material == HTA_RMAT_FLESH ? HTA_SFX_GIB : HTA_SFX_BREAK_WOOD;
        g = 0.9f;
        break;
    case HTA_WFX_CUE_KNOCK:
        k = c->material == HTA_RMAT_METAL ? HTA_SFX_KNOCK_METAL :
            c->material == HTA_RMAT_CONCRETE || c->material == HTA_RMAT_GLASS ? HTA_SFX_KNOCK_STONE :
            c->material == HTA_RMAT_WOOD ? HTA_SFX_KNOCK_WOOD : HTA_SFX_KNOCK_SOFT;
        /* A tap at 1 wu/s is faint, a slam at 6 is full. */
        g = fminf(1.0f, (c->strength - 0.8f) / 5.0f) * 0.55f;
        if (g <= 0.02f) return false;
        break;
    case HTA_WFX_CUE_GIB:
        k = HTA_SFX_GIB;
        g = 0.8f;
        break;
    case HTA_WFX_CUE_BLAST:
        k = HTA_SFX_EXPLOSION;
        g = fminf(1.0f, 0.5f + 0.15f * c->strength);
        break;
    case HTA_WFX_CUE_THUNDER:
        k = HTA_SFX_THUNDER;
        g = fminf(1.0f, fmaxf(0.2f, c->strength));
        break;
    default:
        return false;
    }
    if (kind) *kind = k;
    if (gain) *gain = g;
    return true;
}

static float approach(float v, float to, float rate, float dt)
{
    float k = 1.0f - expf(-rate * dt);
    return v + (to - v) * k;
}

void hta_wfx_audio_update(hta_wfx_audio *wa, hta_world_fx *w, const hta_camera *cam, float dt,
                          bool game_sounds)
{
    if (!wa || !wa->ready || !w || !cam) return;
    float right[3];
    hta_camera_right(cam, right);
    if (wa->knock_gap > 0.0f) wa->knock_gap -= dt;
    wa->blast_age += dt;
    hta_wfx_cue c;
    while (hta_wfx_pop_cue(w, &c)) {
        if (c.echo && game_sounds) continue;
        hta_sfx_kind kind;
        float g, gain, pan;
        if (!hta_wfx_audio_choose(&c, &kind, &g)) continue;
        if (c.kind == HTA_WFX_CUE_BLAST) {
            /* An exploding barrel is reported twice (it broke, and its blast
             * detonated): one bang for blasts this close in place and time. */
            float dx = c.pos[0] - wa->blast_at[0], dy = c.pos[1] - wa->blast_at[1], dz = c.pos[2] - wa->blast_at[2];
            if (wa->blast_age < 0.15f && dx * dx + dy * dy + dz * dz < 2.25f) continue;
            memcpy(wa->blast_at, c.pos, sizeof(wa->blast_at));
            wa->blast_age = 0.0f;
        }
        if (c.kind == HTA_WFX_CUE_KNOCK) {
            if (wa->knock_gap > 0.0f) continue;
            wa->knock_gap = 0.03f;
        }
        if (c.kind == HTA_WFX_CUE_THUNDER) {
            /* Everywhere at once: it comes from the sky, not a point. */
            gain = 1.0f; pan = 0.0f;
        } else {
            /* Blasts carry further than knocks. */
            float far = c.kind == HTA_WFX_CUE_BLAST ? wa->far * 2.0f :
                        c.kind == HTA_WFX_CUE_KNOCK ? wa->far * 0.4f : wa->far;
            if (!hta_sfx_spatial(cam->pos, right, c.pos, wa->near, far, &gain, &pan)) continue;
        }
        uint32_t clip = hta_sfx_clip(&wa->bank, kind, &wa->rng);
        if (clip == HTA_AUDIO_NO_CLIP) continue;
        hta_audio_play_pan(wa->audio, clip, g * gain * wa->volume, pan);
        wa->played++;
    }
    float rain, wind;
    hta_wfx_ambience(w, cam->pos, &rain, &wind);
    wa->rain = approach(wa->rain, rain, 1.5f, dt);
    wa->wind = approach(wa->wind, wind, 0.8f, dt);
    struct { uint32_t id; hta_sfx_kind kind; float g; } loops[2] = {
        { HTA_WFX_AUDIO_LOOP_RAIN, HTA_SFX_RAIN, wa->rain * 0.3f },
        { HTA_WFX_AUDIO_LOOP_WIND, HTA_SFX_WIND, wa->wind * 0.35f } };
    for (int i = 0; i < 2; i++) {
        if (loops[i].g * wa->volume > 0.005f) {
            uint32_t clip = hta_sfx_clip(&wa->bank, loops[i].kind, NULL);
            if (clip != HTA_AUDIO_NO_CLIP)
                hta_audio_loop_ex(wa->audio, loops[i].id, clip, loops[i].g * wa->volume, 0.0f, 1.0f);
        } else {
            hta_audio_loop_stop(wa->audio, loops[i].id);
        }
    }
}
