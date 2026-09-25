/* Procedural sound effects and the world-effects sound cues. No game data,
 * no audio device: the bank's signals are measured, the cues counted, and
 * the mixer driven directly. */
#include "engine/sfx.h"
#include "game/world_fx_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double rms(const int16_t *p, uint32_t n)
{
    double s = 0;
    for (uint32_t i = 0; i < n; i++) s += (double)p[i] * p[i];
    return sqrt(s / (n ? n : 1));
}
/* Sign changes per second: a rough brightness. */
static double zcr(const int16_t *p, uint32_t n)
{
    uint32_t z = 0;
    for (uint32_t i = 1; i < n; i++) z += (p[i - 1] < 0) != (p[i] < 0);
    return (double)z * HTA_SFX_RATE / n;
}

static hta_vertex V[6];
static uint32_t I[6];

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    static hta_sfx_bank b;
    assert(hta_sfx_build(&b, 7));
    printf("bank: %.2f MB\n", (double)b.bytes / 1048576.0);
    assert(b.bytes > 500000 && b.bytes < 4u * 1048576u);
    for (int k = 0; k < HTA_SFX_COUNT; k++) {
        assert(b.variants[k] >= 1);
        for (uint32_t v = 0; v < b.variants[k]; v++) {
            const int16_t *p = b.pcm[k][v];
            uint32_t n = b.frames[k][v];
            int peak = 0;
            for (uint32_t i = 0; i < n; i++) peak = abs(p[i]) > peak ? abs(p[i]) : peak;
            /* Normalised, never clipped, not silent. */
            assert(peak > 15000 && peak < 32767);
            assert(rms(p, n) > 400.0);
            if (v == 0) printf("  %-15s %5.2f s  rms %6.0f  zcr %6.0f/s\n", hta_sfx_name((hta_sfx_kind)k),
                               (double)n / HTA_SFX_RATE, rms(p, n), zcr(p, n));
        }
        /* Variants differ. */
        if (b.variants[k] > 1)
            assert(b.frames[k][0] != b.frames[k][1] || memcmp(b.pcm[k][0], b.pcm[k][1], b.frames[k][0] * 2));
    }
    /* Character: glass is bright, a thud is dark; an explosion outlasts a knock. */
    assert(zcr(b.pcm[HTA_SFX_BREAK_GLASS][0], b.frames[HTA_SFX_BREAK_GLASS][0]) >
           3.0 * zcr(b.pcm[HTA_SFX_KNOCK_SOFT][0], b.frames[HTA_SFX_KNOCK_SOFT][0]));
    assert(zcr(b.pcm[HTA_SFX_THUNDER][0], b.frames[HTA_SFX_THUNDER][0]) < 1000.0);
    assert(b.frames[HTA_SFX_EXPLOSION][0] > 8 * b.frames[HTA_SFX_KNOCK_WOOD][0]);
    /* One-shots end in silence: no click when a voice runs out. */
    for (int k = 0; k < HTA_SFX_RAIN; k++)
        assert(abs(b.pcm[k][0][b.frames[k][0] - 1]) < 64);
    /* Loops wrap without a step: the jump from last to first sample is no
     * bigger than the signal's own sample-to-sample movement. */
    for (int k = HTA_SFX_RAIN; k <= HTA_SFX_WIND; k++) {
        const int16_t *p = b.pcm[k][0];
        uint32_t n = b.frames[k][0];
        double step = 0;
        for (uint32_t i = 1; i < n; i++) step += fabs((double)p[i] - p[i - 1]);
        step /= n - 1;
        assert(fabs((double)p[0] - p[n - 1]) < 4.0 * step + 200.0);
        /* And the two halves are about as loud: no swell at the seam. */
        double a = rms(p, n / 2), c = rms(p + n / 2, n - n / 2);
        assert(a / c > 0.6 && a / c < 1.6);
    }
    /* Same seed, same sound. */
    static hta_sfx_bank b2;
    assert(hta_sfx_build(&b2, 7));
    assert(!memcmp(b.pcm[HTA_SFX_BREAK_WOOD][1], b2.pcm[HTA_SFX_BREAK_WOOD][1], b.frames[HTA_SFX_BREAK_WOOD][1] * 2));
    hta_sfx_free(&b2);

    /* Spatial: louder near, silent far, panned to the side it is on. */
    float ear[3] = { 0, 0, 0 }, right[3] = { 0, -1, 0 }, g, pan;
    assert(hta_sfx_spatial(ear, right, (float[3]){ 0, -10, 0 }, 2, 60, &g, &pan) && pan > 0.9f && g < 0.2f);
    float g2;
    assert(hta_sfx_spatial(ear, right, (float[3]){ 1, 0, 0 }, 2, 60, &g2, &pan) && g2 == 1.0f && fabsf(pan) < 0.01f);
    assert(!hta_sfx_spatial(ear, right, (float[3]){ 70, 0, 0 }, 2, 60, &g, &pan));
    hta_sfx_free(&b);

    /* Cue choices. */
    hta_wfx_cue c;
    hta_sfx_kind k;
    memset(&c, 0, sizeof(c));
    c.kind = HTA_WFX_CUE_BREAK; c.material = HTA_RMAT_GLASS;
    assert(hta_wfx_audio_choose(&c, &k, &g) && k == HTA_SFX_BREAK_GLASS);
    c.kind = HTA_WFX_CUE_KNOCK; c.material = HTA_RMAT_METAL; c.strength = 0.5f;
    assert(!hta_wfx_audio_choose(&c, &k, &g));                  /* too soft to hear */
    c.strength = 6.0f;
    assert(hta_wfx_audio_choose(&c, &k, &g) && k == HTA_SFX_KNOCK_METAL && g > 0.5f);

    /* The director's cues: a floor, two props. */
    const float q[6][3] = { {-40,-40,0}, {40,-40,0}, {40,40,0}, {-40,-40,0}, {40,40,0}, {-40,40,0} };
    for (int i = 0; i < 6; i++) { memset(&V[i], 0, sizeof(V[i])); memcpy(V[i].pos, q[i], 12); I[i] = (uint32_t)i; }
    hta_bsp_mesh m;
    memset(&m, 0, sizeof(m));
    m.vertices = V; m.vertex_count = 6; m.indices = I; m.index_count = 6;
    m.bounds_min[0] = m.bounds_min[1] = -40; m.bounds_max[0] = m.bounds_max[1] = 40; m.bounds_max[2] = 1;
    hta_collision col;
    assert(hta_collision_build(&col, &m));
    hta_gfx_settings s;
    hta_gfx_settings_preset(&s, HTA_QUALITY_MEDIUM);
    static hta_world_fx w;
    assert(hta_wfx_init(&w, &col, &s));
    hta_external_breakable br[2];
    memset(br, 0, sizeof(br));
    br[0].min[0] = 4; br[0].max[0] = 4.6f; br[0].min[1] = -0.3f; br[0].max[1] = 0.3f; br[0].max[2] = 0.6f;
    br[0].material = 3; br[0].health = 10;                       /* a pane of glass */
    br[1] = br[0]; br[1].min[1] = 5; br[1].max[1] = 5.6f; br[1].material = 1; br[1].explosive = true;
    hta_external_map em;
    memset(&em, 0, sizeof(em));
    em.breakables = br; em.breakable_count = 2;
    hta_wfx_load_map(&w, &em, 0);
    hta_camera cam;
    hta_camera_init(&cam);
    cam.pos[2] = 0.6f;
    hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    assert(!hta_wfx_pop_cue(&w, NULL));                          /* whole props: nothing */
    float from[3] = { 0, 0, 0.3f };
    hta_props_damage(&w.props, 0, 100, w.props.props[0].centre, from, &w.rigid, &w.fx);
    hta_props_damage(&w.props, 1, 1000, w.props.props[1].centre, from, &w.rigid, &w.fx);
    hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    int breaks = 0, blasts = 0, glass = 0;
    while (hta_wfx_pop_cue(&w, &c)) {
        breaks += c.kind == HTA_WFX_CUE_BREAK;
        blasts += c.kind == HTA_WFX_CUE_BLAST && !c.echo;
        glass += c.kind == HTA_WFX_CUE_BREAK && c.material == HTA_RMAT_GLASS;
    }
    assert(breaks == 2 && blasts == 1 && glass == 1);
    /* The chunks land: knocks, a few per frame at most. */
    uint32_t most = 0, total = 0;
    for (int i = 0; i < 120; i++) {
        hta_wfx_update(&w, 1.0f / 60.0f, &cam);
        uint32_t n = 0;
        while (hta_wfx_pop_cue(&w, &c)) n += c.kind == HTA_WFX_CUE_KNOCK;
        most = n > most ? n : most; total += n;
    }
    printf("knocks: %u over 2 s, at most %u in a frame\n", total, most);
    assert(total > 0 && most <= HTA_WFX_KNOCKS_PER_FRAME);
    /* A quiet sync (a joiner catching up) breaks them silently. */
    hta_wfx_load_map(&w, &em, 0);
    hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    while (hta_wfx_pop_cue(&w, NULL)) {}
    uint8_t mask[1] = { 3 };
    w.props.remote = true;
    hta_props_apply_mask(&w.props, mask, 2, NULL, NULL);
    hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    while (hta_wfx_pop_cue(&w, &c)) assert(c.kind != HTA_WFX_CUE_BREAK && c.kind != HTA_WFX_CUE_BLAST);
    /* A detonation is an echo: Halo has its own sound for it. */
    hta_game_event d;
    memset(&d, 0, sizeof(d));
    d.kind = HTA_EV_DETONATE; d.pool = -1; d.dir[2] = 1;
    hta_wfx_game_event(&w, &d, NULL);
    assert(hta_wfx_pop_cue(&w, &c) && c.kind == HTA_WFX_CUE_BLAST && c.echo);
    /* Thunder: one cue per clap, however long it waits to be taken. */
    w.weather.thunder_ready = true; w.weather.thunder_gain = 0.7f;
    hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    int thunder = 0;
    while (hta_wfx_pop_cue(&w, &c)) thunder += c.kind == HTA_WFX_CUE_THUNDER;
    assert(thunder == 1);
    /* Ambience: a storm is rain and wind; clear is silence. */
    float rain, wind;
    hta_wfx_choose_weather(&w, HTA_WEATHER_STORM);
    hta_wfx_ambience(&w, cam.pos, &rain, &wind);
    assert(rain > 0.5f && wind > 0.2f);
    hta_wfx_choose_weather(&w, HTA_WEATHER_CLEAR);
    hta_wfx_ambience(&w, cam.pos, &rain, &wind);
    assert(rain == 0.0f && wind == 0.0f);

    /* Through the real mixer: a break is heard, echoes skipped when the
     * game has its own sounds, and a storm's loops run. */
    static hta_audio a;
    hta_audio_init(&a, 48000, 2);
    static hta_wfx_audio wa;
    assert(hta_wfx_audio_init(&wa, &a, 3));
    hta_wfx_choose_weather(&w, HTA_WEATHER_STORM);
    hta_wfx_game_event(&w, &d, NULL);                            /* an echo */
    hta_wfx_audio_update(&wa, &w, &cam, 1.0f / 60.0f, true);
    assert(wa.played == 0);
    hta_wfx_game_event(&w, &d, NULL);
    hta_wfx_audio_update(&wa, &w, &cam, 1.0f / 60.0f, false);    /* the sandbox: no game sounds */
    assert(wa.played == 1);
    for (int i = 0; i < 60; i++) hta_wfx_audio_update(&wa, &w, &cam, 1.0f / 60.0f, false);
    static int16_t out[2 * 4800];
    hta_audio_mix(&a, out, 4800);
    assert(hta_audio_active_voices(&a) >= 2);                   /* the blast and the rain, at least */
    double loud = rms(out, 2 * 4800);
    printf("mixed rms %.0f with %u voices\n", loud, hta_audio_active_voices(&a));
    assert(loud > 300.0);
    hta_wfx_audio_hush(&wa);
    hta_wfx_audio_free(&wa);

    hta_wfx_free(&w);
    hta_collision_free(&col);
    puts("sfx OK");
    return 0;
}
