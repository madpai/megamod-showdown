#include "particle.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Halo's own, and what the player and the projectiles fall at. */
#define PART_GRAVITY 3.4f
/* Smoke and sparks slow down in air; without this they fly forever in a
 * straight line and read as debris rather than a plume. */
#define PART_DRAG 1.6f

void hta_particles_init(hta_particles *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->rng = 0x9E3779B9u;
}

void hta_particles_free(hta_particles *p)
{
    if (!p) return;
    hta_bsp_free(&p->mesh);
    memset(p, 0, sizeof(*p));
}

uint32_t hta_particles_count(const hta_particles *p)
{
    if (!p) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < HTA_PART_MAX; i++) if (p->live[i].alive) n++;
    return n;
}

static float frand(hta_particles *p)
{
    p->rng = p->rng * 1664525u + 1013904223u;
    return (float)((p->rng >> 8) & 0xFFFFFFu) / (float)0x1000000;
}

static void hide_slot(hta_particles *p, uint32_t slot)
{
    hta_vertex *v = &p->mesh.vertices[slot * 4u];
    for (int k = 0; k < 4; k++) {
        v[k].pos[0] = v[k].pos[1] = v[k].pos[2] = 0.0f;
    }
}

uint32_t hta_particles_add(hta_particles *p, const hta_cache *c,
                           const hta_resource_map *bitmaps,
                           uint32_t effect_tag_id)
{
    if (!p || !c || !effect_tag_id) return HTA_PART_NO_RECIPE;
    if (p->recipe_count >= HTA_PART_RECIPES) return HTA_PART_NO_RECIPE;
    if (!p->mesh.textures) {
        p->mesh.textures = (hta_bsp_texture *)calloc(HTA_PART_TYPES,
                                                     sizeof(hta_bsp_texture));
        if (!p->mesh.textures) return HTA_PART_NO_RECIPE;
    }

    /* An effect already added is the same recipe; impacts share heavily. */
    for (uint32_t r = 0; r < p->recipe_count; r++)
        if (p->recipe[r].effect_id == effect_tag_id) return r;

    uint32_t n = hta_effect_particle_count(c, effect_tag_id);
    if (!n) return HTA_PART_NO_RECIPE;

    hta_particle_recipe rec;
    memset(&rec, 0, sizeof(rec));
    rec.effect_id = effect_tag_id;

    for (uint32_t i = 0; i < n && rec.emit_count < HTA_PART_EMITS; i++) {
        hta_effect_particle ep;
        if (!hta_effect_particle_at(c, effect_tag_id, i, &ep)) continue;
        /* Underwater and space variants are a different effect entirely,
         * and a first-person-only particle belongs to the viewmodel. */
        if (ep.create_in != HTA_FX_IN_ANY && ep.create_in != HTA_FX_IN_AIR) continue;
        if (ep.create == HTA_FX_CAM_FIRST) continue;
        if (ep.count_max <= 0) continue;        /* never spawns */

        /* Share a type with anything already using this bitmap. */
        uint32_t t = ~0u;
        for (uint32_t k = 0; k < p->type_count; k++)
            if (p->type[k].bitmap_id == ep.bitmap_id) { t = k; break; }
        if (t == ~0u) {
            if (p->type_count >= HTA_PART_TYPES) continue;
            uint32_t tex = hta_mesh_intern_bitmap(&p->mesh, c, bitmaps,
                                                  ep.bitmap_id, 0);
            if (tex == ~0u) continue;
            t = p->type_count;
            p->type[t].bitmap_id = ep.bitmap_id;
            p->type[t].tex = tex;
            p->type[t].blend = ep.blend;

            uint32_t seqs = hta_bitmap_sequence_count(c, ep.bitmap_id);
            for (uint32_t sq = 0; sq < seqs && p->type[t].sprite_count < 8; sq++) {
                hta_bitmap_sprite sp;
                if (!hta_bitmap_sprite_at(c, ep.bitmap_id, sq, &sp)) continue;
                if (sp.bitmap_index != 0) continue;   /* the sheet we interned */
                if (sp.u1 <= sp.u0 || sp.v1 <= sp.v0) continue;
                p->type[t].sprite[p->type[t].sprite_count++] = sp;
            }
            if (!p->type[t].sprite_count) {
                /* Not a sheet: the whole bitmap is the particle. */
                p->type[t].sprite[0].bitmap_index = 0;
                p->type[t].sprite[0].u0 = 0.0f; p->type[t].sprite[0].u1 = 1.0f;
                p->type[t].sprite[0].v0 = 0.0f; p->type[t].sprite[0].v1 = 1.0f;
                p->type[t].sprite_count = 1;
            }

            /* A particle bitmap with no alpha channel cannot be
             * alpha-blended: it would draw as an opaque square of its own
             * black background. The rocket's lens flare is exactly that --
             * `flares_generic` is a format with no alpha, so every texel
             * decodes to 255 -- and Halo reads such art additively, where
             * black contributes nothing. Measured, not assumed. */
            if (p->type[t].blend != HTA_FX_BLEND_ADD) {
                const hta_bsp_texture *bt = &p->mesh.textures[tex];
                bool has_alpha = false;
                if (bt->rgba) {
                    size_t px = (size_t)bt->width * bt->height;
                    for (size_t q = 0; q < px; q++)
                        if (bt->rgba[q * 4u + 3u] < 250u) { has_alpha = true; break; }
                }
                if (!has_alpha) p->type[t].blend = HTA_FX_BLEND_ADD;
            }
            p->type_count++;
        }

        hta_particle_emit *em = &rec.emit[rec.emit_count++];
        em->type = (uint8_t)t;
        em->count_min = ep.count_min;
        em->count_max = ep.count_max;
        em->speed_min = ep.speed_min;
        em->speed_max = ep.speed_max;
        em->spread    = ep.spread > 0.0f ? ep.spread : 1.0f;
        em->radius_min = ep.radius_min;
        em->radius_max = ep.radius_max > ep.radius_min ? ep.radius_max
                                                       : ep.radius_min;
        em->life      = ep.lifespan > 0.01f ? ep.lifespan : 0.5f;
        em->fade_in   = ep.fade_in;
        em->fade_out  = ep.fade_out;
    }
    if (!rec.emit_count) return HTA_PART_NO_RECIPE;

    p->recipe[p->recipe_count] = rec;
    return p->recipe_count++;
}

