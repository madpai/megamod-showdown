#include "sfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define R ((float)HTA_SFX_RATE)
#define TAU 6.28318531f

/* ---- small DSP kit --------------------------------------------------------- */

typedef struct { uint32_t s; } rng_t;
static uint32_t rnext(rng_t *r) { r->s ^= r->s << 13; r->s ^= r->s >> 17; r->s ^= r->s << 5; return r->s; }
static float frand(rng_t *r) { return (float)(rnext(r) >> 8) * (1.0f / 16777216.0f); }        /* 0..1 */
static float noise(rng_t *r) { return frand(r) * 2.0f - 1.0f; }
static float range(rng_t *r, float a, float b) { return a + (b - a) * frand(r); }

/* RBJ biquad. */
typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } biquad;
static void bq_set(biquad *q, int type, float fc, float Q)
{
    if (fc > R * 0.45f) fc = R * 0.45f;
    if (fc < 10.0f) fc = 10.0f;
    float w = TAU * fc / R, c = cosf(w), s = sinf(w), al = s / (2.0f * Q);
    float b0, b1, b2, a0 = 1.0f + al, a1 = -2.0f * c, a2 = 1.0f - al;
    if (type == 0) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; }          /* low-pass */
    else if (type == 1) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }  /* high-pass */
    else { b0 = al; b1 = 0; b2 = -al; }                                         /* band-pass */
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}
static void bq_init(biquad *q, int type, float fc, float Q) { memset(q, 0, sizeof(*q)); bq_set(q, type, fc, Q); }
static float bq(biquad *q, float x)
{
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}

typedef struct { float *d; uint32_t n; } buf_t;

static uint32_t at(float t) { return t <= 0.0f ? 0u : (uint32_t)(t * R); }

/* A damped sinusoid, as one mode of a struck object. */
static void mode(buf_t *b, float t0, float f, float amp, float tau)
{
    uint32_t i0 = at(t0);
    uint32_t len = at(tau * 7.0f);
    float w = TAU * f / R, ph = 0.0f, k = expf(-1.0f / (tau * R)), e = amp;
    for (uint32_t i = i0; i < b->n && i < i0 + len; i++) {
        b->d[i] += e * sinf(ph);
        ph += w; e *= k;
    }
}

/* A falling-pitch mode: a thump or a wet pop. */
static void drop(buf_t *b, float t0, float f0, float f1, float amp, float tau)
{
    uint32_t i0 = at(t0), len = at(tau * 7.0f);
    float ph = 0.0f, e = amp, k = expf(-1.0f / (tau * R));
    for (uint32_t i = 0; i0 + i < b->n && i < len; i++) {
        float f = f1 + (f0 - f1) * expf(-(float)i / (tau * 0.6f * R));
        b->d[i0 + i] += e * sinf(ph);
        ph += TAU * f / R; e *= k;
    }
}

/* Filtered noise with an attack and an exponential decay. */
static void burst(buf_t *b, rng_t *r, float t0, int type, float fc, float Q, float amp,
                  float attack, float tau, float len)
{
    biquad q;
    bq_init(&q, type, fc, Q);
    uint32_t i0 = at(t0), n = at(len > 0 ? len : tau * 7.0f), na = at(attack);
    float k = expf(-1.0f / (tau * R)), e = amp;
    for (uint32_t i = 0; i0 + i < b->n && i < n; i++) {
        float env = i < na ? (float)i / (float)na : 1.0f;
        b->d[i0 + i] += bq(&q, noise(r)) * e * env;
        if (i >= na) e *= k;
    }
}

/* A struck plate or can: inharmonic partials. */
static void clang(buf_t *b, rng_t *r, float t0, float f0, float amp, float tau)
{
    static const float ratio[5] = { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f };
    static const float gain[5] = { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f };
    static const float life[5] = { 1.0f, 0.7f, 0.45f, 0.3f, 0.2f };
    for (int k = 0; k < 5; k++) {
        float f = f0 * ratio[k] * range(r, 0.98f, 1.02f);
        if (f > R * 0.45f) break;
        mode(b, t0, f, amp * gain[k], tau * life[k] * range(r, 0.8f, 1.2f));
    }
    burst(b, r, t0, 1, 3000.0f, 0.7f, amp * 0.6f, 0.0005f, 0.004f, 0);
}

