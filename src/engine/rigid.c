#include "rigid.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ small math */

static float dot3(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void cross3(const float a[3], const float b[3], float o[3])
{
    float x = a[1]*b[2] - a[2]*b[1], y = a[2]*b[0] - a[0]*b[2], z = a[0]*b[1] - a[1]*b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
static float len3(const float a[3]) { return sqrtf(dot3(a, a)); }

/* Quaternion (w,x,y,z) to a row-major 3x3, body -> world. */
static void quat_mat(const float q[4], float m[9])
{
    float w = q[0], x = q[1], y = q[2], z = q[3];
    m[0] = 1 - 2*(y*y + z*z); m[1] = 2*(x*y - w*z);     m[2] = 2*(x*z + w*y);
    m[3] = 2*(x*y + w*z);     m[4] = 1 - 2*(x*x + z*z); m[5] = 2*(y*z - w*x);
    m[6] = 2*(x*z - w*y);     m[7] = 2*(y*z + w*x);     m[8] = 1 - 2*(x*x + y*y);
}
static void mat_mul(const float m[9], const float v[3], float o[3])
{
    float x = m[0]*v[0] + m[1]*v[1] + m[2]*v[2];
    float y = m[3]*v[0] + m[4]*v[1] + m[5]*v[2];
    float z = m[6]*v[0] + m[7]*v[1] + m[8]*v[2];
    o[0] = x; o[1] = y; o[2] = z;
}
static void mat_tmul(const float m[9], const float v[3], float o[3])
{
    float x = m[0]*v[0] + m[3]*v[1] + m[6]*v[2];
    float y = m[1]*v[0] + m[4]*v[1] + m[7]*v[2];
    float z = m[2]*v[0] + m[5]*v[1] + m[8]*v[2];
    o[0] = x; o[1] = y; o[2] = z;
}
static void quat_norm(float q[4])
{
    float l = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (l < 1e-8f) { q[0] = 1; q[1] = q[2] = q[3] = 0; return; }
    for (int i = 0; i < 4; i++) q[i] /= l;
}
/* q += 0.5 * (0, w) * q * dt */
static void quat_integrate(float q[4], const float w[3], float dt)
{
    float h = 0.5f * dt;
    float dw = -w[0]*q[1] - w[1]*q[2] - w[2]*q[3];
    float dx =  w[0]*q[0] + w[1]*q[3] - w[2]*q[2];
    float dy = -w[0]*q[3] + w[1]*q[0] + w[2]*q[1];
    float dz =  w[0]*q[2] - w[1]*q[1] + w[2]*q[0];
    q[0] += dw*h; q[1] += dx*h; q[2] += dy*h; q[3] += dz*h;
    quat_norm(q);
}

static uint32_t xrand(hta_rigid_world *w)
{
    uint32_t x = w->rng ? w->rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return w->rng = x;
}
static float frand(hta_rigid_world *w) { return (float)(xrand(w) & 0xFFFFFF) / 16777215.0f; }

/* ------------------------------------------------------------- materials */

/* Densities are relative, in "kg per cubic world unit" divided by a
 * thousand -- only ratios matter to the solver. Bounce and friction are
 * the usual tabulated values. OURS, not from any tag. */
void hta_rigid_material_props(hta_rigid_material m, float *density, float *rest, float *fric)
{
    static const float t[HTA_RMAT_COUNT][3] = {
        /* density restitution friction */
        { 0.6f, 0.35f, 0.6f },  /* wood */
        { 7.8f, 0.25f, 0.4f },  /* metal */
        { 2.4f, 0.15f, 0.8f },  /* concrete */
        { 2.5f, 0.20f, 0.3f },  /* glass */
        { 1.0f, 0.05f, 0.9f },  /* flesh: thud, stick */
        { 1.6f, 0.05f, 1.0f },  /* dirt clods */
    };
    unsigned i = (unsigned)m < HTA_RMAT_COUNT ? (unsigned)m : 0u;
    if (density) *density = t[i][0];
    if (rest) *rest = t[i][1];
    if (fric) *fric = t[i][2];
}

/* ------------------------------------------------------------ the pool */

bool hta_rigid_init(hta_rigid_world *w, uint32_t cap, const hta_collision *world)
{
    if (!w) return false;
    memset(w, 0, sizeof(*w));
    w->world = world;
    w->gravity = 3.215f;
    w->floor_z = -1e9f;
    w->linear_drag = 0.05f;
    w->angular_drag = 0.1f;
    w->impact_min = 0.6f;
    w->collide_bodies = true;
    w->rng = 0x2545F491u;
    return hta_rigid_resize(w, cap);
}

void hta_rigid_free(hta_rigid_world *w)
{
    if (!w) return;
    free(w->bodies);
    w->bodies = NULL;
    w->cap = 0;
}

bool hta_rigid_resize(hta_rigid_world *w, uint32_t cap)
{
    if (!w) return false;
    if (cap == w->cap) return true;
    hta_rigid_body *nb = cap ? calloc(cap, sizeof(*nb)) : NULL;
    if (cap && !nb) return false;
    /* Keep the youngest bodies that fit. */
    uint32_t kept = 0;
    while (kept < cap) {
        int best = -1;
        for (uint32_t i = 0; i < w->cap; i++) {
            if (!w->bodies[i].active) continue;
            if (best < 0 || w->bodies[i].age < w->bodies[best].age) best = (int)i;
        }
        if (best < 0) break;
        nb[kept++] = w->bodies[best];
        w->bodies[best].active = false;
    }
    free(w->bodies);
    w->bodies = nb;
    w->cap = cap;
    return true;
}

void hta_rigid_clear(hta_rigid_world *w)
{
    if (!w) return;
    for (uint32_t i = 0; i < w->cap; i++) w->bodies[i].active = false;
    w->impact_count = 0;
}

uint32_t hta_rigid_active(const hta_rigid_world *w)
{
    uint32_t n = 0;
    for (uint32_t i = 0; w && i < w->cap; i++) n += w->bodies[i].active;
    return n;
}

uint32_t hta_rigid_spawn(hta_rigid_world *w, const hta_rigid_desc *d)
{
    if (!w || !d || !w->cap) return UINT32_MAX;
    uint32_t slot = UINT32_MAX;
    float oldest = -1.0f;
    for (uint32_t i = 0; i < w->cap; i++) {
        if (!w->bodies[i].active) { slot = i; break; }
        /* A body already fading, or asleep, goes before one in flight. */
        float a = w->bodies[i].age + (w->bodies[i].asleep ? 1000.0f : 0.0f);
        if (a > oldest) { oldest = a; slot = i; }
    }
    hta_rigid_body *b = &w->bodies[slot];
    uint32_t gen = b->generation + 1u;
    memset(b, 0, sizeof(*b));
    b->generation = gen;
    b->active = true;
    b->shape = (uint8_t)d->shape;
    b->material = (uint8_t)d->material;
    for (int k = 0; k < 3; k++) {
        float h = d->half[k] > 1e-3f ? d->half[k] : 1e-3f;
        b->half[k] = d->shape == HTA_RIGID_SPHERE ? (d->half[0] > 1e-3f ? d->half[0] : 1e-3f) : h;
        b->pos[k] = d->pos[k]; b->vel[k] = d->vel[k]; b->ang[k] = d->ang[k];
    }
    if (d->rot[0] == 0 && d->rot[1] == 0 && d->rot[2] == 0 && d->rot[3] == 0) b->rot[0] = 1.0f;
    else { memcpy(b->rot, d->rot, sizeof(b->rot)); quat_norm(b->rot); }
    float dens, rest, fric;
    hta_rigid_material_props(d->material, &dens, &rest, &fric);
    if (d->density > 0.0f) dens = d->density;
    float mass, ix, iy, iz;
    if (d->shape == HTA_RIGID_SPHERE) {
        float r = b->half[0];
        mass = dens * 4.18879f * r*r*r;
        ix = iy = iz = 0.4f * mass * r*r;
    } else {
        float x = 2*b->half[0], y = 2*b->half[1], z = 2*b->half[2];
        mass = dens * x*y*z;
        ix = mass * (y*y + z*z) / 12.0f;
        iy = mass * (x*x + z*z) / 12.0f;
        iz = mass * (x*x + y*y) / 12.0f;
    }
    if (mass < 1e-6f) mass = 1e-6f;
    b->inv_mass = 1.0f / mass;
    b->inv_inertia[0] = ix > 1e-9f ? 1.0f / ix : 0.0f;
    b->inv_inertia[1] = iy > 1e-9f ? 1.0f / iy : 0.0f;
    b->inv_inertia[2] = iz > 1e-9f ? 1.0f / iz : 0.0f;
    b->restitution = rest;
    b->friction = fric;
    b->life = d->life;
    b->fade = d->fade;
    b->user = d->user;
    return slot;
}

void hta_rigid_remove(hta_rigid_world *w, uint32_t i)
{
    if (w && i < w->cap) w->bodies[i].active = false;
}

/* ------------------------------------------------- sphere vs triangles */

static void closest_on_tri(const float p[3], const float a[3], const float b[3], const float c[3], float out[3])
{
    /* Ericson, Real-Time Collision Detection, 5.1.5. */
    float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float ac[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    float ap[3] = { p[0]-a[0], p[1]-a[1], p[2]-a[2] };
    float d1 = dot3(ab, ap), d2 = dot3(ac, ap);
    if (d1 <= 0 && d2 <= 0) { memcpy(out, a, 12); return; }
    float bp[3] = { p[0]-b[0], p[1]-b[1], p[2]-b[2] };
    float d3 = dot3(ab, bp), d4 = dot3(ac, bp);
    if (d3 >= 0 && d4 <= d3) { memcpy(out, b, 12); return; }
    float vc = d1*d4 - d3*d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float v = d1 / (d1 - d3);
        for (int k = 0; k < 3; k++) out[k] = a[k] + ab[k]*v;
        return;
    }
    float cp[3] = { p[0]-c[0], p[1]-c[1], p[2]-c[2] };
    float d5 = dot3(ab, cp), d6 = dot3(ac, cp);
    if (d6 >= 0 && d5 <= d6) { memcpy(out, c, 12); return; }
    float vb = d5*d2 - d1*d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float v = d2 / (d2 - d6);
        for (int k = 0; k < 3; k++) out[k] = a[k] + ac[k]*v;
        return;
    }
    float va = d3*d6 - d5*d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        float v = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (int k = 0; k < 3; k++) out[k] = b[k] + (c[k]-b[k])*v;
        return;
    }
    float den = 1.0f / (va + vb + vc);
    float v = vb * den, u = vc * den;
    for (int k = 0; k < 3; k++) out[k] = a[k] + ab[k]*v + ac[k]*u;
}

