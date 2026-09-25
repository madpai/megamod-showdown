#include "gore.h"
#include <math.h>
#include <string.h>

/* Every number here is OURS, chosen by eye: see HANDOFF.md's ledger. */

bool hta_gibs_should(float blast_fraction, float *strength)
{
    /* A blast doing at least 60% of a full body's worth of damage in the
     * killing blow. A grenade at the rim wounds; one at your feet gibs. */
    if (!(blast_fraction >= 0.6f)) return false;
    if (strength) {
        float s = (blast_fraction - 0.6f) / 1.4f + 0.3f;
        *strength = s > 1.0f ? 1.0f : s;
    }
    return true;
}

typedef struct { float off[3]; float half[3]; bool sphere; } part;

uint32_t hta_gibs_spawn(hta_rigid_world *w, hta_fx *fx, const hta_gib_desc *d, uint32_t level)
{
    if (!w || !d || level == 0) return 0;
    float h = d->height > 0.05f ? d->height : 0.7f;
    float k = h / 0.7f;
    /* A body as rough pieces, offsets from its centre (z up). */
    static const part parts[] = {
        { {  0.00f,  0.00f,  0.28f }, { 0.050f, 0.05f, 0.05f }, true  },  /* head */
        { {  0.00f,  0.00f,  0.10f }, { 0.070f, 0.10f, 0.08f }, false },  /* chest */
        { {  0.00f,  0.00f, -0.04f }, { 0.060f, 0.09f, 0.06f }, false },  /* belly */
        { {  0.00f,  0.14f,  0.10f }, { 0.030f, 0.03f, 0.10f }, false },  /* arms */
        { {  0.00f, -0.14f,  0.10f }, { 0.030f, 0.03f, 0.10f }, false },
        { {  0.00f,  0.06f, -0.22f }, { 0.035f, 0.035f, 0.12f }, false }, /* legs */
        { {  0.00f, -0.06f, -0.22f }, { 0.035f, 0.035f, 0.12f }, false },
    };
    const uint32_t nparts = sizeof(parts) / sizeof(parts[0]);
    uint32_t scraps = level >= 2 ? 6u : 3u;

    float away[3] = { d->pos[0] - d->from[0], d->pos[1] - d->from[1], d->pos[2] - d->from[2] };
    float al = sqrtf(away[0]*away[0] + away[1]*away[1] + away[2]*away[2]);
    if (al < 1e-3f) { away[0] = 0; away[1] = 0; away[2] = 1; al = 1; }
    for (int q = 0; q < 3; q++) away[q] /= al;
    float s = d->strength < 0.1f ? 0.1f : d->strength > 1.0f ? 1.0f : d->strength;

    uint32_t made = 0;
    for (uint32_t i = 0; i < nparts + scraps; i++) {
        hta_rigid_desc b;
        memset(&b, 0, sizeof(b));
        b.material = HTA_RMAT_FLESH;
        uint32_t r = w->rng = w->rng * 1664525u + 1013904223u;
        float jx = (float)((r >> 8) & 255) / 127.5f - 1.0f;
        float jy = (float)((r >> 16) & 255) / 127.5f - 1.0f;
        float jz = (float)((r >> 24) & 255) / 255.0f;
        if (i < nparts) {
            const part *p = &parts[i];
            b.shape = p->sphere ? HTA_RIGID_SPHERE : HTA_RIGID_BOX;
            for (int q = 0; q < 3; q++) { b.half[q] = p->half[q] * k; b.pos[q] = d->pos[q] + p->off[q] * k; }
        } else {
            b.shape = HTA_RIGID_BOX;
            float sz = (0.015f + 0.02f * jz) * k;
            b.half[0] = sz; b.half[1] = sz * 0.8f; b.half[2] = sz * 0.6f;
            for (int q = 0; q < 3; q++) b.pos[q] = d->pos[q];
            b.pos[0] += jx * 0.08f * k; b.pos[1] += jy * 0.08f * k;
        }
        /* Thrown away from the blast and up, scattered, spinning: at
         * most ~4 wu/s (13 m/s) out and ~1 wu (3.5 m) of lift. An earlier
         * 7 wu/s put heads 36 m in the air. */
        float speed = (1.0f + 2.0f * s) * (0.6f + 0.8f * jz);
        b.vel[0] = d->vel[0] + (away[0] + jx * 0.6f) * speed;
        b.vel[1] = d->vel[1] + (away[1] + jy * 0.6f) * speed;
        b.vel[2] = d->vel[2] + (fabsf(away[2]) * 0.3f + 0.35f + jz * 0.3f) * speed;
        b.ang[0] = jx * 14.0f; b.ang[1] = jy * 14.0f; b.ang[2] = (jz - 0.5f) * 10.0f;
        b.life = 12.0f + jz * 6.0f;
        b.fade = 1.5f;
        b.user = HTA_GIB_USER | (i & 0xFFFFu);
        hta_rigid_spawn(w, &b);
        made++;
    }
    if (fx) {
        float up[3] = { away[0] * 0.5f, away[1] * 0.5f, 0.8f };
        hta_fx_burst(fx, HTA_BURST_GORE, d->pos, up, level >= 2 ? 24u : 14u);
    }
    return made;
}

void hta_gibs_update(hta_rigid_world *w, hta_fx *fx, float dt, uint32_t level)
{
    if (!w || !fx || level < 2) return;
    static const float red[4] = { 0.40f, 0.02f, 0.02f, 0.9f };
    /* A hard landing paints the ground. */
    for (uint32_t i = 0; i < w->impact_count; i++) {
        const hta_rigid_impact *im = &w->impacts[i];
        if (im->material != HTA_RMAT_FLESH || im->speed < 1.2f) continue;
        const hta_rigid_body *b = &w->bodies[im->body];
        if ((b->user & 0xFF000000u) != HTA_GIB_USER || b->age > 4.0f) continue;
        float size = 0.06f + fminf(im->speed, 6.0f) * 0.025f;
        hta_fx_splat(fx, im->point, im->normal, size, red, 20.0f);
    }
    /* In flight, and for the first seconds, a chunk drips. */
    for (uint32_t i = 0; i < w->cap; i++) {
        const hta_rigid_body *b = &w->bodies[i];
        if (!b->active || b->asleep || (b->user & 0xFF000000u) != HTA_GIB_USER || b->age > 3.0f) continue;
        float sp2 = b->vel[0]*b->vel[0] + b->vel[1]*b->vel[1] + b->vel[2]*b->vel[2];
        if (sp2 < 0.25f) continue;
        /* About 14 drops a second per chunk, fewer as it bleeds out. */
        float rate = 6.0f * (1.0f - b->age / 3.0f) * fx->density;
        uint32_t r = w->rng = w->rng * 1664525u + 1013904223u;
        if ((float)(r & 0xFFFF) / 65535.0f > rate * dt) continue;
        hta_sprite s;
        memset(&s, 0, sizeof(s));
        memcpy(s.pos, b->pos, 12);
        for (int k = 0; k < 3; k++) s.vel[k] = b->vel[k] * 0.3f;
        s.size0 = 0.012f; s.size1 = 0.008f;
        memcpy(s.color, red, sizeof(red));
        s.life = 1.5f; s.gravity = 1.0f; s.drag = 0.3f;
        s.tile = HTA_TILE_STREAK; s.mode = HTA_SPRITE_STREAK; s.collide = true;
        hta_fx_emit(fx, &s);
    }
}
