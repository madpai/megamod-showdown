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
    hta_bsp_free(&p->mesh);
    free(p->base);
    memset(p, 0, sizeof(*p));

    if (!weap->projectile_id) return false;
    int32_t ti = hta_cache_find_tag_by_id(c, weap->projectile_id);
    if (ti < 0) return false;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return false;
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return false;

    uint32_t model = 0;
    hta_rd_u32(c, base + OBJ_MODEL + 12u, &model);
    /* Most of the roster's rounds are particles, not objects. Nothing to
     * draw is the normal answer, not a failure. */
    if (!model || model == 0xFFFFFFFFu) return false;

    float v0 = 0.0f, v1 = 0.0f;
    hta_rd_f32(c, base + PROJ_INITIAL_VELOCITY, &v0);
    hta_rd_f32(c, base + PROJ_FINAL_VELOCITY, &v1);
    if (!(v0 > 0.0f)) return false;
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
    }

    p->proj_tag_id   = weap->projectile_id;
    p->speed_initial = v0 * HTA_TICKS_PER_SECOND;
    p->speed_final   = v1 * HTA_TICKS_PER_SECOND;
    hta_rd_f32(c, base + PROJ_MAX_RANGE, &p->range);
    hta_rd_f32(c, base + PROJ_AIR_GRAVITY, &p->gravity_scale);
    /* The timer is a range; Halo rolls inside it. The low end is soon
     * enough and keeps this deterministic. */
    hta_rd_f32(c, base + PROJ_TIMER, &p->timer);

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

void hta_projectiles_fire(hta_projectiles *p, const float origin[3],
                          const float dir[3])
{
    if (!p || !p->loaded || !origin || !dir) return;
    float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (!(len > 1e-6f)) return;

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
    q->alive = true;
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
    if (dt <= 0.0f) dt = 0.0f;

    for (uint32_t i = 0; i < HTA_PROJ_MAX; i++) {
        hta_projectile *q = &p->live[i];
        if (!q->alive) { hide_one(p, i); continue; }

        q->age += dt;

        float speed = speed_at(p, q->travelled);
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
            if (col && hta_collision_ray_material(col, q->pos, ray, len,
                                                  &t, hit, nrm, &material)) {
                p->detonated = true;
                for (int k = 0; k < 3; k++) {
                    p->hit[k] = hit[k];
                    p->hit_normal[k] = nrm[k];
                }
                p->hit_material = material;
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

        /* Out of range, or the detonation timer ran out -- the needler's
         * needles go off after 0.75 s whether or not they hit anything. */
        if ((p->range > 0.0f && q->travelled >= p->range) ||
            (p->timer > 0.0f && q->age >= p->timer)) {
            q->alive = false;
            hide_one(p, i);
            continue;
        }

        pose_one(p, i, q);
    }
}