/* Keep the `max` deepest contacts, merging near-duplicates (a sphere on the
 * shared edge of two floor triangles touches both at one point). */
static uint32_t add_contact(hta_contact *out, uint32_t n, uint32_t max, const hta_contact *c)
{
    for (uint32_t i = 0; i < n; i++) {
        float d[3] = { out[i].point[0]-c->point[0], out[i].point[1]-c->point[1], out[i].point[2]-c->point[2] };
        if (dot3(d, d) < 1e-6f && dot3(out[i].normal, c->normal) > 0.95f) {
            if (c->depth > out[i].depth) out[i] = *c;
            return n;
        }
    }
    if (n < max) { out[n] = *c; return n + 1; }
    uint32_t shallow = 0;
    for (uint32_t i = 1; i < n; i++) if (out[i].depth < out[shallow].depth) shallow = i;
    if (c->depth > out[shallow].depth) out[shallow] = *c;
    return n;
}

/* `ref` (optional) is a point known to be on the OPEN side: the body's
 * centre. With it, a probe that has sunk under a surface -- deeper than its
 * own radius, where a closest-point test sees nothing -- still reports the
 * contact, up to `max_depth` below. Without it (spheres), only overlap. */
static uint32_t grid_sphere(const hta_collision *c, const float p[3], float r,
                            const float *ref, float max_depth,
                            hta_contact *out, uint32_t n, uint32_t max, uint32_t *tests)
{
    float reach = ref ? r + max_depth : r;
    if (!c->built || !c->cell_start || !c->tri_index || c->cell <= 0.0f) return n;
    int x0 = (int)floorf((p[0] - reach - c->min[0]) / c->cell), x1 = (int)floorf((p[0] + reach - c->min[0]) / c->cell);
    int y0 = (int)floorf((p[1] - reach - c->min[1]) / c->cell), y1 = (int)floorf((p[1] + reach - c->min[1]) / c->cell);
    if (x1 < 0 || y1 < 0 || x0 >= (int)c->nx || y0 >= (int)c->ny) return n;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)c->nx) x1 = (int)c->nx - 1;
    if (y1 >= (int)c->ny) y1 = (int)c->ny - 1;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        uint32_t cell = (uint32_t)y * c->nx + (uint32_t)x;
        for (uint32_t k = c->cell_start[cell]; k < c->cell_start[cell + 1]; k++) {
            uint32_t t = c->tri_index[k];
            if (t >= c->tri_count) continue;
            const float *a = c->verts[c->indices[t*3+0]].pos;
            const float *b = c->verts[c->indices[t*3+1]].pos;
            const float *d = c->verts[c->indices[t*3+2]].pos;
            /* Cheap reject: the triangle's box against the sphere's. */
            if (fminf(a[2], fminf(b[2], d[2])) > p[2] + reach || fmaxf(a[2], fmaxf(b[2], d[2])) < p[2] - reach) continue;
            if (tests) (*tests)++;
            if (ref) {
                /* Plane test, facing the body: catches a sunken corner. */
                float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] }, e2[3] = { d[0]-a[0], d[1]-a[1], d[2]-a[2] };
                float nn[3];
                cross3(e1, e2, nn);
                float l = len3(nn);
                if (l > 1e-9f) {
                    for (int m = 0; m < 3; m++) nn[m] /= l;
                    float rc[3] = { ref[0]-a[0], ref[1]-a[1], ref[2]-a[2] };
                    if (dot3(rc, nn) < 0.0f) for (int m = 0; m < 3; m++) nn[m] = -nn[m];
                    float pa[3] = { p[0]-a[0], p[1]-a[1], p[2]-a[2] };
                    float h = dot3(pa, nn);
                    if (h < r && h > -max_depth) {
                        float q[3] = { p[0]-nn[0]*h, p[1]-nn[1]*h, p[2]-nn[2]*h };
                        /* Inside the triangle? Same-side test on each edge. */
                        float c0[3], c1[3], c2[3];
                        float qa[3] = { q[0]-a[0], q[1]-a[1], q[2]-a[2] };
                        float qb[3] = { q[0]-b[0], q[1]-b[1], q[2]-b[2] };
                        float qd[3] = { q[0]-d[0], q[1]-d[1], q[2]-d[2] };
                        float bd[3] = { d[0]-b[0], d[1]-b[1], d[2]-b[2] };
                        float da[3] = { a[0]-d[0], a[1]-d[1], a[2]-d[2] };
                        cross3(e1, qa, c0); cross3(bd, qb, c1); cross3(da, qd, c2);
                        float s0 = dot3(c0, nn), s1 = dot3(c1, nn), s2 = dot3(c2, nn);
                        if ((s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0)) {
                            hta_contact ct;
                            memcpy(ct.point, q, 12);
                            memcpy(ct.normal, nn, 12);
                            ct.depth = r - h;
                            n = add_contact(out, n, max, &ct);
                            continue;
                        }
                    }
                }
            }
            float q[3];
            closest_on_tri(p, a, b, d, q);
            float v[3] = { p[0]-q[0], p[1]-q[1], p[2]-q[2] };
            float dd = dot3(v, v);
            if (dd >= r*r) continue;
            /* Behind the surface as seen from the body: the plane test
             * above owns that case; a closest point here would push the
             * wrong way. */
            if (ref) {
                float rv[3] = { ref[0]-q[0], ref[1]-q[1], ref[2]-q[2] };
                if (dot3(rv, v) < 0.0f) continue;
            }
            hta_contact ct;
            float dist = sqrtf(dd);
            memcpy(ct.point, q, 12);
            if (dist > 1e-5f) { for (int m = 0; m < 3; m++) ct.normal[m] = v[m] / dist; }
            else {
                float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] }, e2[3] = { d[0]-a[0], d[1]-a[1], d[2]-a[2] };
                cross3(e1, e2, ct.normal);
                float l = len3(ct.normal);
                if (l < 1e-9f) continue;
                for (int m = 0; m < 3; m++) ct.normal[m] /= l;
                if (ct.normal[2] < 0) for (int m = 0; m < 3; m++) ct.normal[m] = -ct.normal[m];
            }
            ct.depth = r - dist;
            n = add_contact(out, n, max, &ct);
        }
    }
    return n;
}