static void seamless(buf_t *b, uint32_t fade)
{
    /* b holds n + fade samples; fold the tail over the head (equal power)
     * and keep n: the loop point is then inaudible. */
    uint32_t n = b->n - fade;
    for (uint32_t i = 0; i < fade; i++) {
        float t = (float)i / (float)fade;
        b->d[i] = b->d[i] * sqrtf(t) + b->d[n + i] * sqrtf(1.0f - t);
    }
    b->n = n;
}

/* ---- the sounds ------------------------------------------------------------ */

static void make_wood(buf_t *b, rng_t *r)
{
    drop(b, 0.0f, range(r, 140, 180), range(r, 80, 100), 0.7f, 0.05f);
    int cracks = 6 + (int)(frand(r) * 5);
    float t = 0.0f;
    for (int i = 0; i < cracks; i++) {
        float a = 1.0f - 0.08f * (float)i;
        burst(b, r, t, 2, range(r, 1100, 3200), 1.4f, a, 0.0005f, range(r, 0.006f, 0.014f), 0);
        mode(b, t, range(r, 350, 900), a * 0.35f, range(r, 0.025f, 0.05f));
        t += range(r, 0.01f, 0.05f) * (1.0f + 0.3f * (float)i);
    }
    /* Splinters settling. */
    for (int i = 0; i < 14; i++)
        burst(b, r, range(r, 0.15f, 0.55f), 2, range(r, 1500, 4000), 2.0f, range(r, 0.05f, 0.15f),
              0.0003f, 0.004f, 0);
}

static void make_metal(buf_t *b, rng_t *r)
{
    float f0 = range(r, 170, 320);
    clang(b, r, 0.0f, f0, 0.8f, range(r, 0.8f, 1.2f));
    drop(b, 0.0f, 120, 70, 0.5f, 0.06f);
    clang(b, r, range(r, 0.1f, 0.25f), f0 * range(r, 1.2f, 1.5f), 0.35f, 0.5f);
    for (int i = 0; i < 4; i++)
        clang(b, r, range(r, 0.2f, 0.7f), range(r, 600, 1400), range(r, 0.05f, 0.15f), 0.15f);
}

static void make_concrete(buf_t *b, rng_t *r)
{
    drop(b, 0.0f, range(r, 90, 110), range(r, 50, 65), 0.9f, 0.08f);
    burst(b, r, 0.0f, 0, 1800, 0.7f, 0.6f, 0.001f, 0.06f, 0);
    /* The crunch: a decaying hail of grit. */
    float t = 0.0f;
    while (t < 0.6f) {
        float a = 0.5f * expf(-t / 0.18f);
        burst(b, r, t, 2, range(r, 600, 2600), 1.2f, a * range(r, 0.5f, 1.0f), 0.0003f,
              range(r, 0.002f, 0.006f), 0);
        t += range(r, 0.002f, 0.012f) * (1.0f + t * 6.0f);
    }
}

static void make_glass(buf_t *b, rng_t *r)
{
    burst(b, r, 0.0f, 1, 2800, 0.7f, 0.9f, 0.0005f, 0.05f, 0);
    burst(b, r, 0.0f, 2, 5000, 0.8f, 0.5f, 0.0005f, 0.12f, 0);
    int shards = 40 + (int)(frand(r) * 25);
    for (int i = 0; i < shards; i++) {
        float t = 0.01f - 0.22f * logf(1.0f - frand(r) * 0.97f);    /* front-loaded */
        float a = range(r, 0.08f, 0.35f) * expf(-t / 0.5f);
        float f = range(r, 2400, 7500);
        mode(b, t, f, a, range(r, 0.02f, 0.1f));
        mode(b, t, f * range(r, 2.1f, 2.5f), a * 0.4f, range(r, 0.01f, 0.05f));
    }
}

