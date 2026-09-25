#include "props.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Health per material, in the game's damage points (a Spartan has 75 of
 * shield and health together). OURS: a crate takes a few rifle rounds, a
 * concrete block wants a grenade. */
float hta_props_default_health(hta_rigid_material m)
{
    switch (m) {
    case HTA_RMAT_GLASS:    return 5.0f;
    case HTA_RMAT_WOOD:     return 40.0f;
    case HTA_RMAT_METAL:    return 120.0f;
    case HTA_RMAT_CONCRETE: return 200.0f;
    case HTA_RMAT_DIRT:     return 60.0f;
    default:                return 60.0f;
    }
}

bool hta_props_init(hta_props *p, uint32_t cap)
{
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    if (!cap) return true;
    /* Fixed arrays: instances point at props' grids, so nothing may move. */
    p->props = calloc(cap, sizeof(hta_prop));
    p->instances = calloc(cap, sizeof(hta_collision_instance));
    if (!p->props || !p->instances) { hta_props_free(p); return false; }
    p->cap = cap;
    p->chunks_per_break = 10;
    return true;
}

void hta_props_free(hta_props *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->count; i++) {
        hta_collision_free(&p->props[i].coll);
        free(p->props[i].coll_mesh.vertices);
        free(p->props[i].coll_mesh.indices);
    }
    free(p->props);
    free(p->instances);
    memset(p, 0, sizeof(*p));
}

/* A box of twelve triangles in its own space. */
static bool box_mesh(hta_bsp_mesh *m, const float h[3])
{
    memset(m, 0, sizeof(*m));
    m->vertices = calloc(8, sizeof(hta_vertex));
    m->indices = malloc(36 * sizeof(uint32_t));
    if (!m->vertices || !m->indices) return false;
    for (int i = 0; i < 8; i++) {
        m->vertices[i].pos[0] = (i & 1) ? h[0] : -h[0];
        m->vertices[i].pos[1] = (i & 2) ? h[1] : -h[1];
        m->vertices[i].pos[2] = (i & 4) ? h[2] : -h[2];
    }
    static const uint32_t ix[36] = {
        0,2,3, 0,3,1,  4,5,7, 4,7,6,  0,1,5, 0,5,4,
        2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
    };
    memcpy(m->indices, ix, sizeof(ix));
    m->vertex_count = 8;
    m->index_count = 36;
    for (int k = 0; k < 3; k++) { m->bounds_min[k] = -h[k]; m->bounds_max[k] = h[k]; }
    return true;
}

static void place(hta_prop *pr)
{
    hta_collision_instance *in = pr->inst;
    float c = cosf(pr->yaw), s = sinf(pr->yaw);
    const float rot[9] = { c, -s, 0,  s, c, 0,  0, 0, 1 };   /* row-major local->world */
    memcpy(in->rot, rot, sizeof(rot));
    memcpy(in->pos, pr->centre, 12);
    in->radius = sqrtf(pr->half[0]*pr->half[0] + pr->half[1]*pr->half[1] + pr->half[2]*pr->half[2]) + 0.05f;
    in->grid = &pr->coll;
    in->active = !pr->broken;
}

uint32_t hta_props_add(hta_props *p, const float centre[3], const float half[3], float yaw,
                       hta_rigid_material material, float health, uint32_t user)
{
    if (!p || p->count >= p->cap) return UINT32_MAX;
    uint32_t i = p->count;
    hta_prop *pr = &p->props[i];
    memset(pr, 0, sizeof(*pr));
    memcpy(pr->centre, centre, 12);
    for (int k = 0; k < 3; k++) pr->half[k] = half[k] > 0.01f ? half[k] : 0.01f;
    pr->yaw = yaw;
    pr->material = (uint8_t)material;
    pr->max_health = pr->health = health > 0.0f ? health : hta_props_default_health(material);
    pr->user = user;
    if (!box_mesh(&pr->coll_mesh, pr->half) || !hta_collision_build(&pr->coll, &pr->coll_mesh)) {
        free(pr->coll_mesh.vertices); free(pr->coll_mesh.indices);
        memset(pr, 0, sizeof(*pr));
        return UINT32_MAX;
    }
    pr->inst = &p->instances[i];
    place(pr);
    p->count++;
    return i;
}

static void event(hta_props *p, hta_prop_ev_kind k, uint32_t prop, const float pos[3], float dmg, float rad)
{
    if (p->event_count >= HTA_PROP_MAX_EVENTS) return;
    hta_prop_event *e = &p->events[p->event_count++];
    e->kind = k; e->prop = prop;
    memcpy(e->pos, pos, 12);
    e->damage = dmg; e->radius = rad;
}