static uint32_t sphere_all(const hta_collision *c, const float p[3], float r,
                           const float *ref, float max_depth,
                           hta_contact *out, uint32_t n, uint32_t max, uint32_t *tests)
{
    if (!c) return n;
    n = grid_sphere(c, p, r, ref, max_depth, out, n, max, tests);
    if (c->extra) n = sphere_all(c->extra, p, r, ref, max_depth, out, n, max, tests);
    for (uint32_t i = 0; i < c->instance_count; i++) {
        const hta_collision_instance *in = &c->instances[i];
        if (!in->active || !in->grid || !in->grid->built) continue;
        float d[3] = { p[0]-in->pos[0], p[1]-in->pos[1], p[2]-in->pos[2] };
        if (dot3(d, d) > (in->radius + r) * (in->radius + r)) continue;
        float lp[3], lref[3];
        mat_tmul(in->rot, d, lp);      /* rot is row-major local->world */
        if (ref) {
            float dr[3] = { ref[0]-in->pos[0], ref[1]-in->pos[1], ref[2]-in->pos[2] };
            mat_tmul(in->rot, dr, lref);
        }
        hta_contact tmp[8];
        uint32_t m = grid_sphere(in->grid, lp, r, ref ? lref : NULL, max_depth, tmp, 0, 8, tests);
        for (uint32_t k = 0; k < m; k++) {
            hta_contact wc;
            float wp[3];
            mat_mul(in->rot, tmp[k].point, wp);
            for (int q = 0; q < 3; q++) wc.point[q] = wp[q] + in->pos[q];
            mat_mul(in->rot, tmp[k].normal, wc.normal);
            wc.depth = tmp[k].depth;
            n = add_contact(out, n, max, &wc);
        }
    }
    return n;
}