static void make_gib(buf_t *b, rng_t *r)
{
    burst(b, r, 0.0f, 0, 500, 0.8f, 1.0f, 0.003f, 0.07f, 0);
    burst(b, r, 0.0f, 2, 1400, 1.0f, 0.35f, 0.001f, 0.03f, 0);
    int pops = 3 + (int)(frand(r) * 3);
    for (int i = 0; i < pops; i++)
        drop(b, range(r, 0.0f, 0.25f), range(r, 250, 500), range(r, 90, 150), range(r, 0.2f, 0.45f), 0.025f);
    for (int i = 0; i < 6; i++)                                      /* spatter */
        burst(b, r, range(r, 0.1f, 0.4f), 0, 900, 0.8f, range(r, 0.05f, 0.15f), 0.001f, 0.02f, 0);
}

static void make_knock(buf_t *b, rng_t *r, hta_sfx_kind k)
{
    switch (k) {
    case HTA_SFX_KNOCK_WOOD:
        mode(b, 0, range(r, 400, 800), 0.8f, 0.03f);
        mode(b, 0, range(r, 900, 1500), 0.4f, 0.02f);
        burst(b, r, 0, 2, 2000, 1.0f, 0.4f, 0.0003f, 0.003f, 0);
        break;
    case HTA_SFX_KNOCK_METAL:
        clang(b, r, 0, range(r, 600, 1100), 0.7f, 0.25f);
        break;
    case HTA_SFX_KNOCK_STONE:
        burst(b, r, 0, 2, range(r, 1200, 2000), 1.0f, 0.8f, 0.0003f, 0.015f, 0);
        mode(b, 0, range(r, 180, 260), 0.5f, 0.02f);
        break;
    default:
        burst(b, r, 0, 0, 300, 0.7f, 1.0f, 0.002f, 0.035f, 0);
        break;
    }
}

static void make_explosion(buf_t *b, rng_t *r)
{
    burst(b, r, 0.0f, 1, 200, 0.7f, 1.0f, 0.0003f, 0.008f, 0);              /* the crack */
    /* The boom: noise whose brightness falls away. */
    biquad q;
    bq_init(&q, 0, 1500, 0.9f);
    uint32_t n = b->n;
    for (uint32_t i = 0; i < n; i++) {
        float t = (float)i / R;
        if ((i & 63) == 0) bq_set(&q, 0, 60.0f + 1500.0f * expf(-t / 0.18f), 0.9f);
        float env = (t < 0.005f ? t / 0.005f : 1.0f) * (0.75f * expf(-t / 0.35f) + 0.25f * expf(-t / 1.2f));
        b->d[i] += bq(&q, noise(r)) * 2.2f * env;
    }
    drop(b, 0.0f, 55, 32, 0.9f, 0.35f);                                     /* sub */
    for (int i = 0; i < 12; i++)                                            /* debris crackle */
        burst(b, r, range(r, 0.08f, 0.9f), 2, range(r, 800, 3000), 1.5f, range(r, 0.05f, 0.2f),
              0.0003f, 0.006f, 0);
}

static void make_thunder(buf_t *b, rng_t *r, bool close)
{
    if (close) {
        burst(b, r, 0.0f, 1, 400, 0.7f, 0.9f, 0.0005f, 0.12f, 0);
        drop(b, 0.0f, 70, 35, 0.6f, 0.4f);
    }
    biquad q;
    bq_init(&q, 0, range(r, 140, 220), 0.8f);
    /* Rolls: bumps of loudness scattered through the first few seconds. */
    float roll_t[10], roll_w[10], roll_a[10];
    int rolls = 6 + (int)(frand(r) * 4);
    for (int k = 0; k < rolls; k++) {
        roll_t[k] = range(r, 0.05f, 3.2f);
        roll_w[k] = range(r, 0.25f, 0.8f);
        roll_a[k] = range(r, 0.4f, 1.0f);
    }
    for (uint32_t i = 0; i < b->n; i++) {
        float t = (float)i / R, env = 0.15f * expf(-t / 2.0f);
        for (int k = 0; k < rolls; k++) {
            float x = (t - roll_t[k]) / roll_w[k];
            env += roll_a[k] * expf(-x * x);
        }
        float fade = t > (float)b->n / R - 0.5f ? ((float)b->n / R - t) / 0.5f : 1.0f;
        b->d[i] += bq(&q, noise(r)) * env * fade * (t < 0.05f ? t / 0.05f : 1.0f);
    }
}