bool hta_particles_build(hta_particles *p, char *err, size_t errlen)
{
    if (!p || !p->type_count) return false;

    /* One slot per particle, grouped by type so a type is one submesh. */
    uint32_t slots = p->type_count * HTA_PART_PER_TYPE;
    p->mesh.vertices = (hta_vertex *)calloc((size_t)slots * 4u, sizeof(hta_vertex));
    p->mesh.indices  = (uint32_t *)calloc((size_t)slots * 6u, sizeof(uint32_t));
    p->mesh.submeshes = (hta_submesh *)calloc(p->type_count, sizeof(hta_submesh));
    if (!p->mesh.vertices || !p->mesh.indices || !p->mesh.submeshes) {
        if (err) snprintf(err, errlen, "out of memory building particles");
        return false;
    }
    for (uint32_t s = 0; s < slots; s++) {
        uint32_t b = s * 4u, *idx = &p->mesh.indices[s * 6u];
        idx[0] = b; idx[1] = b + 1; idx[2] = b + 2;
        idx[3] = b; idx[4] = b + 2; idx[5] = b + 3;
    }
    for (uint32_t t = 0; t < p->type_count; t++) {
        hta_submesh *sm = &p->mesh.submeshes[t];
        hta_submesh_init(sm);
        sm->first_index = t * HTA_PART_PER_TYPE * 6u;
        sm->index_count = HTA_PART_PER_TYPE * 6u;
        sm->albedo_tex = p->type[t].tex;
        sm->draw_mode = (p->type[t].blend == HTA_FX_BLEND_ADD) ? HTA_DRAW_ADD
                                                               : HTA_DRAW_ALPHA;
    }
    p->mesh.vertex_count = slots * 4u;
    p->mesh.index_count  = slots * 6u;
    p->mesh.submesh_count = p->type_count;
    for (uint32_t s = 0; s < slots; s++) hide_slot(p, s);
    p->loaded = true;
    return true;
}

/* A unit vector inside a cone of half-angle `spread` around `dir`. */
static void cone_dir(hta_particles *p, const float dir[3], float spread,
                     float out[3])
{
    /* Any two axes perpendicular to dir. */
    float up[3] = { 0.0f, 0.0f, 1.0f };
    if (fabsf(dir[2]) > 0.99f) { up[0] = 1.0f; up[2] = 0.0f; }
    float r[3] = { up[1]*dir[2] - up[2]*dir[1],
                   up[2]*dir[0] - up[0]*dir[2],
                   up[0]*dir[1] - up[1]*dir[0] };
    float rl = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    if (!(rl > 1e-6f)) { out[0] = dir[0]; out[1] = dir[1]; out[2] = dir[2]; return; }
    for (int k = 0; k < 3; k++) r[k] /= rl;
    float u[3] = { dir[1]*r[2] - dir[2]*r[1],
                   dir[2]*r[0] - dir[0]*r[2],
                   dir[0]*r[1] - dir[1]*r[0] };

    float ang = frand(p) * 6.2831853f;
    float t = spread * sqrtf(frand(p));        /* uniform over the cap */
    float st = sinf(t), ct = cosf(t);
    float ca = cosf(ang), sa = sinf(ang);
    for (int k = 0; k < 3; k++)
        out[k] = dir[k]*ct + (r[k]*ca + u[k]*sa) * st;
}