uint32_t hta_collision_sphere(const hta_collision *c, const float centre[3], float radius,
                              hta_contact *out, uint32_t max)
{
    if (!c || !out || !max || radius <= 0.0f) return 0;
    return sphere_all(c, centre, radius, NULL, 0.0f, out, 0, max, NULL);
}

/* ------------------------------------------------------------- solver */

typedef struct {
    float r[3];         /* contact point relative to the centre of mass */
    float n[3];
    float depth;
    float jn;           /* accumulated normal impulse */
    float bias;         /* target separating speed (restitution) */
} contact;

/* World-space inverse inertia applied to a vector: R * diag(I^-1) * R^T. */
static void inv_inertia_mul(const hta_rigid_body *b, const float R[9], const float v[3], float o[3])
{
    float l[3];
    mat_tmul(R, v, l);
    for (int k = 0; k < 3; k++) l[k] *= b->inv_inertia[k];
    mat_mul(R, l, o);
}

static void point_velocity(const hta_rigid_body *b, const float r[3], float o[3])
{
    float wr[3];
    cross3(b->ang, r, wr);
    for (int k = 0; k < 3; k++) o[k] = b->vel[k] + wr[k];
}

static void apply_impulse(hta_rigid_body *b, const float R[9], const float r[3], const float j[3])
{
    for (int k = 0; k < 3; k++) b->vel[k] += j[k] * b->inv_mass;
    float rj[3], dw[3];
    cross3(r, j, rj);
    inv_inertia_mul(b, R, rj, dw);
    for (int k = 0; k < 3; k++) b->ang[k] += dw[k];
}