static void make_rain(buf_t *b, rng_t *r)
{
    biquad hp, lp;
    bq_init(&hp, 1, 700, 0.7f);
    bq_init(&lp, 0, 4500, 0.7f);
    for (uint32_t i = 0; i < b->n; i++) b->d[i] = bq(&lp, bq(&hp, noise(r))) * 0.35f;
    uint32_t drops = (uint32_t)((float)b->n / R * 350.0f);
    for (uint32_t k = 0; k < drops; k++)
        mode(b, frand(r) * (float)b->n / R, range(r, 1800, 5500), range(r, 0.02f, 0.12f), range(r, 0.002f, 0.006f));
}

static void make_wind(buf_t *b, rng_t *r, float loop_seconds)
{
    biquad q;
    bq_init(&q, 2, 400, 1.2f);
    /* Whole cycles over the loop, so the gusts wrap too. */
    float g1 = TAU * 1.0f / loop_seconds, g2 = TAU * 3.0f / loop_seconds, ph = frand(r) * TAU;
    for (uint32_t i = 0; i < b->n; i++) {
        float t = (float)i / R;
        float gust = 0.6f + 0.25f * sinf(g1 * t + ph) + 0.15f * sinf(g2 * t);
        if ((i & 63) == 0) bq_set(&q, 2, 220.0f + 420.0f * gust, 1.2f);
        b->d[i] = bq(&q, noise(r)) * gust;
    }
}

/* ---- the bank -------------------------------------------------------------- */

static const struct { const char *name; float seconds; uint32_t variants; bool loop; float peak; } SPEC[HTA_SFX_COUNT] = {
    [HTA_SFX_BREAK_WOOD]     = { "break_wood",     0.75f, 3, false, 0.85f },
    [HTA_SFX_BREAK_METAL]    = { "break_metal",    1.4f,  3, false, 0.85f },
    [HTA_SFX_BREAK_CONCRETE] = { "break_concrete", 0.85f, 3, false, 0.85f },
    [HTA_SFX_BREAK_GLASS]    = { "break_glass",    1.1f,  3, false, 0.8f },
    [HTA_SFX_GIB]            = { "gib",            0.55f, 3, false, 0.85f },
    [HTA_SFX_KNOCK_WOOD]     = { "knock_wood",     0.2f,  3, false, 0.7f },
    [HTA_SFX_KNOCK_METAL]    = { "knock_metal",    0.6f,  3, false, 0.6f },
    [HTA_SFX_KNOCK_STONE]    = { "knock_stone",    0.18f, 3, false, 0.7f },
    [HTA_SFX_KNOCK_SOFT]     = { "knock_soft",     0.2f,  3, false, 0.7f },
    [HTA_SFX_EXPLOSION]      = { "explosion",      2.6f,  3, false, 0.95f },
    [HTA_SFX_THUNDER]        = { "thunder",        5.5f,  2, false, 0.9f },
    [HTA_SFX_RAIN]           = { "rain",           3.0f,  1, true,  0.6f },
    [HTA_SFX_WIND]           = { "wind",           4.0f,  1, true,  0.6f },
};

const char *hta_sfx_name(hta_sfx_kind k) { return k < HTA_SFX_COUNT ? SPEC[k].name : "?"; }

