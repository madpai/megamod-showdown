#include "projectile.h"
#include "../asset/model.h"
#include "../asset/effect.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Projectile, which inherits Object(380). All of these reconcile against
 * Invader's definition at 588 bytes. */
#define PROJ_TIMER            444u   /* float bounds, seconds */
#define PROJ_MAX_RANGE        456u
#define PROJ_AIR_GRAVITY      460u
#define PROJ_INITIAL_VELOCITY 484u   /* world units per TICK */
#define PROJ_FINAL_VELOCITY   488u
#define PROJ_EFFECT           428u   /* TagDependency -> effe, the detonation */
#define PROJ_TIMER_STARTS     384u   /* ProjectileDetonationTimerStarts */
#define PROJ_RESPONSES        576u   /* TagReflexive, 160 bytes an entry */
#define PROJ_RESPONSE_SIZE    160u
#define PROJ_RESPONSE_KIND      2u   /* ProjectileResponse: 2 == reflect */
#define PROJ_RESPONSE_REFLECT   2u

/* How much of its speed a grenade keeps off a wall. Halo's projectiles
 * carry no elasticity of their own -- the `pphy` that gives a spent casing
 * its bounce belongs to PARTICLES, not to these -- so this is chosen, and
 * is the fourth invented number in the project. */
#define PROJ_BOUNCE 0.35f
#define OBJ_MODEL              40u   /* TagDependency -> mod2 */

/* Halo's own, and what the player falls at in player.c. */
#define PROJ_GRAVITY 3.4f

void hta_projectiles_init(hta_projectiles *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
}

void hta_projectiles_free(hta_projectiles *p)
{
    if (!p) return;
    hta_bsp_free(&p->mesh);
    free(p->base);
    memset(p, 0, sizeof(*p));
}

uint32_t hta_projectiles_count(const hta_projectiles *p)
{
    if (!p) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < HTA_PROJ_MAX; i++) if (p->live[i].alive) n++;
    return n;
}

/* Collapse every copy onto its own origin: zero area, nothing drawn, and the
 * index buffer stays exactly as uploaded. */
static void hide_all(hta_projectiles *p)
{
    for (uint32_t i = 0; i < p->mesh.vertex_count; i++) {
        p->mesh.vertices[i].pos[0] = 0.0f;
        p->mesh.vertices[i].pos[1] = 0.0f;
        p->mesh.vertices[i].pos[2] = 0.0f;
    }
}

bool hta_projectiles_equip(hta_projectiles *p, const hta_cache *c,
                           const hta_resource_map *bitmaps,
                           const hta_weapon_def *weap, char *err, size_t errlen)
{
    if (!p || !c || !weap) return false;
    return hta_projectiles_equip_projectile(p, c, bitmaps, weap->projectile_id,
                                            err, errlen);
}

static bool equip(hta_projectiles *p, const hta_cache *c,
                  const hta_resource_map *bitmaps, uint32_t projectile_id,
                  bool bare, char *err, size_t errlen);