static float eff_mass(const hta_rigid_body *b, const float R[9], const float r[3], const float n[3])
{
    float rn[3], t[3], u[3];
    cross3(r, n, rn);
    inv_inertia_mul(b, R, rn, t);
    cross3(t, r, u);
    float k = b->inv_mass + dot3(n, u);
    return k > 1e-9f ? k : 1e-9f;
}

/* The points a body touches the world with: its centre for a sphere, its
 * eight corners (rounded by `cr`) for a box. */
static uint32_t probe_points(const hta_rigid_body *b, const float R[9], float pts[8][3], float *radius)
{
    if (b->shape == HTA_RIGID_SPHERE) {
        memcpy(pts[0], b->pos, 12);
        *radius = b->half[0];
        return 1;
    }
    /* A little rounding keeps corner contacts smooth; much more and a box
     * behaves like a ball and rocks on its edges forever (it did, at
     * half the smallest extent). */
    float cr = fminf(b->half[0], fminf(b->half[1], b->half[2])) * 0.2f;
    *radius = cr;
    uint32_t n = 0;
    for (int i = 0; i < 8; i++) {
        float l[3] = { (i & 1 ? 1.f : -1.f) * (b->half[0] - cr),
                       (i & 2 ? 1.f : -1.f) * (b->half[1] - cr),
                       (i & 4 ? 1.f : -1.f) * (b->half[2] - cr) };
        float wv[3];
        mat_mul(R, l, wv);
        for (int k = 0; k < 3; k++) pts[n][k] = b->pos[k] + wv[k];
        n++;
    }
    return n;
}

static void record_impact(hta_rigid_world *w, uint32_t i, const float p[3], const float n[3], float speed)
{
    if (speed < w->impact_min) return;
    hta_rigid_impact *im = NULL;
    for (uint32_t k = 0; k < w->impact_count; k++)
        if (w->impacts[k].body == i) { im = &w->impacts[k]; break; }
    if (!im) {
        if (w->impact_count >= HTA_RIGID_MAX_IMPACTS) return;
        im = &w->impacts[w->impact_count++];
        im->speed = 0.0f;
    }
    if (speed <= im->speed) return;
    im->body = i;
    memcpy(im->point, p, 12);
    memcpy(im->normal, n, 12);
    im->speed = speed;
    im->material = w->bodies[i].material;
}