void hta_particles_burst(hta_particles *p, uint32_t recipe,
                         const float origin[3], const float dir[3])
{
    if (!p || !p->loaded || !origin) return;
    if (recipe >= p->recipe_count) return;
    float d[3] = { 0.0f, 0.0f, 1.0f };
    if (dir) {
        float l = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (l > 1e-6f) for (int k = 0; k < 3; k++) d[k] = dir[k] / l;
    }

    const hta_particle_recipe *rec = &p->recipe[recipe];
    for (uint32_t ei = 0; ei < rec->emit_count; ei++) {
        const hta_particle_emit *em = &rec->emit[ei];
        uint32_t t = em->type;
        if (t >= p->type_count) continue;
        int lo = em->count_min > 0 ? em->count_min : 0;
        int hi = em->count_max > lo ? em->count_max : lo;
        int want = lo + (int)(frand(p) * (float)(hi - lo + 1));
        if (want > (int)HTA_PART_PER_TYPE) want = (int)HTA_PART_PER_TYPE;

        uint32_t base = t * HTA_PART_PER_TYPE;
        for (int spawned = 0, slot = 0;
             spawned < want && slot < (int)HTA_PART_PER_TYPE; slot++) {
            hta_particle *q = &p->live[base + (uint32_t)slot];
            if (q->alive) continue;            /* a burst never cuts one short */
            memset(q, 0, sizeof(*q));
            for (int k = 0; k < 3; k++) q->pos[k] = origin[k];
            float v[3];
            cone_dir(p, d, em->spread, v);
            float speed = em->speed_min + (em->speed_max - em->speed_min) * frand(p);
            for (int k = 0; k < 3; k++) q->vel[k] = v[k] * speed;
            q->life = em->life;
            q->radius0 = em->radius_min;
            q->radius1 = em->radius_max;
            q->fade_in = em->fade_in;
            q->fade_out = em->fade_out;
            q->alive = true;
            spawned++;
        }
    }
}

void hta_particles_update(hta_particles *p, const hta_camera *cam, float dt)
{
    if (!p || !p->loaded) return;
    if (dt < 0.0f) dt = 0.0f;

    /* Billboards face the camera, so the quad's axes are the camera's. */
    float right[3] = { 1.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 0.0f, 1.0f };
    if (cam) { hta_camera_right(cam, right); hta_camera_up(cam, up); }

    for (uint32_t t = 0; t < p->type_count; t++) {
        for (uint32_t s = 0; s < HTA_PART_PER_TYPE; s++) {
            uint32_t slot = t * HTA_PART_PER_TYPE + s;
            hta_particle *q = &p->live[slot];
            if (!q->alive) { hide_slot(p, slot); continue; }

            q->age += dt;
            if (q->age >= q->life) { q->alive = false; hide_slot(p, slot); continue; }

            float drag = 1.0f - PART_DRAG * dt;
            if (drag < 0.0f) drag = 0.0f;
            for (int k = 0; k < 3; k++) {
                q->pos[k] += q->vel[k] * dt;
                q->vel[k] *= drag;
            }
            q->vel[2] -= PART_GRAVITY * dt * 0.25f;   /* smoke barely falls */

            float life = q->age / q->life;
            float radius = q->radius0 + (q->radius1 - q->radius0) * life;

            /* Halo fades a particle in and out over its own tagged times;
             * the additive blend does the rest, so shrinking to nothing at
             * the end is the fade. */
            float a = 1.0f;
            if (q->fade_in > 0.0f && q->age < q->fade_in)
                a = q->age / q->fade_in;
            if (q->fade_out > 0.0f && q->life - q->age < q->fade_out)
                a *= (q->life - q->age) / q->fade_out;
            if (a < 0.0f) a = 0.0f;
            radius *= a;

            hta_vertex *v = &p->mesh.vertices[slot * 4u];
            const hta_bitmap_sprite *sp =
                &p->type[t].sprite[slot % p->type[t].sprite_count];
            const float corner[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
            const float uvs[4][2] = { {sp->u0, sp->v1}, {sp->u1, sp->v1},
                                      {sp->u1, sp->v0}, {sp->u0, sp->v0} };
            for (int k = 0; k < 4; k++) {
                for (int a2 = 0; a2 < 3; a2++)
                    v[k].pos[a2] = q->pos[a2] +
                                   (right[a2] * corner[k][0] +
                                    up[a2]    * corner[k][1]) * radius;
                v[k].normal[0] = 0.0f; v[k].normal[1] = 0.0f; v[k].normal[2] = 1.0f;
                v[k].uv[0] = uvs[k][0];
                v[k].uv[1] = uvs[k][1];
                v[k].lm_uv[0] = v[k].lm_uv[1] = 0.0f;
            }
        }
    }
}