/* Fills the prop's volume with a grid of chunks and throws them. */
static void shatter(hta_props *p, uint32_t i, const float from[3], float force,
                    hta_rigid_world *w, hta_fx *fx)
{
    hta_prop *pr = &p->props[i];
    float c = cosf(pr->yaw), s = sinf(pr->yaw);
    /* Split the longest axes more: a plank breaks along its length. */
    float longest = fmaxf(pr->half[0], fmaxf(pr->half[1], pr->half[2]));
    int n[3];
    uint32_t budget = p->chunks_per_break ? p->chunks_per_break : 1;
    for (int k = 0; k < 3; k++) {
        float share = pr->half[k] / longest;
        n[k] = (int)ceilf(cbrtf((float)budget) * share);
        if (n[k] < 1) n[k] = 1;
    }
    while ((uint32_t)(n[0] * n[1] * n[2]) > budget * 2) {
        int big = n[0] >= n[1] && n[0] >= n[2] ? 0 : (n[1] >= n[2] ? 1 : 2);
        if (n[big] > 1) n[big]--; else break;
    }
    float cell[3] = { 2 * pr->half[0] / n[0], 2 * pr->half[1] / n[1], 2 * pr->half[2] / n[2] };
    uint32_t seed = (uint32_t)(i * 2654435761u) ^ 0x5bd1e995u;
    if (w) {
        for (int z = 0; z < n[2]; z++) for (int y = 0; y < n[1]; y++) for (int x = 0; x < n[0]; x++) {
            seed = seed * 1664525u + 1013904223u;
            float j = (float)((seed >> 9) & 1023) / 1023.0f;
            float l[3] = { -pr->half[0] + cell[0] * ((float)x + 0.5f),
                           -pr->half[1] + cell[1] * ((float)y + 0.5f),
                           -pr->half[2] + cell[2] * ((float)z + 0.5f) };
            hta_rigid_desc d;
            memset(&d, 0, sizeof(d));
            d.shape = HTA_RIGID_BOX;
            d.material = (hta_rigid_material)pr->material;
            /* A little smaller than its cell, and uneven, so chunks do not
             * start interlocked and do not look like a grid. */
            for (int k = 0; k < 3; k++) d.half[k] = cell[k] * 0.5f * (0.72f + 0.2f * j);
            d.pos[0] = pr->centre[0] + c * l[0] - s * l[1];
            d.pos[1] = pr->centre[1] + s * l[0] + c * l[1];
            d.pos[2] = pr->centre[2] + l[2];
            float yawh = pr->yaw * 0.5f;
            d.rot[0] = cosf(yawh); d.rot[3] = sinf(yawh);
            /* Away from the source, more for the chunks nearest it. */
            float a[3] = { d.pos[0] - from[0], d.pos[1] - from[1], d.pos[2] - from[2] };
            float al = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
            if (al < 1e-3f) { a[0] = 0; a[1] = 0; a[2] = 1; al = 1; }
            float sp = force * (0.6f + 0.8f * j);
            for (int k = 0; k < 3; k++) d.vel[k] = a[k] / al * sp;
            d.vel[2] += 0.8f * force * j;
            d.ang[0] = (j - 0.5f) * 10.0f; d.ang[1] = (0.5f - j) * 8.0f; d.ang[2] = j * 4.0f;
            d.life = 10.0f + 6.0f * j;
            d.fade = 2.0f;
            d.user = 0x70000000u | i;
            hta_rigid_spawn(w, &d);
        }
    }
    if (fx) {
        static const hta_burst burst[HTA_RMAT_COUNT] = {
            HTA_BURST_SPLINTERS, HTA_BURST_SPARKS, HTA_BURST_DUST, HTA_BURST_GLASS,
            HTA_BURST_GORE, HTA_BURST_DUST };
        float up[3] = { 0, 0, 1 };
        hta_burst b = pr->material < HTA_RMAT_COUNT ? burst[pr->material] : HTA_BURST_DUST;
        hta_fx_burst(fx, b, pr->centre, up, 16);
        if (b != HTA_BURST_DUST) hta_fx_burst(fx, HTA_BURST_DUST, pr->centre, up, 6);
    }
}

static bool breaks(hta_props *p, uint32_t i, const float from[3], float force,
                   hta_rigid_world *w, hta_fx *fx)
{
    hta_prop *pr = &p->props[i];
    pr->broken = true;
    pr->inst->active = false;
    pr->respawn_in = pr->respawn_time;
    shatter(p, i, from, force, w, fx);
    event(p, HTA_PROP_EV_BROKE, i, pr->centre, 0, 0);
    if (pr->explosive) event(p, HTA_PROP_EV_EXPLODED, i, pr->centre, pr->blast_damage, pr->blast_radius);
    return true;
}