static void solve_world(hta_rigid_world *w, uint32_t bi, float dt)
{
    hta_rigid_body *b = &w->bodies[bi];
    float R[9];
    quat_mat(b->rot, R);
    float pts[8][3], pr;
    uint32_t np = probe_points(b, R, pts, &pr);

    /* Speculative margin: how far any point of the body can travel this
     * substep. Contacts inside it are solved BEFORE they touch, so a box
     * landing a hair off flat meets the floor with both edges in the same
     * solve -- rather than one edge a substep early, which spun it up to
     * 3 rad/s and left it tumbling on its edge. */
    float ext = b->shape == HTA_RIGID_SPHERE ? b->half[0] : len3(b->half);
    float margin = (len3(b->vel) + len3(b->ang) * ext) * dt + 0.005f;

    contact cs[16];
    uint32_t nc = 0;
    for (uint32_t p = 0; p < np && nc < 16; p++) {
        hta_contact hc[2];
        uint32_t m = 0;
        /* Box corners test against surfaces facing the centre, down to
         * the box's own thickness; spheres test plain overlap. */
        float md = b->shape == HTA_RIGID_BOX ? fminf(b->half[0], fminf(b->half[1], b->half[2])) * 1.5f : 0.0f;
        if (w->world) m = sphere_all(w->world, pts[p], pr + margin, b->shape == HTA_RIGID_BOX ? b->pos : NULL, md,
                                     hc, 0, 2, &w->tri_tests);
        /* The floor, when there is no world (tests, menus). */
        if (!w->world && pts[p][2] - pr - margin < w->floor_z) {
            hc[0].point[0] = pts[p][0]; hc[0].point[1] = pts[p][1]; hc[0].point[2] = w->floor_z;
            hc[0].normal[0] = 0; hc[0].normal[1] = 0; hc[0].normal[2] = 1;
            hc[0].depth = w->floor_z - (pts[p][2] - pr - margin);
            m = 1;
        }
        for (uint32_t k = 0; k < m && nc < 16; k++) {
            contact *c = &cs[nc++];
            for (int q = 0; q < 3; q++) c->r[q] = hc[k].point[q] - b->pos[q];
            memcpy(c->n, hc[k].normal, 12);
            c->depth = hc[k].depth - margin;     /* < 0: a gap still to close */
            c->jn = 0.0f;
            float v[3];
            point_velocity(b, c->r, v);
            float vn = dot3(v, c->n);
            float gap = c->depth < 0.0f ? -c->depth : 0.0f;
            bool hits = vn < 0.0f && gap <= -vn * dt;
            if (hits && vn < -0.35f) {
                /* Arriving this substep: bounce. */
                c->bias = -b->restitution * vn;
                record_impact(w, bi, hc[k].point, c->n, -vn);
            } else {
                /* Otherwise allow closing exactly the gap, no more. */
                c->bias = -gap / dt;
            }
        }
    }
    if (!nc) return;

    /* Sequential impulses: normal, then friction bounded by it. */
    /* Symmetric Gauss-Seidel: alternate the order each pass, so a box
     * landing flat is not spun by which corner happened to be solved first. */
    for (int it = 0; it < 8; it++) {
        for (uint32_t kk = 0; kk < nc; kk++) {
            contact *c = &cs[(it & 1) ? nc - 1 - kk : kk];
            float v[3];
            point_velocity(b, c->r, v);
            float vn = dot3(v, c->n);
            float kn = eff_mass(b, R, c->r, c->n);
            float dj = (c->bias - vn) / kn;
            float old = c->jn;
            c->jn = fmaxf(old + dj, 0.0f);
            dj = c->jn - old;
            float J[3] = { c->n[0]*dj, c->n[1]*dj, c->n[2]*dj };
            apply_impulse(b, R, c->r, J);

            point_velocity(b, c->r, v);
            vn = dot3(v, c->n);
            float vt[3] = { v[0] - c->n[0]*vn, v[1] - c->n[1]*vn, v[2] - c->n[2]*vn };
            float vtl = len3(vt);
            if (vtl > 1e-5f) {
                float t[3] = { vt[0]/vtl, vt[1]/vtl, vt[2]/vtl };
                float kt = eff_mass(b, R, c->r, t);
                float jt = -vtl / kt;
                float lim = b->friction * c->jn;
                if (jt < -lim) jt = -lim;
                float Jt[3] = { t[0]*jt, t[1]*jt, t[2]*jt };
                apply_impulse(b, R, c->r, Jt);
            }
        }
    }
    /* Nearly stopped against the ground, a body also rolls to a stop --
     * otherwise a pebble on a flat floor rolls forever. Only when slow: a
     * real roll down a slope must not be braked (it runs at 5/7 g sin a,
     * and the test holds it to that). */
    float ground = 0.0f;
    for (uint32_t k = 0; k < nc; k++) if (cs[k].n[2] > 0.5f) ground = 1.0f;
    if (ground > 0.0f && dot3(b->vel, b->vel) < 0.3f * 0.3f) {
        float damp = expf(-3.0f * b->friction * dt);
        for (int q = 0; q < 3; q++) b->ang[q] *= damp;
    }

    /* Push out of the deepest overlap along its normal, past a small slop
     * so resting contact is not a jitter. */
    uint32_t deep = 0;
    for (uint32_t k = 1; k < nc; k++) if (cs[k].depth > cs[deep].depth) deep = k;
    float push = cs[deep].depth - 0.002f;       /* negative for a gap: no push */
    if (push > 0.0f) for (int q = 0; q < 3; q++) b->pos[q] += cs[deep].n[q] * push * 0.8f;
}