bool hta_projectiles_equip_projectile(hta_projectiles *p, const hta_cache *c,
                                      const hta_resource_map *bitmaps,
                                      uint32_t projectile_id,
                                      char *err, size_t errlen)
{
    return equip(p, c, bitmaps, projectile_id, false, err, errlen);
}
bool hta_projectiles_equip_any(hta_projectiles *p, const hta_cache *c,
                               const hta_resource_map *bitmaps,
                               uint32_t projectile_id, char *err, size_t errlen)
{
    return equip(p, c, bitmaps, projectile_id, true, err, errlen);
}
static bool equip(hta_projectiles *p, const hta_cache *c,
                  const hta_resource_map *bitmaps, uint32_t projectile_id,
                  bool bare, char *err, size_t errlen)
{
    if (!p || !c) return false;
    hta_bsp_free(&p->mesh);
    free(p->base);
    memset(p, 0, sizeof(*p));

    if (!projectile_id) return false;
    int32_t ti = hta_cache_find_tag_by_id(c, projectile_id);
    if (ti < 0) return false;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return false;
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return false;

    uint32_t model = 0;
    hta_rd_u32(c, base + OBJ_MODEL + 12u, &model);
    /* Most of the roster's rounds are particles, not objects. Nothing to
     * draw is the normal answer, not a failure -- unless the caller wants
     * the round to fly anyway (a tank shell), drawn some other way. */
    if (model == 0xFFFFFFFFu) model = 0;
    if (!model && !bare) return false;

    float v0 = 0.0f, v1 = 0.0f;
    hta_rd_f32(c, base + PROJ_INITIAL_VELOCITY, &v0);
    hta_rd_f32(c, base + PROJ_FINAL_VELOCITY, &v1);
    /* A grenade's own initial velocity is 0.00: in Halo the throw comes
     * from the player. Such a projectile is still perfectly loadable. */
    if (!(v1 > 0.0f)) v1 = v0;

    /* The bang and the char mark. A rocket's decal is 1.25 world units --
     * reusing a bullet hole's 3.5 cm for an explosion is why they did not
     * read as explosions. */
    uint32_t det_fx = 0;
    hta_rd_u32(c, base + PROJ_EFFECT + 12u, &det_fx);
    if (det_fx && det_fx != 0xFFFFFFFFu) {
        p->det_effect = det_fx;
        hta_effect_detonation(c, det_fx, &p->detonation_snd, &p->blast_radius,
                              &p->decal_id);
        hta_effect_damage(c, det_fx, &p->blast_damage_radius, &p->blast_core,
                          &p->blast_damage);
    }

    p->proj_tag_id   = projectile_id;
    p->speed_initial = v0 * HTA_TICKS_PER_SECOND;
    p->speed_final   = v1 * HTA_TICKS_PER_SECOND;
    hta_rd_f32(c, base + PROJ_MAX_RANGE, &p->range);
    hta_rd_f32(c, base + PROJ_AIR_GRAVITY, &p->gravity_scale);
    /* The timer is a range; Halo rolls inside it. The low end is soon
     * enough and keeps this deterministic. */
    hta_rd_f32(c, base + PROJ_TIMER, &p->timer);
    hta_rd_u16(c, base + PROJ_TIMER_STARTS, &p->timer_starts);

    /* Does it bounce or does it go off? Every material the Trial's
     * grenades can hit answers "reflect". */
    {
        uint32_t n = 0, rp = 0, ro = 0;
        if (hta_read_reflexive(c, base + PROJ_RESPONSES, &n, &rp) && n &&
            hta_cache_ptr_to_offset(c, rp, &ro)) {
            uint32_t reflect = 0;
            for (uint32_t m = 0; m < n && m < 33u; m++) {
                uint16_t kind = 0;
                hta_rd_u16(c, ro + m * PROJ_RESPONSE_SIZE + PROJ_RESPONSE_KIND,
                           &kind);
                if (kind == PROJ_RESPONSE_REFLECT) reflect++;
            }
            /* If most surfaces reflect it, it is a thing that bounces. */
            p->bounces = reflect * 2u > n;
        }
    }

    if (!model) {
        /* Flies, collides and goes off; nothing of its own to draw. */
        p->loaded = true;
        return true;
    }
    /* The intern table has to exist before anything can be appended into
     * this mesh -- without it every submesh comes out with no texture and
     * the renderer draws nothing at all. */
    p->mesh.textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
    if (!p->mesh.textures) {
        memset(p, 0, sizeof(*p));
        return false;
    }

    /* HTA_PROJ_MAX copies of the model, all at the origin. Only the first
     * pays for decoding the textures; the rest intern to the same ones. */
    static const float zero3[3] = { 0.0f, 0.0f, 0.0f };
    for (uint32_t i = 0; i < HTA_PROJ_MAX; i++) {
        uint32_t before = p->mesh.vertex_count;
        if (!hta_model_instance(&p->mesh, c, bitmaps, model, zero3, zero3,
                                err, errlen)) {
            hta_bsp_free(&p->mesh);
            memset(p, 0, sizeof(*p));
            return false;
        }
        uint32_t added = p->mesh.vertex_count - before;
        if (i == 0) p->verts_each = added;
        else if (added != p->verts_each) {   /* cannot happen; be sure anyway */
            hta_bsp_free(&p->mesh);
            memset(p, 0, sizeof(*p));
            return false;
        }
    }
    if (!p->verts_each) {
        hta_bsp_free(&p->mesh);
        memset(p, 0, sizeof(*p));
        return false;
    }

    /* Keep the model-space vertices: the mesh's own get rewritten each frame. */
    p->base = (hta_vertex *)malloc((size_t)p->mesh.vertex_count * sizeof(hta_vertex));
    if (!p->base) {
        hta_bsp_free(&p->mesh);
        memset(p, 0, sizeof(*p));
        return false;
    }
    memcpy(p->base, p->mesh.vertices,
           (size_t)p->mesh.vertex_count * sizeof(hta_vertex));

    hide_all(p);
    p->loaded = true;
    return true;
}

int hta_projectiles_fire(hta_projectiles *p, const float origin[3],
                         const float dir[3])
{
    return hta_projectiles_throw(p, origin, dir, 0.0f);
}

