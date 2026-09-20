#include "particle.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Halo's own, and what the player and the projectiles fall at. */
#define PART_GRAVITY 3.4f

/* Halo's `air friction` is 200 for muzzle smoke and 900 for a spent
 * casing. Those are not units of ours, so they are divided into a
 * per-second damping. The divisor is chosen so smoke keeps the 1.6/s that
 * looked right before any of this was read from tags -- it preserves what
 * was already verified and scales everything else against it. */
#define PART_DRAG_SCALE 125.0f

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
    return hta_particles_add_marker(p, c, bitmaps, effect_tag_id, NULL);
}

uint32_t hta_particles_add_marker(hta_particles *p, const hta_cache *c,
                                  const hta_resource_map *bitmaps,
                                  uint32_t effect_tag_id, const char *marker)
{
    if (!p || !c || !effect_tag_id) return HTA_PART_NO_RECIPE;
    if (p->recipe_count >= HTA_PART_RECIPES) return HTA_PART_NO_RECIPE;
    if (!p->mesh.textures) {
        p->mesh.textures = (hta_bsp_texture *)calloc(HTA_PART_TYPES,
                                                     sizeof(hta_bsp_texture));
        if (!p->mesh.textures) return HTA_PART_NO_RECIPE;
    }

    /* An effect already added is the same recipe; impacts share heavily.
     * A marker-filtered add is a different recipe from the whole effect,
     * so it is keyed apart. */
    uint32_t key = effect_tag_id ^ (marker ? 0x5BF03635u : 0u);
    for (uint32_t r = 0; r < p->recipe_count; r++)
        if (p->recipe[r].effect_id == key) return r;

    uint32_t n = hta_effect_particle_count(c, effect_tag_id);
    if (!n) return HTA_PART_NO_RECIPE;

    hta_particle_recipe rec;
    memset(&rec, 0, sizeof(rec));
    rec.effect_id = key;

    for (uint32_t i = 0; i < n && rec.emit_count < HTA_PART_EMITS; i++) {
        hta_effect_particle ep;
        if (!hta_effect_particle_at(c, effect_tag_id, i, &ep)) continue;
        /* Underwater and space variants are a different effect entirely,
         * and a first-person-only particle belongs to the viewmodel. */
        if (ep.create_in != HTA_FX_IN_ANY && ep.create_in != HTA_FX_IN_AIR) continue;
        if (ep.create == HTA_FX_CAM_FIRST) continue;
        if (ep.count_max <= 0) continue;        /* never spawns */
        if (marker && strcmp(ep.marker, marker) != 0) continue;

        /* Share a type with anything already using this bitmap IN THIS
         * COLOUR. Same art tinted differently is a different type. */
        uint32_t tint = hta_tint_pack(ep.tint);
        uint32_t t = ~0u;
        for (uint32_t k = 0; k < p->type_count; k++)
            if (p->type[k].bitmap_id == ep.bitmap_id &&
                p->type[k].tint == tint) { t = k; break; }
        if (t == ~0u) {
            if (p->type_count >= HTA_PART_TYPES) continue;
            uint32_t tex = hta_mesh_intern_bitmap_tinted(&p->mesh, c, bitmaps,
                                                         ep.bitmap_id, 0, tint);
            if (tex == ~0u) continue;
            t = p->type_count;
            p->type[t].bitmap_id = ep.bitmap_id;
            p->type[t].tint = tint;
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
        em->gravity    = ep.gravity;
        em->drag       = ep.drag / PART_DRAG_SCALE;
        em->elasticity = ep.elasticity;
        em->collides   = ep.collides;
    }
    if (!rec.emit_count) return HTA_PART_NO_RECIPE;

    p->recipe[p->recipe_count] = rec;
    return p->recipe_count++;
}

bool hta_particles_build(hta_particles *p, char *err, size_t errlen)
{
    if (!p || !p->type_count) return false;

    /* One slot per particle, grouped by type so a type is one submesh.
     * The fixed pool is shared out here: a load with two types gets deep
     * pools, a load with seventeen gets shallow ones, and the geometry is
     * the same either way. */
    p->per_type = HTA_PART_MAX / p->type_count;
    if (p->per_type > HTA_PART_PER_TYPE) p->per_type = HTA_PART_PER_TYPE;
    if (p->per_type < HTA_PART_PER_TYPE_MIN) p->per_type = HTA_PART_PER_TYPE_MIN;
    uint32_t slots = p->type_count * p->per_type;
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
        sm->first_index = t * p->per_type * 6u;
        sm->index_count = p->per_type * 6u;
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
        if (want > (int)p->per_type) want = (int)p->per_type;

        uint32_t base = t * p->per_type;
        for (int spawned = 0, slot = 0;
             spawned < want && slot < (int)p->per_type; slot++) {
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
            q->gravity = em->gravity;
            q->drag = em->drag;
            q->elasticity = em->elasticity;
            q->collides = em->collides;
            q->alive = true;
            spawned++;
        }
    }
}