/* Bodies against bodies, as their inscribed spheres: enough for a pile of
 * rubble to heap instead of interpenetrating. */
static void solve_pairs(hta_rigid_world *w)
{
    for (uint32_t i = 0; i < w->cap; i++) {
        hta_rigid_body *a = &w->bodies[i];
        if (!a->active) continue;
        float ra = a->shape == HTA_RIGID_SPHERE ? a->half[0]
                 : (a->half[0] + a->half[1] + a->half[2]) / 3.0f;
        for (uint32_t j = i + 1; j < w->cap; j++) {
            hta_rigid_body *b = &w->bodies[j];
            if (!b->active || (a->asleep && b->asleep)) continue;
            float rb = b->shape == HTA_RIGID_SPHERE ? b->half[0]
                     : (b->half[0] + b->half[1] + b->half[2]) / 3.0f;
            float d[3] = { b->pos[0]-a->pos[0], b->pos[1]-a->pos[1], b->pos[2]-a->pos[2] };
            float rr = ra + rb, dd = dot3(d, d);
            if (dd >= rr*rr || dd < 1e-10f) continue;
            float dist = sqrtf(dd);
            float n[3] = { d[0]/dist, d[1]/dist, d[2]/dist };
            float pen = rr - dist;
            float wa = a->inv_mass, wb = b->inv_mass, ws = wa + wb;
            if (ws <= 0.0f) continue;
            for (int k = 0; k < 3; k++) {
                a->pos[k] -= n[k] * pen * (wa / ws) * 0.5f;
                b->pos[k] += n[k] * pen * (wb / ws) * 0.5f;
            }
            float rv[3] = { b->vel[0]-a->vel[0], b->vel[1]-a->vel[1], b->vel[2]-a->vel[2] };
            float vn = dot3(rv, n);
            if (vn >= 0.0f) continue;
            float e = fminf(a->restitution, b->restitution);
            float j = -(1.0f + e) * vn / ws;
            for (int k = 0; k < 3; k++) { a->vel[k] -= n[k] * j * wa; b->vel[k] += n[k] * j * wb; }
            if (fabsf(vn) > 0.3f) { a->asleep = false; b->asleep = false; a->still_for = b->still_for = 0.0f; }
        }
    }
}

static void substep(hta_rigid_world *w, float dt)
{
    for (uint32_t i = 0; i < w->cap; i++) {
        hta_rigid_body *b = &w->bodies[i];
        if (!b->active || b->asleep) continue;
        b->vel[2] -= w->gravity * dt;
        float ld = expf(-w->linear_drag * dt), ad = expf(-w->angular_drag * dt);
        for (int k = 0; k < 3; k++) { b->vel[k] *= ld; b->ang[k] *= ad; }

        /* Fast and small: sweep the centre so it cannot pass through a
         * wall in one step. */
        float move[3] = { b->vel[0]*dt, b->vel[1]*dt, b->vel[2]*dt };
        float ml = len3(move);
        float rmin = b->shape == HTA_RIGID_SPHERE ? b->half[0]
                   : fminf(b->half[0], fminf(b->half[1], b->half[2]));
        if (w->world && ml > rmin) {
            float t, hit[3], nrm[3];
            if (hta_collision_ray(w->world, b->pos, move, 1.0f, &t, hit, nrm) && t < 1.0f) {
                float back = rmin / ml;
                float tt = t - back > 0.0f ? t - back : 0.0f;
                for (int k = 0; k < 3; k++) b->pos[k] += move[k] * tt;
                float vn = dot3(b->vel, nrm);
                if (vn < 0.0f) {
                    record_impact(w, i, hit, nrm, -vn);
                    for (int k = 0; k < 3; k++) b->vel[k] -= (1.0f + b->restitution) * vn * nrm[k];
                }
                move[0] = move[1] = move[2] = 0.0f;
            }
        }
        for (int k = 0; k < 3; k++) b->pos[k] += move[k];
        quat_integrate(b->rot, b->ang, dt);
        solve_world(w, i, dt);
    }
    if (w->collide_bodies) solve_pairs(w);
}