bool hta_props_damage(hta_props *p, uint32_t i, float amount, const float point[3],
                      const float from[3], hta_rigid_world *w, hta_fx *fx)
{
    if (!p || i >= p->count || amount <= 0.0f) return false;
    hta_prop *pr = &p->props[i];
    if (pr->broken) return false;
    pr->health -= amount;
    if (fx && point) {
        /* Chips fly where it was hit, even if it holds. */
        float d[3] = { from[0] - point[0], from[1] - point[1], from[2] - point[2] };
        static const hta_burst chip[HTA_RMAT_COUNT] = {
            HTA_BURST_SPLINTERS, HTA_BURST_SPARKS, HTA_BURST_DUST, HTA_BURST_GLASS,
            HTA_BURST_BLOOD, HTA_BURST_DUST };
        hta_fx_burst(fx, pr->material < HTA_RMAT_COUNT ? chip[pr->material] : HTA_BURST_DUST, point, d, 3);
    }
    if (pr->health > 0.0f) return false;
    /* Overkill throws harder; a rifle round barely pushes a crate apart. */
    float force = 0.8f + fminf(-pr->health / pr->max_health, 2.0f) * 2.0f;
    return breaks(p, i, from ? from : pr->centre, force, w, fx);
}

void hta_props_blast(hta_props *p, const float centre[3], float damage, float radius,
                     hta_rigid_world *w, hta_fx *fx)
{
    if (!p || radius <= 0.0f || damage <= 0.0f) return;
    for (uint32_t i = 0; i < p->count; i++) {
        hta_prop *pr = &p->props[i];
        if (pr->broken) continue;
        /* Distance to the box's surface, roughly: centre less its size. */
        float d[3] = { pr->centre[0]-centre[0], pr->centre[1]-centre[1], pr->centre[2]-centre[2] };
        float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        float reach = dist - fminf(pr->half[0], fminf(pr->half[1], pr->half[2]));
        if (reach < 0.0f) reach = 0.0f;
        if (reach >= radius) continue;
        float f = 1.0f - reach / radius;
        pr->health -= damage * f;
        if (pr->health <= 0.0f) {
            float force = 1.5f + 5.0f * f;
            breaks(p, i, centre, force, w, fx);
        }
    }
    /* Chunks already lying about get thrown too. */
    if (w) hta_rigid_blast(w, centre, radius, 3.0f + damage * 0.02f);
}

bool hta_props_ray(const hta_props *p, const float o[3], const float dir[3], float max_t,
                   uint32_t *out_prop, float *out_t, float nrm[3])
{
    if (!p) return false;
    bool found = false;
    float best = max_t;
    for (uint32_t i = 0; i < p->count; i++) {
        const hta_prop *pr = &p->props[i];
        if (pr->broken) continue;
        /* Slab test in the prop's own frame. */
        float c = cosf(pr->yaw), s = sinf(pr->yaw);
        float rel[3] = { o[0]-pr->centre[0], o[1]-pr->centre[1], o[2]-pr->centre[2] };
        float lo[3] = { c*rel[0] + s*rel[1], -s*rel[0] + c*rel[1], rel[2] };
        float ld[3] = { c*dir[0] + s*dir[1], -s*dir[0] + c*dir[1], dir[2] };
        float t0 = 0.0f, t1 = best;
        int axis = -1; float sign = 1;
        bool miss = false;
        for (int k = 0; k < 3 && !miss; k++) {
            if (fabsf(ld[k]) < 1e-9f) {
                if (lo[k] < -pr->half[k] || lo[k] > pr->half[k]) miss = true;
                continue;
            }
            float inv = 1.0f / ld[k];
            float a = (-pr->half[k] - lo[k]) * inv, b = (pr->half[k] - lo[k]) * inv;
            float sg = -1.0f;
            if (a > b) { float t = a; a = b; b = t; sg = 1.0f; }
            if (a > t0) { t0 = a; axis = k; sign = sg; }
            if (b < t1) t1 = b;
            if (t0 > t1) miss = true;
        }
        if (miss || axis < 0 || t0 >= best) continue;
        best = t0; found = true;
        if (out_prop) *out_prop = i;
        if (nrm) {
            float ln[3] = { 0, 0, 0 };
            ln[axis] = sign;
            nrm[0] = c*ln[0] - s*ln[1]; nrm[1] = s*ln[0] + c*ln[1]; nrm[2] = ln[2];
        }
    }
    if (found && out_t) *out_t = best;
    return found;
}

void hta_props_update(hta_props *p, float dt)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->count; i++) {
        hta_prop *pr = &p->props[i];
        if (!pr->broken || pr->respawn_time <= 0.0f) continue;
        pr->respawn_in -= dt;
        if (pr->respawn_in > 0.0f) continue;
        pr->broken = false;
        pr->health = pr->max_health;
        pr->inst->active = true;
        event(p, HTA_PROP_EV_RESPAWNED, i, pr->centre, 0, 0);
    }
}

bool hta_props_pop(hta_props *p, hta_prop_event *out)
{
    if (!p || !p->event_count) return false;
    if (out) *out = p->events[0];
    memmove(p->events, p->events + 1, (p->event_count - 1) * sizeof(p->events[0]));
    p->event_count--;
    return true;
}

uint32_t hta_props_instances(const hta_props *p, const hta_collision_instance *others, uint32_t n_others,
                             hta_collision_instance *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; others && i < n_others && n < cap; i++) out[n++] = others[i];
    for (uint32_t i = 0; p && i < p->count && n < cap; i++) out[n++] = p->instances[i];
    return n;
}