int hta_projectiles_throw(hta_projectiles *p, const float origin[3],
                          const float dir[3], float speed)
{
    if (!p || !p->loaded || !origin || !dir) return -1;
    float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (!(len > 1e-6f)) return -1;

    /* Oldest slot when they are all busy: a rocket that has been in the air
     * longest is the one whose loss shows least. */
    uint32_t slot = 0;
    float oldest = -1.0f;
    bool found = false;
    for (uint32_t i = 0; i < HTA_PROJ_MAX; i++) {
        if (!p->live[i].alive) { slot = i; found = true; break; }
        if (p->live[i].age > oldest) { oldest = p->live[i].age; slot = i; }
    }
    (void)found;

    hta_projectile *q = &p->live[slot];
    memset(q, 0, sizeof(*q));
    for (int k = 0; k < 3; k++) {
        q->pos[k] = origin[k];
        q->dir[k] = dir[k] / len;
    }
    q->speed = speed > 0.0f ? speed : 0.0f;
    /* Armed at once, or not until it has bounced or settled.
     *
     * `detonation timer starts` only means anything for something that can
     * bounce. The needler's needles say "when at rest" and never come to
     * rest -- they fly until they hit -- so for a projectile that does not
     * reflect, the countdown runs from launch, which is the 0.75 s a
     * needle lives in Halo. */
    q->fuse = (p->timer > 0.0f &&
               (!p->bounces || p->timer_starts == HTA_PROJ_TIMER_IMMEDIATELY))
            ? p->timer : -1.0f;
    q->alive = true;
    return (int)slot;
}

static void record_blast(hta_projectiles *p, uint32_t slot)
{
    if (p->blast_count >= HTA_PROJ_MAX) return;
    uint32_t b = p->blast_count++;
    for (int k = 0; k < 3; k++) {
        p->blasts[b].pos[k] = p->hit[k];
        p->blasts[b].normal[k] = p->hit_normal[k];
    }
    p->blasts[b].material = p->hit_material;
    p->blasts[b].slot = (uint8_t)slot;
}

/* Speed at a distance flown: Halo ramps the initial velocity to the final
 * one across the projectile's range. */
static float speed_at(const hta_projectiles *p, float travelled)
{
    if (!(p->range > 0.0f)) return p->speed_initial;
    float t = travelled / p->range;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return p->speed_initial + (p->speed_final - p->speed_initial) * t;
}

/* Rotate the model's +X onto `dir`, keeping its up as close to world up as
 * the direction allows -- the same basis the world uses for a placed object,
 * just built from a heading instead of Euler angles. */
static void basis_from_dir(const float dir[3], float m[3][3])
{
    float up[3] = { 0.0f, 0.0f, 1.0f };
    if (fabsf(dir[2]) > 0.999f) { up[0] = 1.0f; up[2] = 0.0f; }

    /* right = up x fwd, then up = fwd x right, both normalised. */
    float r[3] = { up[1]*dir[2] - up[2]*dir[1],
                   up[2]*dir[0] - up[0]*dir[2],
                   up[0]*dir[1] - up[1]*dir[0] };
    float rl = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    if (!(rl > 1e-6f)) { r[0] = 0.0f; r[1] = 1.0f; r[2] = 0.0f; rl = 1.0f; }
    for (int k = 0; k < 3; k++) r[k] /= rl;

    float u[3] = { dir[1]*r[2] - dir[2]*r[1],
                   dir[2]*r[0] - dir[0]*r[2],
                   dir[0]*r[1] - dir[1]*r[0] };

    /* Columns: model +X is forward, +Y is left, +Z is up. */
    for (int k = 0; k < 3; k++) {
        m[k][0] = dir[k];
        m[k][1] = -r[k];
        m[k][2] = u[k];
    }
}

static void pose_one(hta_projectiles *p, uint32_t slot, const hta_projectile *q)
{
    float m[3][3];
    basis_from_dir(q->dir, m);
    uint32_t first = slot * p->verts_each;
    for (uint32_t i = 0; i < p->verts_each; i++) {
        const hta_vertex *src = &p->base[first + i];
        hta_vertex *dst = &p->mesh.vertices[first + i];
        *dst = *src;
        for (int k = 0; k < 3; k++) {
            dst->pos[k] = m[k][0]*src->pos[0] + m[k][1]*src->pos[1]
                        + m[k][2]*src->pos[2] + q->pos[k];
            dst->normal[k] = m[k][0]*src->normal[0] + m[k][1]*src->normal[1]
                           + m[k][2]*src->normal[2];
        }
    }
}

static void hide_one(hta_projectiles *p, uint32_t slot)
{
    uint32_t first = slot * p->verts_each;
    for (uint32_t i = 0; i < p->verts_each; i++) {
        p->mesh.vertices[first + i].pos[0] = 0.0f;
        p->mesh.vertices[first + i].pos[1] = 0.0f;
        p->mesh.vertices[first + i].pos[2] = 0.0f;
    }
}