void hta_rigid_step(hta_rigid_world *w, float dt)
{
    if (!w || dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;               /* a hitch is not a teleport */
    w->impact_count = 0;
    w->tri_tests = 0;

    /* Enough substeps that nothing moves more than half its size in one. */
    float worst = 0.0f;
    for (uint32_t i = 0; i < w->cap; i++) {
        const hta_rigid_body *b = &w->bodies[i];
        if (!b->active || b->asleep) continue;
        float rmin = b->shape == HTA_RIGID_SPHERE ? b->half[0]
                   : fminf(b->half[0], fminf(b->half[1], b->half[2]));
        float r = len3(b->vel) * dt / (rmin > 1e-3f ? rmin : 1e-3f);
        if (r > worst) worst = r;
    }
    int n = (int)ceilf(worst * 0.5f);
    if (n < 2) n = 2;
    if (n > 6) n = 6;
    float h = dt / (float)n;
    for (int s = 0; s < n; s++) substep(w, h);

    w->awake = 0;
    for (uint32_t i = 0; i < w->cap; i++) {
        hta_rigid_body *b = &w->bodies[i];
        if (!b->active) continue;
        b->age += dt;
        if (b->life > 0.0f && b->age > b->life + b->fade) { b->active = false; continue; }
        if (b->asleep) continue;
        /* Nearly still for a while: sleep. */
        float v2 = dot3(b->vel, b->vel), w2 = dot3(b->ang, b->ang);
        if (v2 < 0.03f * 0.03f + w->gravity * dt * w->gravity * dt * 4.0f && w2 < 0.15f * 0.15f) {
            b->still_for += dt;
            if (b->still_for > 0.4f) {
                b->asleep = true;
                memset(b->vel, 0, sizeof(b->vel));
                memset(b->ang, 0, sizeof(b->ang));
                continue;
            }
        } else {
            b->still_for = 0.0f;
        }
        /* Lost below the world: gone. */
        if (b->pos[2] < -5000.0f) { b->active = false; continue; }
        w->awake++;
    }
}

void hta_rigid_blast(hta_rigid_world *w, const float centre[3], float radius, float speed)
{
    if (!w || radius <= 0.0f) return;
    for (uint32_t i = 0; i < w->cap; i++) {
        hta_rigid_body *b = &w->bodies[i];
        if (!b->active) continue;
        float d[3] = { b->pos[0]-centre[0], b->pos[1]-centre[1], b->pos[2]-centre[2] };
        float dist = len3(d);
        if (dist >= radius) continue;
        float f = 1.0f - dist / radius;
        float n[3] = { 0, 0, 1 };
        if (dist > 1e-4f) for (int k = 0; k < 3; k++) n[k] = d[k] / dist;
        /* Always some lift: blasts throw things UP out of craters. */
        n[2] = n[2] * 0.6f + 0.6f;
        float l = len3(n);
        for (int k = 0; k < 3; k++) b->vel[k] += n[k] / l * speed * f;
        for (int k = 0; k < 3; k++) b->ang[k] += (frand(w) * 2.0f - 1.0f) * 12.0f * f;
        b->asleep = false;
        b->still_for = 0.0f;
    }
}

void hta_rigid_impulse(hta_rigid_world *w, uint32_t i, const float point[3], const float j[3])
{
    if (!w || i >= w->cap || !w->bodies[i].active) return;
    hta_rigid_body *b = &w->bodies[i];
    float R[9], r[3];
    quat_mat(b->rot, R);
    for (int k = 0; k < 3; k++) r[k] = point[k] - b->pos[k];
    apply_impulse(b, R, r, j);
    b->asleep = false;
    b->still_for = 0.0f;
}

void hta_rigid_matrix(const hta_rigid_body *b, float o[16])
{
    float R[9];
    quat_mat(b->rot, R);
    /* Column-major: column c is R's column c. */
    o[0] = R[0]; o[1] = R[3]; o[2] = R[6];  o[3] = 0;
    o[4] = R[1]; o[5] = R[4]; o[6] = R[7];  o[7] = 0;
    o[8] = R[2]; o[9] = R[5]; o[10] = R[8]; o[11] = 0;
    o[12] = b->pos[0]; o[13] = b->pos[1]; o[14] = b->pos[2]; o[15] = 1;
}

float hta_rigid_alpha(const hta_rigid_body *b)
{
    if (!b || !b->active) return 0.0f;
    if (b->life <= 0.0f || b->age <= b->life) return 1.0f;
    if (b->fade <= 0.0f) return 0.0f;
    float a = 1.0f - (b->age - b->life) / b->fade;
    return a < 0.0f ? 0.0f : a;
}