bool hta_sfx_build(hta_sfx_bank *b, uint32_t seed)
{
    if (!b) return false;
    memset(b, 0, sizeof(*b));
    for (int k = 0; k < HTA_SFX_COUNT; k++) {
        for (uint32_t v = 0; v < SPEC[k].variants; v++) {
            rng_t r = { (seed ? seed : 0x5EEDu) * 2654435761u + (uint32_t)k * 97u + v * 7919u + 1u };
            uint32_t fade = SPEC[k].loop ? at(0.35f) : 0u;
            buf_t buf = { NULL, at(SPEC[k].seconds) + fade };
            buf.d = calloc(buf.n, sizeof(float));
            if (!buf.d) { hta_sfx_free(b); return false; }
            switch ((hta_sfx_kind)k) {
            case HTA_SFX_BREAK_WOOD: make_wood(&buf, &r); break;
            case HTA_SFX_BREAK_METAL: make_metal(&buf, &r); break;
            case HTA_SFX_BREAK_CONCRETE: make_concrete(&buf, &r); break;
            case HTA_SFX_BREAK_GLASS: make_glass(&buf, &r); break;
            case HTA_SFX_GIB: make_gib(&buf, &r); break;
            case HTA_SFX_EXPLOSION: make_explosion(&buf, &r); break;
            case HTA_SFX_THUNDER: make_thunder(&buf, &r, v == 0); break;
            case HTA_SFX_RAIN: make_rain(&buf, &r); break;
            case HTA_SFX_WIND: make_wind(&buf, &r, SPEC[k].seconds); break;
            default: make_knock(&buf, &r, (hta_sfx_kind)k); break;
            }
            if (fade) seamless(&buf, fade);
            /* A short fade-in and -out: no click at either end of a one-shot. */
            if (!SPEC[k].loop) {
                uint32_t e = at(0.004f);
                for (uint32_t i = 0; i < e && i < buf.n; i++) buf.d[buf.n - 1 - i] *= (float)i / (float)e;
            }
            float peak = 1e-9f;
            for (uint32_t i = 0; i < buf.n; i++) peak = fmaxf(peak, fabsf(buf.d[i]));
            float g = SPEC[k].peak * 32767.0f / peak;
            int16_t *pcm = malloc(buf.n * sizeof(int16_t));
            if (!pcm) { free(buf.d); hta_sfx_free(b); return false; }
            for (uint32_t i = 0; i < buf.n; i++) {
                float s = buf.d[i] * g;
                pcm[i] = (int16_t)(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : lrintf(s));
            }
            free(buf.d);
            b->pcm[k][v] = pcm;
            b->frames[k][v] = buf.n;
            b->bytes += buf.n * sizeof(int16_t);
            b->variants[k]++;
        }
    }
    for (int k = 0; k < HTA_SFX_COUNT; k++)
        for (uint32_t v = 0; v < HTA_SFX_VARIANTS; v++) b->clip[k][v] = HTA_AUDIO_NO_CLIP;
    return true;
}

void hta_sfx_free(hta_sfx_bank *b)
{
    if (!b) return;
    for (int k = 0; k < HTA_SFX_COUNT; k++)
        for (uint32_t v = 0; v < HTA_SFX_VARIANTS; v++) free(b->pcm[k][v]);
    memset(b, 0, sizeof(*b));
}

bool hta_sfx_register(hta_sfx_bank *b, hta_audio *a)
{
    if (!b || !a) return false;
    bool ok = true;
    for (int k = 0; k < HTA_SFX_COUNT; k++)
        for (uint32_t v = 0; v < b->variants[k]; v++) {
            b->clip[k][v] = hta_audio_add_clip(a, b->pcm[k][v], b->frames[k][v], HTA_SFX_RATE, 1);
            ok &= b->clip[k][v] != HTA_AUDIO_NO_CLIP;
        }
    b->registered = ok;
    return ok;
}

uint32_t hta_sfx_clip(const hta_sfx_bank *b, hta_sfx_kind k, uint32_t *rng)
{
    if (!b || k >= HTA_SFX_COUNT || !b->variants[k]) return HTA_AUDIO_NO_CLIP;
    uint32_t v = 0;
    if (rng && b->variants[k] > 1) {
        *rng = *rng * 1664525u + 1013904223u;
        v = (*rng >> 16) % b->variants[k];
    }
    return b->clip[k][v];
}

bool hta_sfx_spatial(const float ear[3], const float right[3], const float p[3],
                     float near, float far, float *gain, float *pan)
{
    float d[3] = { p[0] - ear[0], p[1] - ear[1], p[2] - ear[2] };
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist >= far) return false;
    float g = 1.0f;
    if (dist > near) g = (near / dist) * (1.0f - dist / far);
    float pn = 0.0f;
    if (dist > 0.01f && right) {
        pn = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / dist;
        /* Near sounds sit less hard in one ear: nobody is that close. */
        if (dist < near) pn *= dist / near;
    }
    if (gain) *gain = g;
    if (pan) *pan = pn;
    return g > 0.001f;
}