void hta_particles_update(hta_particles *p, const hta_collision *col,
                          const hta_camera *cam, float dt)
{
    if (!p || !p->loaded) return;
    if (dt < 0.0f) dt = 0.0f;

    /* Billboards face the camera, so the quad's axes are the camera's. */
    float right[3] = { 1.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 0.0f, 1.0f };
    if (cam) { hta_camera_right(cam, right); hta_camera_up(cam, up); }

    for (uint32_t t = 0; t < p->type_count; t++) {
        for (uint32_t s = 0; s < p->per_type; s++) {
            uint32_t slot = t * p->per_type + s;
            hta_particle *q = &p->live[slot];
            if (!q->alive) { hide_slot(p, slot); continue; }

            q->age += dt;
            if (q->age >= q->life) { q->alive = false; hide_slot(p, slot); continue; }

            float damp = 1.0f - q->drag * dt;
            if (damp < 0.0f) damp = 0.0f;

            float step[3];
            for (int k = 0; k < 3; k++) step[k] = q->vel[k] * dt;

            /* Brass and debris bounce off the world; smoke drifts through
             * it. Which is which is the particle's own physics flag. */
            if (q->collides && col) {
                float len = sqrtf(step[0]*step[0] + step[1]*step[1] +
                                  step[2]*step[2]);
                if (len > 1e-5f) {
                    float ray[3] = { step[0]/len, step[1]/len, step[2]/len };
                    float t = 0.0f, hit[3], nrm[3];
                    if (hta_collision_ray(col, q->pos, ray, len, &t, hit, nrm)) {
                        float vn = q->vel[0]*nrm[0] + q->vel[1]*nrm[1] +
                                   q->vel[2]*nrm[2];
                        for (int k = 0; k < 3; k++) {
                            /* Reflect, and keep only what the tag's
                             * elasticity says survives the bounce. */
                            q->vel[k] = (q->vel[k] - 2.0f * vn * nrm[k])
                                      * q->elasticity;
                            q->pos[k] = hit[k] + nrm[k] * 0.01f;
                        }
                        step[0] = step[1] = step[2] = 0.0f;
                    }
                }
            }

            for (int k = 0; k < 3; k++) {
                q->pos[k] += step[k];
                q->vel[k] *= damp;
            }
            /* Signed: a casing is -1.0 and falls, plasma residue is +0.05
             * and drifts up. */
            q->vel[2] += q->gravity * PART_GRAVITY * dt;

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

/* ParticleSystem: particle types at +92, each 128 bytes. Within a type the
 * states are at +104 (192 bytes each) and the particle states at +116 (376).
 * These were found by probing rather than by walking the definition, which
 * drifts by a couple of bytes in this family. */
#define PCTL_TYPES             92u
#define PCTL_TYPE_SIZE        128u
#define PCTL_TYPE_RADIUS       44u
#define PCTL_TYPE_STATES      104u
#define PCTL_STATE_SIZE       192u
#define PCTL_STATE_DURATION    32u   /* float bounds */
#define PCTL_STATE_RATE        88u   /* particles a second */
#define PCTL_TYPE_PSTATES     116u
#define PCTL_PSTATE_SIZE      376u
#define PCTL_PSTATE_BITMAP     48u   /* TagDependency */
#define PCTL_PSTATE_RADIUS    128u   /* radius multiplier */
#define PCTL_PSTATE_BLEND     226u
/* ColorARGB, alpha first: the colour is the three floats after it. */
#define PCTL_PSTATE_COLOR_LOW  96u
#define PCTL_PSTATE_COLOR_HIGH 112u

uint32_t hta_particles_add_system(hta_particles *p, const hta_cache *c,
                                  const hta_resource_map *bitmaps,
                                  uint32_t pctl_tag_id, float speed)
{
    if (!p || !c || !pctl_tag_id) return HTA_PART_NO_RECIPE;
    if (p->recipe_count >= HTA_PART_RECIPES) return HTA_PART_NO_RECIPE;
    if (!p->mesh.textures) {
        p->mesh.textures = (hta_bsp_texture *)calloc(HTA_PART_TYPES,
                                                     sizeof(hta_bsp_texture));
        if (!p->mesh.textures) return HTA_PART_NO_RECIPE;
    }
    for (uint32_t r = 0; r < p->recipe_count; r++)
        if (p->recipe[r].effect_id == pctl_tag_id) return r;

    int32_t ti = hta_cache_find_tag_by_id(c, pctl_tag_id);
    if (ti < 0) return HTA_PART_NO_RECIPE;
    hta_tag_entry t;
    uint32_t base;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return HTA_PART_NO_RECIPE;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return HTA_PART_NO_RECIPE;

    uint32_t tc = 0, tp = 0, to = 0;
    if (!hta_read_reflexive(c, base + PCTL_TYPES, &tc, &tp) || !tc)
        return HTA_PART_NO_RECIPE;
    if (!hta_cache_ptr_to_offset(c, tp, &to)) return HTA_PART_NO_RECIPE;

    /* The first type is the one attached to the marker; the Trial's flame
     * systems have exactly one, called "flames". */
    uint32_t tb = to;
    float type_radius = 0.0f;
    hta_rd_f32(c, tb + PCTL_TYPE_RADIUS, &type_radius);
    if (!(type_radius > 0.0f)) type_radius = 0.3f;

    float rate = 0.0f, life = 1.0f;
    uint32_t sc = 0, sp = 0, so = 0;
    if (hta_read_reflexive(c, tb + PCTL_TYPE_STATES, &sc, &sp) && sc &&
        hta_cache_ptr_to_offset(c, sp, &so)) {
        hta_rd_f32(c, so + PCTL_STATE_RATE, &rate);
        float d0 = 0.0f, d1 = 0.0f;
        hta_rd_f32(c, so + PCTL_STATE_DURATION, &d0);
        hta_rd_f32(c, so + PCTL_STATE_DURATION + 4u, &d1);
        if (d1 > 0.0f) life = d1;
        else if (d0 > 0.0f) life = d0;
    }
    if (!(rate > 0.0f)) return HTA_PART_NO_RECIPE;

    /* A particle walks through its states as it ages: the flame starts
     * invisible at a tenth of the type's radius and swells to four tenths
     * before it dies. Taking the smallest and largest gives the same growth
     * the existing radius ramp already does. */
    uint32_t pc = 0, pp = 0, po = 0;
    if (!hta_read_reflexive(c, tb + PCTL_TYPE_PSTATES, &pc, &pp) || !pc)
        return HTA_PART_NO_RECIPE;
    if (!hta_cache_ptr_to_offset(c, pp, &po)) return HTA_PART_NO_RECIPE;

    uint32_t bitmap = 0;
    uint16_t blend = HTA_FX_BLEND_ADD;
    float rmin = 1e9f, rmax = 0.0f;
    /* The colour of the states that are actually visible. A `pctl` state is
     * a point on a life curve -- the flamethrower's runs invisible, orange,
     * brighter orange, then dead smoke -- and the two dead ends store all
     * zeros. Averaging those in would drag the flame towards black; the
     * flame's colour is what it is while it is burning. */
    float tint[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t tinted = 0;
    for (uint32_t k = 0; k < pc; k++) {
        uint32_t pb = po + k * PCTL_PSTATE_SIZE;
        float rm = 0.0f;
        hta_rd_f32(c, pb + PCTL_PSTATE_RADIUS, &rm);
        if (rm > 0.0f) {
            if (rm < rmin) rmin = rm;
            if (rm > rmax) rmax = rm;
        }
        {
            float mid[3];
            float sum = 0.0f;
            for (int q = 0; q < 3; q++) {
                float lo = 0.0f, hi = 0.0f;
                hta_rd_f32(c, pb + PCTL_PSTATE_COLOR_LOW  + 4u + 4u * (uint32_t)q, &lo);
                hta_rd_f32(c, pb + PCTL_PSTATE_COLOR_HIGH + 4u + 4u * (uint32_t)q, &hi);
                mid[q] = (lo + hi) * 0.5f;
                sum += mid[q];
            }
            if (sum > 0.0f) {
                for (int q = 0; q < 3; q++) tint[q] += mid[q];
                tinted++;
            }
        }
        if (!bitmap) {
            uint32_t bm = 0;
            hta_rd_u32(c, pb + PCTL_PSTATE_BITMAP + 12u, &bm);
            if (bm && bm != 0xFFFFFFFFu) {
                bitmap = bm;
                hta_rd_u16(c, pb + PCTL_PSTATE_BLEND, &blend);
            }
        }
    }
    if (!bitmap) return HTA_PART_NO_RECIPE;
    if (rmin > rmax) { rmin = 0.1f; rmax = 0.4f; }
    if (tinted) { for (int q = 0; q < 3; q++) tint[q] /= (float)tinted; }
    else        { tint[0] = tint[1] = tint[2] = 1.0f; }
    uint32_t tpack = hta_tint_pack(tint);

    /* Share a type with anything already using this bitmap in this colour. */
    uint32_t ty = ~0u;
    for (uint32_t k = 0; k < p->type_count; k++)
        if (p->type[k].bitmap_id == bitmap && p->type[k].tint == tpack)
            { ty = k; break; }
    if (ty == ~0u) {
        if (p->type_count >= HTA_PART_TYPES) return HTA_PART_NO_RECIPE;
        uint32_t tex = hta_mesh_intern_bitmap_tinted(&p->mesh, c, bitmaps,
                                                     bitmap, 0, tpack);
        if (tex == ~0u) return HTA_PART_NO_RECIPE;
        ty = p->type_count;
        p->type[ty].bitmap_id = bitmap;
        p->type[ty].tint = tpack;
        p->type[ty].tex = tex;
        p->type[ty].blend = (uint8_t)blend;
        uint32_t seqs = hta_bitmap_sequence_count(c, bitmap);
        for (uint32_t s2 = 0; s2 < seqs && p->type[ty].sprite_count < 8; s2++) {
            hta_bitmap_sprite spr;
            if (!hta_bitmap_sprite_at(c, bitmap, s2, &spr)) continue;
            if (spr.bitmap_index != 0) continue;
            if (spr.u1 <= spr.u0 || spr.v1 <= spr.v0) continue;
            p->type[ty].sprite[p->type[ty].sprite_count++] = spr;
        }
        if (!p->type[ty].sprite_count) {
            p->type[ty].sprite[0].bitmap_index = 0;
            p->type[ty].sprite[0].u0 = 0.0f; p->type[ty].sprite[0].u1 = 1.0f;
            p->type[ty].sprite[0].v0 = 0.0f; p->type[ty].sprite[0].v1 = 1.0f;
            p->type[ty].sprite_count = 1;
        }
        p->type_count++;
    }

    hta_particle_recipe rec;
    memset(&rec, 0, sizeof(rec));
    rec.effect_id = pctl_tag_id;
    rec.rate = rate;
    rec.emit_count = 1;
    hta_particle_emit *em = &rec.emit[0];
    em->type = (uint8_t)ty;
    em->count_min = 1; em->count_max = 1;
    em->speed_min = speed * 0.7f;
    em->speed_max = speed;
    em->spread = 0.18f;                 /* a jet, not a cloud */
    em->radius_min = type_radius * rmin;
    em->radius_max = type_radius * rmax;
    em->life = life;
    em->fade_in = life * 0.15f;
    em->fade_out = life * 0.45f;
    em->gravity = 0.05f;                /* flame rises, like its own smoke */
    em->drag = 1.2f;
    em->collides = false;

    p->recipe[p->recipe_count] = rec;
    return p->recipe_count++;
}

void hta_particles_emit(hta_particles *p, uint32_t recipe,
                        const float origin[3], const float dir[3], float dt)
{
    if (!p || !p->loaded || recipe >= p->recipe_count) return;
    hta_particle_recipe *rec = &p->recipe[recipe];
    if (!(rec->rate > 0.0f) || dt <= 0.0f) return;

    rec->accum += rec->rate * dt;
    /* Never let a hitch dump a whole second of flame at once. */
    if (rec->accum > (float)p->per_type) rec->accum = (float)p->per_type;
    while (rec->accum >= 1.0f) {
        rec->accum -= 1.0f;
        hta_particles_burst(p, recipe, origin, dir);
    }
}