void hta_projectiles_update(hta_projectiles *p, const hta_collision *col, float dt)
{
    if (!p || !p->loaded) return;
    p->detonated = false;
    p->blast_count = 0;
    if (dt <= 0.0f) dt = 0.0f;

    for (uint32_t i = 0; i < HTA_PROJ_MAX; i++) {
        hta_projectile *q = &p->live[i];
        if (!q->alive) { hide_one(p, i); continue; }

        q->age += dt;

        float speed = q->speed > 0.0f ? q->speed : speed_at(p, q->travelled);
        float step = speed * dt;

        /* Gravity bends the path rather than changing the heading's length:
         * a rocket's scale is 0 and it flies flat, a plasma bolt's is 0.1
         * and it droops. */
        float move[3];
        for (int k = 0; k < 3; k++) move[k] = q->dir[k] * step;
        if (p->gravity_scale > 0.0f) {
            q->fall += PROJ_GRAVITY * p->gravity_scale * dt;
            move[2] -= q->fall * dt;
        }

        float len = sqrtf(move[0]*move[0] + move[1]*move[1] + move[2]*move[2]);
        if (len > 1e-6f) {
            float ray[3] = { move[0]/len, move[1]/len, move[2]/len };
            float t = 0.0f, hit[3], nrm[3];
            uint8_t material = 0;
            hta_collision bare;
            const hta_collision *use = col;
            if (col && q->travelled < q->clear) {
                bare = *col;
                bare.instances = NULL;
                bare.instance_count = 0;
                use = &bare;
            }
            if (use && hta_collision_ray_material(use, q->pos, ray, len,
                                                  &t, hit, nrm, &material)) {
                if (p->bounces) {
                    /* Off the wall, not against it. The fuse starts here if
                     * the tag says "after first bounce", which is what the
                     * frag grenade says. */
                    float vel[3];
                    for (int k = 0; k < 3; k++) vel[k] = move[k] / dt;
                    float vn = vel[0]*nrm[0] + vel[1]*nrm[1] + vel[2]*nrm[2];
                    float out[3];
                    for (int k = 0; k < 3; k++)
                        out[k] = (vel[k] - 2.0f * vn * nrm[k]) * PROJ_BOUNCE;
                    float ol = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
                    for (int k = 0; k < 3; k++) {
                        q->pos[k] = hit[k] + nrm[k] * 0.03f;
                        q->dir[k] = ol > 1e-4f ? out[k] / ol : nrm[k];
                    }
                    q->speed = ol;
                    q->fall = 0.0f;
                    q->bounces++;
                    if (q->fuse < 0.0f && p->timer > 0.0f &&
                        p->timer_starts == HTA_PROJ_TIMER_AFTER_BOUNCE)
                        q->fuse = p->timer;
                    pose_one(p, i, q);
                    continue;
                }
                p->detonated = true;
                for (int k = 0; k < 3; k++) {
                    p->hit[k] = hit[k];
                    p->hit_normal[k] = nrm[k];
                }
                p->hit_material = material;
                record_blast(p, i);
                q->alive = false;
                hide_one(p, i);
                continue;
            }
            for (int k = 0; k < 3; k++) q->pos[k] += move[k];
            /* The direction follows the path once gravity has bent it. */
            if (p->gravity_scale > 0.0f)
                for (int k = 0; k < 3; k++) q->dir[k] = move[k] / len;
            q->travelled += len;
        }

        /* A grenade that has stopped rolling arms itself: the plasma
         * grenade's fuse starts "when at rest". */
        if (q->fuse < 0.0f && p->timer > 0.0f &&
            p->timer_starts == HTA_PROJ_TIMER_AT_REST &&
            q->bounces > 0 && speed < 0.5f)
            q->fuse = p->timer;

        /* The fuse, once it is running. */
        if (q->fuse >= 0.0f) {
            q->fuse -= dt;
            if (q->fuse <= 0.0f) {
                p->detonated = true;
                for (int k = 0; k < 3; k++) {
                    p->hit[k] = q->pos[k];
                    p->hit_normal[k] = (k == 2) ? 1.0f : 0.0f;
                }
                p->hit_material = HTA_MATERIAL_NONE;
                record_blast(p, i);
                q->alive = false;
                hide_one(p, i);
                continue;
            }
        }

        /* Out of range -- the needler's needles expire whether or not they
         * hit anything. A grenade has no range and waits for its fuse. */
        if (p->range > 0.0f && q->travelled >= p->range) {
            q->alive = false;
            hide_one(p, i);
            continue;
        }

        pose_one(p, i, q);
    }
}
