#include "player.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRID_TARGET_CELLS 64u
#define MAX_GRID_DIM      256u

void hta_collision_free(hta_collision *c)
{
    if (!c) return;
    free(c->cell_start);
    free(c->tri_index);
    memset(c, 0, sizeof(*c));
}

void hta_collision_set_slope(hta_collision *c, float max_slope_radians)
{
    if (!c) return;
    float nz = cosf(max_slope_radians);
    if (nz > 0.1f && nz < 0.999f) c->walkable_nz = nz;
}

bool hta_collision_build(hta_collision *c, const hta_bsp_mesh *mesh)
{
    return hta_collision_build_cells(c, mesh, GRID_TARGET_CELLS);
}

bool hta_collision_build_cells(hta_collision *c, const hta_bsp_mesh *mesh, uint32_t across)
{
    /* `across` cells of span/across fit in across+1 columns; keep that
     * within MAX_GRID_DIM or the clamp below drops the far edge's strip. */
    if (across < 1u || across > MAX_GRID_DIM - 1u) across = GRID_TARGET_CELLS;
    if (!c || !mesh || !mesh->vertices || !mesh->indices || mesh->index_count < 3) return false;
    memset(c, 0, sizeof(*c));

    c->verts     = mesh->vertices;
    c->indices   = mesh->indices;
    c->tri_material = mesh->tri_material;
    c->tri_count = mesh->index_count / 3;
    c->walkable_nz = 0.50f;

    float ex = mesh->bounds_max[0] - mesh->bounds_min[0];
    float ey = mesh->bounds_max[1] - mesh->bounds_min[1];
    if (!(ex > 0.0f) || !(ey > 0.0f)) return false;

    float span = ex > ey ? ex : ey;
    c->cell = span / (float)across;
    if (c->cell <= 1e-4f) c->cell = 1.0f;

    c->min[0] = mesh->bounds_min[0];
    c->min[1] = mesh->bounds_min[1];
    c->nx = (uint32_t)(ex / c->cell) + 1u;
    c->ny = (uint32_t)(ey / c->cell) + 1u;
    if (c->nx > MAX_GRID_DIM) c->nx = MAX_GRID_DIM;
    if (c->ny > MAX_GRID_DIM) c->ny = MAX_GRID_DIM;

    uint32_t ncells = c->nx * c->ny;
    uint32_t *counts = (uint32_t *)calloc(ncells + 1u, sizeof(uint32_t));
    if (!counts) return false;

    /* pass 1: count triangles per cell (by AABB overlap) */
    uint64_t total = 0;
    for (uint32_t t = 0; t < c->tri_count; t++) {
        uint32_t i0 = c->indices[t*3+0], i1 = c->indices[t*3+1], i2 = c->indices[t*3+2];
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count) continue;
        const float *a = c->verts[i0].pos, *b = c->verts[i1].pos, *d = c->verts[i2].pos;

        float lo0 = fminf(a[0], fminf(b[0], d[0])), hi0 = fmaxf(a[0], fmaxf(b[0], d[0]));
        float lo1 = fminf(a[1], fminf(b[1], d[1])), hi1 = fmaxf(a[1], fmaxf(b[1], d[1]));
        int cx0 = (int)((lo0 - c->min[0]) / c->cell), cx1 = (int)((hi0 - c->min[0]) / c->cell);
        int cy0 = (int)((lo1 - c->min[1]) / c->cell), cy1 = (int)((hi1 - c->min[1]) / c->cell);
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (cx1 >= (int)c->nx) cx1 = (int)c->nx - 1;
        if (cy1 >= (int)c->ny) cy1 = (int)c->ny - 1;
        for (int y = cy0; y <= cy1; y++)
            for (int x = cx0; x <= cx1; x++) { counts[(uint32_t)y * c->nx + (uint32_t)x]++; total++; }
    }
    if (total == 0 || total > 64u * 1024u * 1024u) { free(counts); return false; }

    c->cell_start = (uint32_t *)calloc(ncells + 1u, sizeof(uint32_t));
    c->tri_index  = (uint32_t *)malloc((size_t)total * sizeof(uint32_t));
    if (!c->cell_start || !c->tri_index) { free(counts); hta_collision_free(c); return false; }

    uint32_t run = 0;
    for (uint32_t i = 0; i < ncells; i++) { c->cell_start[i] = run; run += counts[i]; }
    c->cell_start[ncells] = run;

    /* pass 2: fill */
    uint32_t *cursor = (uint32_t *)calloc(ncells, sizeof(uint32_t));
    if (!cursor) { free(counts); hta_collision_free(c); return false; }
    for (uint32_t t = 0; t < c->tri_count; t++) {
        uint32_t i0 = c->indices[t*3+0], i1 = c->indices[t*3+1], i2 = c->indices[t*3+2];
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count) continue;
        const float *a = c->verts[i0].pos, *b = c->verts[i1].pos, *d = c->verts[i2].pos;
        float lo0 = fminf(a[0], fminf(b[0], d[0])), hi0 = fmaxf(a[0], fmaxf(b[0], d[0]));
        float lo1 = fminf(a[1], fminf(b[1], d[1])), hi1 = fmaxf(a[1], fmaxf(b[1], d[1]));
        int cx0 = (int)((lo0 - c->min[0]) / c->cell), cx1 = (int)((hi0 - c->min[0]) / c->cell);
        int cy0 = (int)((lo1 - c->min[1]) / c->cell), cy1 = (int)((hi1 - c->min[1]) / c->cell);
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (cx1 >= (int)c->nx) cx1 = (int)c->nx - 1;
        if (cy1 >= (int)c->ny) cy1 = (int)c->ny - 1;
        for (int y = cy0; y <= cy1; y++)
            for (int x = cx0; x <= cx1; x++) {
                uint32_t ci = (uint32_t)y * c->nx + (uint32_t)x;
                c->tri_index[c->cell_start[ci] + cursor[ci]++] = t;
            }
    }
    free(cursor);
    free(counts);
    c->built = true;
    return true;
}

void hta_collision_rebind(hta_collision *c, const hta_vertex *verts,
                          const uint32_t *indices)
{
    if (!c || !c->built) return;
    if (verts) c->verts = verts;
    if (indices) c->indices = indices;
}

/* Barycentric point-in-triangle in XY, then interpolate Z. */
static bool tri_height(const float a[3], const float b[3], const float c3[3],
                       float x, float y, float *out_z)
{
    float v0x = c3[0]-a[0], v0y = c3[1]-a[1];
    float v1x = b[0]-a[0],  v1y = b[1]-a[1];
    float v2x = x-a[0],     v2y = y-a[1];

    float d00 = v0x*v0x + v0y*v0y;
    float d01 = v0x*v1x + v0y*v1y;
    float d02 = v0x*v2x + v0y*v2y;
    float d11 = v1x*v1x + v1y*v1y;
    float d12 = v1x*v2x + v1y*v2y;

    float denom = d00*d11 - d01*d01;
    if (fabsf(denom) < 1e-12f) return false;    /* degenerate in XY */
    float inv = 1.0f / denom;
    float u = (d11*d02 - d01*d12) * inv;
    float v = (d00*d12 - d01*d02) * inv;
    const float EPS = -1e-4f;
    if (u < EPS || v < EPS || (u + v) > 1.0f - EPS*10.0f + 1e-3f) {
        if (u < EPS || v < EPS || (u + v) > 1.0f + 1e-3f) return false;
    }
    *out_z = a[2] + u * (c3[2]-a[2]) + v * (b[2]-a[2]);
    return true;
}

/* Shared by the height query and the material query: the best walkable
 * triangle under this point, or -1. */
static int32_t ground_tri(const hta_collision *c, float x, float y, float z_from,
                          float *out_z)
{
    if (!c || !c->built) return -1;
    int cx = (int)((x - c->min[0]) / c->cell);
    int cy = (int)((y - c->min[1]) / c->cell);
    if (cx < 0 || cy < 0 || cx >= (int)c->nx || cy >= (int)c->ny) return -1;

    uint32_t ci = (uint32_t)cy * c->nx + (uint32_t)cx;
    uint32_t s = c->cell_start[ci], e = c->cell_start[ci + 1u];
    int32_t best_tri = -1;
    float best = -1e30f;
    const float WALKABLE_NZ = (c->walkable_nz > 0.1f) ? c->walkable_nz : 0.50f;
    const float STEP = 0.18f;
    for (uint32_t k = s; k < e; k++) {
        uint32_t t = c->tri_index[k];
        const float *a = c->verts[c->indices[t*3+0]].pos;
        const float *b = c->verts[c->indices[t*3+1]].pos;
        const float *d = c->verts[c->indices[t*3+2]].pos;
        float e1x = b[0]-a[0], e1y = b[1]-a[1], e1z = b[2]-a[2];
        float e2x = d[0]-a[0], e2y = d[1]-a[1], e2z = d[2]-a[2];
        float nx = e1y*e2z - e1z*e2y;
        float ny = e1z*e2x - e1x*e2z;
        float nz = e1x*e2y - e1y*e2x;
        float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
        if (nlen < 1e-8f || fabsf(nz) / nlen < WALKABLE_NZ) continue;
        float z;
        if (!tri_height(a, b, d, x, y, &z)) continue;
        if (z <= z_from + STEP && z > best) { best = z; best_tri = (int32_t)t; }
    }
    if (best_tri >= 0 && out_z) *out_z = best;
    return best_tri;
}

static bool inst_ground(const hta_collision_instance *in, float x, float y, float from,
                        float *out_z, uint8_t *mat);
uint8_t hta_collision_ground_material(const hta_collision *c,
                                      float x, float y, float z_from)
{
    if (!c) return HTA_MATERIAL_NONE;
    float z = -INFINITY, ez = -INFINITY;
    int32_t t = ground_tri(c, x, y, z_from, &z);
    if (c->extra && hta_collision_ground(c->extra, x, y, z_from, &ez) && ez > z)
        return hta_collision_ground_material(c->extra, x, y, z_from);
    uint8_t best = t >= 0 && c->tri_material ? c->tri_material[t] : HTA_MATERIAL_NONE;
    for (uint32_t i = 0; i < c->instance_count; i++) {
        uint8_t m;
        if (inst_ground(&c->instances[i], x, y, z_from, &ez, &m) && ez > z) { z = ez; best = m; }
    }
    return best;
}

void hta_collision_rebind_material(hta_collision *c, const uint8_t *tri_material)
{
    if (c) c->tri_material = tri_material;
}

static bool collision_ground_static(const hta_collision *c, float x, float y, float z_from,
                          float *out_z)
{
    if (!c || !c->built || !out_z) return false;
    int cx = (int)((x - c->min[0]) / c->cell);
    int cy = (int)((y - c->min[1]) / c->cell);
    if (cx < 0 || cy < 0 || cx >= (int)c->nx || cy >= (int)c->ny) return false;

    uint32_t ci = (uint32_t)cy * c->nx + (uint32_t)cx;
    uint32_t s = c->cell_start[ci], e = c->cell_start[ci + 1u];

    bool found = false;
    float best = -1e30f;
    /* Only faces that point mostly up are floors. Steep pylon/cliff sides
     * used to count as ground, so walking into a wall launched you over it. */
    const float WALKABLE_NZ = (c->walkable_nz > 0.1f) ? c->walkable_nz : 0.50f;
    const float STEP = 0.18f;
    for (uint32_t k = s; k < e; k++) {
        uint32_t t = c->tri_index[k];
        const float *a = c->verts[c->indices[t*3+0]].pos;
        const float *b = c->verts[c->indices[t*3+1]].pos;
        const float *d = c->verts[c->indices[t*3+2]].pos;
        float e1x = b[0]-a[0], e1y = b[1]-a[1], e1z = b[2]-a[2];
        float e2x = d[0]-a[0], e2y = d[1]-a[1], e2z = d[2]-a[2];
        float nx = e1y*e2z - e1z*e2y;
        float ny = e1z*e2x - e1x*e2z;
        float nz = e1x*e2y - e1y*e2x;
        float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
        if (nlen < 1e-8f || fabsf(nz) / nlen < WALKABLE_NZ) continue;
        float z;
        if (!tri_height(a, b, d, x, y, &z)) continue;
        if (z <= z_from + STEP && z > best) { best = z; found = true; }
    }
    if (found) *out_z = best;
    return found;
}


/* The pawn is a CYLINDER: radius r, from the feet to the head, flat-topped.
 * A triangle blocks it only where the triangle lies inside that height slab.
 *
 * So: clip the triangle to [z0,z1] and measure the XY distance from the axis to
 * what is left. Anything above the head clips away entirely and cannot push --
 * which is the whole bug. The old code probed one height and clamped the answer,
 * which on a leaning face reports the wrong distance in both directions; a
 * capsule test is no good either, because its rounded shoulder still catches
 * roofs. Returns 1e30f when the triangle never reaches the slab.
 *
 * `inside` is set when the axis is within the clipped polygon, where there is no
 * meaningful direction to push and the caller falls back to the face normal. */
static float tri_slab_xy_dist(const float a[3], const float b[3], const float c3[3],
                              float px, float py, float z0, float z1,
                              float *out_qx, float *out_qy, int *inside)
{
    float poly[8][3], tmp[8][3];
    int n = 3, m = 0;
    poly[0][0]=a[0]; poly[0][1]=a[1]; poly[0][2]=a[2];
    poly[1][0]=b[0]; poly[1][1]=b[1]; poly[1][2]=b[2];
    poly[2][0]=c3[0]; poly[2][1]=c3[1]; poly[2][2]=c3[2];

    /* Sutherland-Hodgman against the two horizontal planes. */
    for (int pass = 0; pass < 2; pass++) {
        float lim = pass ? z1 : z0;
        m = 0;
        for (int i = 0; i < n; i++) {
            const float *p = poly[i], *q = poly[(i + 1) % n];
            float dp = pass ? (lim - p[2]) : (p[2] - lim);
            float dq = pass ? (lim - q[2]) : (q[2] - lim);
            int inp = dp >= 0.0f, inq = dq >= 0.0f;
            if (inp && m < 8) { tmp[m][0]=p[0]; tmp[m][1]=p[1]; tmp[m][2]=p[2]; m++; }
            if (inp != inq && m < 8) {
                float den = dp - dq;
                float t = (fabsf(den) > 1e-12f) ? dp / den : 0.0f;
                tmp[m][0] = p[0] + (q[0]-p[0])*t;
                tmp[m][1] = p[1] + (q[1]-p[1])*t;
                tmp[m][2] = p[2] + (q[2]-p[2])*t;
                m++;
            }
        }
        n = m;
        for (int i = 0; i < n; i++) {
            poly[i][0]=tmp[i][0]; poly[i][1]=tmp[i][1]; poly[i][2]=tmp[i][2];
        }
        if (n == 0) return 1e30f;
    }
    if (n == 0) return 1e30f;

    /* Distance in XY from the axis to the clipped polygon. */
    *inside = 0;
    if (n >= 3) {
        int neg = 0, pos = 0;
        for (int i = 0; i < n; i++) {
            const float *p = poly[i], *q = poly[(i + 1) % n];
            float cr = (q[0]-p[0])*(py-p[1]) - (q[1]-p[1])*(px-p[0]);
            if (cr < -1e-9f) neg = 1;
            if (cr >  1e-9f) pos = 1;
        }
        if (!(neg && pos)) { *inside = 1; *out_qx = px; *out_qy = py; return 0.0f; }
    }
    float best = 1e30f;
    for (int i = 0; i < n; i++) {
        const float *p = poly[i];
        const float *q = poly[(i + 1) % n];
        float ex = q[0]-p[0], ey = q[1]-p[1];
        float len2 = ex*ex + ey*ey;
        float t = 0.0f;
        if (len2 > 1e-12f) {
            t = ((px-p[0])*ex + (py-p[1])*ey) / len2;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        }
        float qx = p[0] + ex*t, qy = p[1] + ey*t;
        float dx = px-qx, dy = py-qy;
        float d2 = dx*dx + dy*dy;
        if (d2 < best) { best = d2; *out_qx = qx; *out_qy = qy; }
        if (n < 2) break;
    }
    return best;
}

static void collision_depenetrate_static(const hta_collision *c,
                               float *x, float *y, float z_feet,
                               float height, float radius)
{
    if (!c || !c->built || !x || !y || radius <= 0.0f) return;
    float walk = (c->walkable_nz > 0.1f) ? c->walkable_nz : 0.50f;
    float z0 = z_feet;
    float z1 = z_feet + (height > 1e-4f ? height : 0.7f);
    float r = radius;
    /* Deepest penetration per pass so a triangle listed in several grid
     * cells cannot shove the pawn several times in one frame. */
    for (int iter = 0; iter < 3; iter++) {
        int cx0 = (int)((*x - r - c->min[0]) / c->cell);
        int cx1 = (int)((*x + r - c->min[0]) / c->cell);
        int cy0 = (int)((*y - r - c->min[1]) / c->cell);
        int cy1 = (int)((*y + r - c->min[1]) / c->cell);
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (cx1 >= (int)c->nx) cx1 = (int)c->nx - 1;
        if (cy1 >= (int)c->ny) cy1 = (int)c->ny - 1;
        float px = *x, py = *y;
        float best_push = 0.0f, best_hx = 0.0f, best_hy = 0.0f;
        for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            uint32_t ci = (uint32_t)cy * c->nx + (uint32_t)cx;
            uint32_t s = c->cell_start[ci], e = c->cell_start[ci + 1u];
            for (uint32_t k = s; k < e; k++) {
                uint32_t t = c->tri_index[k];
                const float *a = c->verts[c->indices[t*3+0]].pos;
                const float *b = c->verts[c->indices[t*3+1]].pos;
                const float *d = c->verts[c->indices[t*3+2]].pos;
                float e1x=b[0]-a[0], e1y=b[1]-a[1], e1z=b[2]-a[2];
                float e2x=d[0]-a[0], e2y=d[1]-a[1], e2z=d[2]-a[2];
                float nx=e1y*e2z-e1z*e2y, ny=e1z*e2x-e1x*e2z, nz=e1x*e2y-e1y*e2x;
                float nlen = sqrtf(nx*nx+ny*ny+nz*nz);
                if (nlen < 1e-8f) continue;
                if (fabsf(nz) / nlen >= walk) continue; /* floor/ceiling */
                /* Ledge lips: the vertical face of the floor you are standing on
                 * must not act as a wall, or you cannot walk off a base. A real
                 * wall/pylon rises more than a step above the feet. */
                float zmax = a[2];
                if (b[2] > zmax) zmax = b[2];
                if (d[2] > zmax) zmax = d[2];
                if (zmax <= z0 + 0.18f) continue;

                /* The crown of the pawn is rounded, not a flat disc.
                 *
                 * A flat-topped cylinder meets an overhead lip a full radius
                 * early -- and on a descending ramp the floor is still
                 * (radius * slope) higher back there, so it bangs its head on
                 * a lip it would comfortably clear a step later. That is a
                 * 0.12 wu penalty at this radius on a 30-degree ramp, against
                 * clearances measured in millimetres: it is what stops you
                 * walking down the base ramp standing while a crouch strolls
                 * through.
                 *
                 * So cap the body with a hemisphere of the same radius: below
                 * z1 - r it is the full-radius cylinder it always was, and
                 * within the cap the usable radius narrows to zero at the
                 * crown. Only faces that live entirely in that top band are
                 * affected -- a wall reaching any lower still blocks at full
                 * radius, so pylons, hog flanks and base walls are untouched.
                 *
                 * Widest-point rule: measure the radius at the LOWEST part of
                 * the face that is in the cap, which is where it bites most. */
                float zmin = a[2];
                if (b[2] < zmin) zmin = b[2];
                if (d[2] < zmin) zmin = d[2];
                float cap_base = z1 - r;
                if (cap_base < z0) cap_base = z0;
                float reff = r;
                if (zmin > cap_base) {
                    float up = zmin - cap_base;
                    if (up >= r) continue;      /* clears the crown entirely */
                    reff = sqrtf(r * r - up * up);
                }
                float reff2 = reff * reff;

                float qx = px, qy = py;
                int inside = 0;
                float dist2 = tri_slab_xy_dist(a, b, d, px, py, z0, z1,
                                               &qx, &qy, &inside);
                if (dist2 >= reff2) continue;
                float dx = px - qx, dy = py - qy;
                if (inside) dist2 = 0.0f;
                float hx, hy, hl, push;
                if (dist2 < 1e-12f) {
                    hx = nx / nlen;
                    hy = ny / nlen;
                    hl = sqrtf(hx * hx + hy * hy);
                    if (hl < 1e-5f) continue;
                    hx /= hl;
                    hy /= hl;
                    push = reff;
                } else {
                    float dist = sqrtf(dist2);
                    push = reff - dist;
                    hx = dx;
                    hy = dy;
                    hl = sqrtf(hx * hx + hy * hy);
                    if (hl < 1e-5f) {
                        hx = nx / nlen;
                        hy = ny / nlen;
                        hl = sqrtf(hx * hx + hy * hy);
                    }
                    if (hl < 1e-5f) continue;
                    hx /= hl;
                    hy /= hl;
                }
                if (push > best_push) {
                    best_push = push;
                    best_hx = hx;
                    best_hy = hy;
                }
            }
        }
        if (best_push < 1e-5f) break;
        *x = px + best_hx * best_push;
        *y = py + best_hy * best_push;
    }
}

/* One triangle against the ray, Moller-Trumbore. Updates the running best. */
static void ray_tri(const hta_collision *c, uint32_t t,
                    const float orig[3], const float dir[3],
                    float *best, int *found, int32_t *best_tri)
{
    const float EPS = 1e-7f;
    const float *v0 = c->verts[c->indices[t * 3 + 0]].pos;
    const float *v1 = c->verts[c->indices[t * 3 + 1]].pos;
    const float *v2 = c->verts[c->indices[t * 3 + 2]].pos;
    float e1[3] = { v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2] };
    float e2[3] = { v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2] };
    float pvec[3] = {
        dir[1]*e2[2] - dir[2]*e2[1],
        dir[2]*e2[0] - dir[0]*e2[2],
        dir[0]*e2[1] - dir[1]*e2[0]
    };
    float det = e1[0]*pvec[0] + e1[1]*pvec[1] + e1[2]*pvec[2];
    if (det > -EPS && det < EPS) return;
    float inv = 1.0f / det;
    float tvec[3] = { orig[0]-v0[0], orig[1]-v0[1], orig[2]-v0[2] };
    float u = (tvec[0]*pvec[0] + tvec[1]*pvec[1] + tvec[2]*pvec[2]) * inv;
    if (u < 0.0f || u > 1.0f) return;
    float qvec[3] = {
        tvec[1]*e1[2] - tvec[2]*e1[1],
        tvec[2]*e1[0] - tvec[0]*e1[2],
        tvec[0]*e1[1] - tvec[1]*e1[0]
    };
    float v = (dir[0]*qvec[0] + dir[1]*qvec[1] + dir[2]*qvec[2]) * inv;
    if (v < 0.0f || u + v > 1.0f) return;
    float tt = (e2[0]*qvec[0] + e2[1]*qvec[1] + e2[2]*qvec[2]) * inv;
    if (tt <= EPS || tt >= *best) return;
    *best = tt;
    *found = 1;
    *best_tri = (int32_t)t;
}

/* Ray against the collision mesh, walking the XY grid the height query has
 * always used.
 *
 * This used to test EVERY triangle in the map, which is fine at a handful of
 * rays a frame -- the player casts a few -- and ruinous at sixty. Spent brass
 * collides, lives thirty seconds and asks for a ray each frame it is alive,
 * so a floor littered with casings was running tens of millions of triangle
 * tests a frame. That, not fill, was what took the phone from 120 fps to 14.
 *
 * A particle's ray is centimetres long and touches one cell. A bullet's
 * crosses the map and touches a diagonal of them, and the walk stops as soon
 * as the nearest hit so far is closer than the next cell can possibly be. */
static bool collision_ray_static(const hta_collision *c,
                                const float orig[3], const float dir[3], float max_t,
                                float *out_t, float hit[3], float nrm[3],
                                uint8_t *out_material)
{
    if (out_material) *out_material = HTA_MATERIAL_NONE;
    if (!c || !c->built || !orig || !dir) return false;
    float best = max_t;
    int found = 0;
    int32_t best_tri = -1;
    float bn[3] = {0, 0, 1};

    if (!c->cell_start || !c->tri_index || c->cell <= 0.0f) {
        for (uint32_t t = 0; t < c->tri_count; t++)
            ray_tri(c, t, orig, dir, &best, &found, &best_tri);
    } else {
        /* 2D DDA over the grid. A triangle straddling a cell boundary is
         * listed in both and may be tested twice; that costs a little and
         * cannot change which hit is nearest. */
        float px = (orig[0] - c->min[0]) / c->cell;
        float py = (orig[1] - c->min[1]) / c->cell;
        int cx = (int)floorf(px), cy = (int)floorf(py);
        float dx = dir[0] / c->cell, dy = dir[1] / c->cell;

        int stepx = dx > 0.0f ? 1 : (dx < 0.0f ? -1 : 0);
        int stepy = dy > 0.0f ? 1 : (dy < 0.0f ? -1 : 0);
        /* Distance along the ray to the next boundary in each axis, and how
         * far one whole cell is. INFINITY where the ray does not move in
         * that axis, which is what makes a straight-down ray work. */
        float tmaxx = INFINITY, tdx = INFINITY;
        float tmaxy = INFINITY, tdy = INFINITY;
        if (stepx) {
            float next = (float)(cx + (stepx > 0 ? 1 : 0));
            tmaxx = (next - px) / dx;
            tdx = (float)stepx / dx;
        }
        if (stepy) {
            float next = (float)(cy + (stepy > 0 ? 1 : 0));
            tmaxy = (next - py) / dy;
            tdy = (float)stepy / dy;
        }

        float t_enter = 0.0f;
        for (uint32_t guard = 0; guard < 4096u; guard++) {
            if (cx >= 0 && cy >= 0 && cx < (int)c->nx && cy < (int)c->ny) {
                uint32_t ci = (uint32_t)cy * c->nx + (uint32_t)cx;
                uint32_t k0 = c->cell_start[ci], k1 = c->cell_start[ci + 1u];
                for (uint32_t k = k0; k < k1; k++)
                    ray_tri(c, c->tri_index[k], orig, dir, &best, &found, &best_tri);
            }
            /* Nothing further along the ray can beat what we already have. */
            if (found && best <= t_enter) break;
            float t_next = tmaxx < tmaxy ? tmaxx : tmaxy;
            if (t_next > max_t || t_next == INFINITY) break;
            if (tmaxx < tmaxy) { cx += stepx; t_enter = tmaxx; tmaxx += tdx; }
            else               { cy += stepy; t_enter = tmaxy; tmaxy += tdy; }
            /* Once outside the grid in the direction of travel, stop. */
            if ((cx < 0 && stepx <= 0) || (cx >= (int)c->nx && stepx >= 0)) break;
            if ((cy < 0 && stepy <= 0) || (cy >= (int)c->ny && stepy >= 0)) break;
        }
    }

    if (found && best_tri >= 0) {
        uint32_t t = (uint32_t)best_tri;
        const float *v0 = c->verts[c->indices[t * 3 + 0]].pos;
        const float *v1 = c->verts[c->indices[t * 3 + 1]].pos;
        const float *v2 = c->verts[c->indices[t * 3 + 2]].pos;
        float e1[3] = { v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2] };
        float e2[3] = { v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2] };
        float nx = e1[1]*e2[2] - e1[2]*e2[1];
        float ny = e1[2]*e2[0] - e1[0]*e2[2];
        float nz = e1[0]*e2[1] - e1[1]*e2[0];
        float nl = sqrtf(nx*nx + ny*ny + nz*nz);
        if (nl > 1e-8f) { nx /= nl; ny /= nl; nz /= nl; }
        if (nx*dir[0] + ny*dir[1] + nz*dir[2] > 0) { nx = -nx; ny = -ny; nz = -nz; }
        bn[0] = nx; bn[1] = ny; bn[2] = nz;
    }
    if (!found) return false;
    if (out_t) *out_t = best;
    if (hit) {
        hit[0] = orig[0] + dir[0] * best;
        hit[1] = orig[1] + dir[1] * best;
        hit[2] = orig[2] + dir[2] * best;
    }
    if (nrm) { nrm[0] = bn[0]; nrm[1] = bn[1]; nrm[2] = bn[2]; }
    if (out_material && c->tri_material && best_tri >= 0)
        *out_material = c->tri_material[best_tri];
    return true;
}

/* ---- placed rigid grids --------------------------------------------- */

static void inst_to_local(const hta_collision_instance *in, const float w[3], float l[3])
{
    float d[3] = { w[0]-in->pos[0], w[1]-in->pos[1], w[2]-in->pos[2] };
    /* rot is local->world, so its transpose takes world to local. */
    for (int k = 0; k < 3; k++)
        l[k] = in->rot[0*3+k]*d[0] + in->rot[1*3+k]*d[1] + in->rot[2*3+k]*d[2];
}
static void inst_dir_to_local(const hta_collision_instance *in, const float w[3], float l[3])
{
    for (int k = 0; k < 3; k++)
        l[k] = in->rot[0*3+k]*w[0] + in->rot[1*3+k]*w[1] + in->rot[2*3+k]*w[2];
}
static void inst_dir_to_world(const hta_collision_instance *in, const float l[3], float w[3])
{
    for (int k = 0; k < 3; k++)
        w[k] = in->rot[k*3+0]*l[0] + in->rot[k*3+1]*l[1] + in->rot[k*3+2]*l[2];
}
static bool inst_near_xy(const hta_collision_instance *in, float x, float y, float pad)
{
    if (!in->active || !in->grid || !in->grid->built) return false;
    float dx = x - in->pos[0], dy = y - in->pos[1], r = in->radius + pad;
    return dx*dx + dy*dy <= r*r;
}

/* The ground of a placed grid, taken along the grid's own up. Vehicles tilt
 * a few degrees, so local "down" is close enough to world down to stand on. */
static bool inst_ground(const hta_collision_instance *in, float x, float y, float from,
                        float *out_z, uint8_t *mat)
{
    if (!inst_near_xy(in, x, y, 0.5f)) return false;
    float w[3] = { x, y, from }, l[3], lz;
    inst_to_local(in, w, l);
    int32_t t = ground_tri(in->grid, l[0], l[1], l[2], &lz);
    if (t < 0) return false;
    float lp[3] = { l[0], l[1], lz }, wd[3];
    inst_dir_to_world(in, lp, wd);
    *out_z = wd[2] + in->pos[2];
    if (mat) *mat = in->grid->tri_material ? in->grid->tri_material[t] : HTA_MATERIAL_NONE;
    return true;
}

bool hta_collision_ground(const hta_collision *c, float x, float y, float from,
                           float *out)
{
    if (!c || !out) return false;
    float z = -INFINITY, ez = -INFINITY;
    bool found = collision_ground_static(c, x, y, from, &z);
    if (c->extra && hta_collision_ground(c->extra, x, y, from, &ez)) {
        z = found ? fmaxf(z, ez) : ez; found = true;
    }
    for (uint32_t i = 0; i < c->instance_count; i++)
        if (inst_ground(&c->instances[i], x, y, from, &ez, NULL)) {
            z = found ? fmaxf(z, ez) : ez; found = true;
        }
    if (found) *out = z;
    return found;
}

static void inst_depenetrate(const hta_collision_instance *in, float *x, float *y,
                             float z, float height, float radius)
{
    if (!inst_near_xy(in, *x, *y, radius + 0.1f)) return;
    float w[3] = { *x, *y, z }, l[3];
    inst_to_local(in, w, l);
    float lx = l[0], ly = l[1];
    collision_depenetrate_static(in->grid, &lx, &ly, l[2], height, radius);
    if (lx == l[0] && ly == l[1]) return;
    float d[3] = { lx - l[0], ly - l[1], 0.0f }, wd[3];
    inst_dir_to_world(in, d, wd);
    *x += wd[0]; *y += wd[1];
}

void hta_collision_depenetrate(const hta_collision *c, float *x, float *y,
                                float z, float height, float radius)
{
    if (!c) return;
    collision_depenetrate_static(c, x, y, z, height, radius);
    bool moved = false;
    if (c->extra) {
        hta_collision_depenetrate(c->extra, x, y, z, height, radius);
        moved = true;
    }
    for (uint32_t i = 0; i < c->instance_count; i++) {
        float ox = *x, oy = *y;
        inst_depenetrate(&c->instances[i], x, y, z, height, radius);
        if (ox != *x || oy != *y) moved = true;
    }
    /* Pushed out of a vehicle and into a wall is no good: the world wins. */
    if (moved) collision_depenetrate_static(c, x, y, z, height, radius);
}

bool hta_collision_ray_material(const hta_collision *c, const float orig[3],
    const float dir[3], float max_t, float *out_t, float hit[3], float nrm[3], uint8_t *mat)
{
    if (!c) return false;
    float t = max_t;
    bool found = collision_ray_static(c, orig, dir, max_t, &t, hit, nrm, mat);
    float et, eh[3], en[3]; uint8_t em = HTA_MATERIAL_NONE;
    if (c->extra && hta_collision_ray_material(c->extra, orig, dir, t, &et, eh, en, &em)) {
        t = et; found = true;
        if (hit) memcpy(hit, eh, sizeof(eh));
        if (nrm) memcpy(nrm, en, sizeof(en));
        if (mat) *mat = em;
    }
    for (uint32_t i = 0; i < c->instance_count; i++) {
        const hta_collision_instance *in = &c->instances[i];
        if (!in->active || !in->grid || !in->grid->built) continue;
        /* Sphere cull: does the segment pass within the bound? */
        float dl2 = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
        if (dl2 < 1e-12f) continue;
        float oc[3] = { in->pos[0]-orig[0], in->pos[1]-orig[1], in->pos[2]-orig[2] };
        float s = (oc[0]*dir[0] + oc[1]*dir[1] + oc[2]*dir[2]) / dl2;
        if (s < 0.0f) s = 0.0f;
        if (s > t) s = t;
        float q[3] = { orig[0]+dir[0]*s - in->pos[0], orig[1]+dir[1]*s - in->pos[1],
                       orig[2]+dir[2]*s - in->pos[2] };
        if (q[0]*q[0] + q[1]*q[1] + q[2]*q[2] > in->radius * in->radius) continue;
        float lo[3], ld[3];
        inst_to_local(in, orig, lo);
        inst_dir_to_local(in, dir, ld);
        float it = t, ln[3];
        uint8_t im = HTA_MATERIAL_NONE;
        if (!collision_ray_static(in->grid, lo, ld, t, &it, NULL, ln, &im) || it >= t) continue;
        t = it; found = true;
        if (hit) for (int k = 0; k < 3; k++) hit[k] = orig[k] + dir[k] * it;
        if (nrm) inst_dir_to_world(in, ln, nrm);
        if (mat) *mat = im;
    }
    if (found && out_t) *out_t = t;
    return found;
}

bool hta_collision_ray(const hta_collision *c,
                       const float orig[3], const float dir[3], float max_t,
                       float *out_t, float hit[3], float nrm[3])
{
    return hta_collision_ray_material(c, orig, dir, max_t, out_t, hit, nrm, NULL);
}

/* ------------------------------ player ------------------------------ */

void hta_player_init(hta_player *p)
{
    memset(p, 0, sizeof(*p));
    p->zoom = 1.0f;
    hta_player_physics_defaults(&p->phys);
    hta_player_apply_physics(p, &p->phys);
}

void hta_player_apply_physics(hta_player *p, const hta_player_physics *phys)
{
    if (!p || !phys) return;
    p->phys = *phys;
    p->eye_height = phys->cam_stand;
    p->radius     = phys->radius;
    p->walk_speed = phys->run_forward;
    p->jump_speed = phys->jump_speed;
    p->gravity    = phys->gravity;
}

static void accel_toward(float *vx, float *vy, float tx, float ty, float a, float dt)
{
    float dx = tx - *vx, dy = ty - *vy;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = a * dt;
    if (dist <= step || dist < 1e-6f) { *vx = tx; *vy = ty; return; }
    *vx += dx / dist * step;
    *vy += dy / dist * step;
}

void hta_player_spawn(hta_player *p, const hta_spawn_point *sp)
{
    if (!p || !sp) return;
    p->pos[0] = sp->position[0];
    p->pos[1] = sp->position[1];
    p->pos[2] = sp->position[2];
    p->velocity[0] = p->velocity[1] = p->velocity[2] = 0.0f;
    p->on_ground = false;
}

void hta_player_set_zoom(hta_player *p, float magnification)
{
    if (!p) return;
    p->zoom = magnification > 1.0f ? magnification : 1.0f;
}

void hta_player_update(hta_player *p, hta_camera *cam, const hta_collision *col,
                       const hta_player_input *in, float dt)
{
    if (!p || !cam || !in) return;
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;    /* never let a hitch teleport the player */

    hta_camera_look(cam, in->look_yaw, in->look_pitch);
    cam->fov_y = p->phys.fov_y / (p->zoom > 1.0f ? p->zoom : 1.0f);

    float target_crouch = in->crouch ? 1.0f : 0.0f;
    float crate = (p->phys.crouch_time > 1e-3f) ? (dt / p->phys.crouch_time) : 1.0f;
    if (target_crouch > p->crouch_t) {
        p->crouch_t += crate;
        if (p->crouch_t > 1.0f) p->crouch_t = 1.0f;
    } else {
        p->crouch_t -= crate;
        if (p->crouch_t < 0.0f) p->crouch_t = 0.0f;
    }
    p->eye_height = p->phys.cam_stand + (p->phys.cam_crouch - p->phys.cam_stand) * p->crouch_t;
    p->radius = p->phys.radius;

    bool  was_air_this_update = false;
    float fall_speed_in = 0.0f;
    float step_start_x = p->pos[0], step_start_y = p->pos[1];

    float fwd[3], right[3];
    hta_camera_forward(cam, fwd);
    hta_camera_right(cam, right);
    /* flatten forward so looking up does not slow you down */
    float fl = sqrtf(fwd[0]*fwd[0] + fwd[1]*fwd[1]);
    if (fl > 1e-5f) { fwd[0] /= fl; fwd[1] /= fl; }
    fwd[2] = 0.0f;

    int sneak = in->crouch && p->on_ground;
    float spd_f = sneak ? p->phys.sneak_forward : p->phys.run_forward;
    float spd_b = sneak ? p->phys.sneak_back    : p->phys.run_back;
    float spd_s = sneak ? p->phys.sneak_side    : p->phys.run_side;
    float accel = sneak ? p->phys.sneak_accel   : p->phys.run_accel;
    if (!p->on_ground) accel = p->phys.air_accel;

    float mf = in->move_forward, mr = in->move_right;
    float wishx = fwd[0] * (mf >= 0.0f ? mf * spd_f : mf * spd_b)
                + right[0] * (mr * spd_s);
    float wishy = fwd[1] * (mf >= 0.0f ? mf * spd_f : mf * spd_b)
                + right[1] * (mr * spd_s);
    float maxspd = spd_f > spd_s ? spd_f : spd_s;
    float wl = sqrtf(wishx * wishx + wishy * wishy);
    if (wl > maxspd && wl > 1e-6f) { wishx *= maxspd / wl; wishy *= maxspd / wl; }

    if (p->noclip) {
        float f3[3];
        hta_camera_forward(cam, f3);
        float ns = p->phys.run_forward * 6.0f;
        p->pos[0] += (f3[0]*in->move_forward + right[0]*in->move_right) * ns * dt;
        p->pos[1] += (f3[1]*in->move_forward + right[1]*in->move_right) * ns * dt;
        p->pos[2] += (f3[2]*in->move_forward) * ns * dt;
        if (in->jump) p->pos[2] += ns * dt;
        p->velocity[0] = p->velocity[1] = p->velocity[2] = 0.0f;
        p->on_ground = false;
    } else {
        bool was_walking;
        if (p->fly) {
            /* Along the look, pitch and all, like a Banshee; a lighter hand
             * sideways; straight up or down on JUMP and CROUCH. */
            float f3[3];
            hta_camera_forward(cam, f3);
            float sp = p->fly_speed > 0.1f ? p->fly_speed : 5.0f;
            float want[3];
            for (int k = 0; k < 3; k++)
                want[k] = (f3[k] * in->move_forward + right[k] * in->move_right * 0.7f) * sp;
            if (in->jump) want[2] += sp * 0.6f;
            if (in->crouch) want[2] -= sp * 0.6f;
            float a = sp * 3.0f * dt;            /* full speed from rest in 1/3 s */
            for (int k = 0; k < 3; k++) {
                float d = want[k] - p->velocity[k];
                p->velocity[k] += d > a ? a : d < -a ? -a : d;
            }
            if (p->velocity[2] > 0.05f) p->on_ground = false;
            was_walking = false;
        } else {
        accel_toward(&p->velocity[0], &p->velocity[1], wishx, wishy, accel, dt);

        /* Was the pawn walking, rather than falling or jumping, when this
         * frame began? Only then may it be stuck to a descending surface. */
        was_walking = p->on_ground && !in->jump;
        if (in->jump && p->on_ground) { p->velocity[2] = p->jump_speed; p->on_ground = false; }
        p->velocity[2] -= p->gravity * dt;
        }
        if (p->velocity[2] < -40.0f) p->velocity[2] = -40.0f;

        was_air_this_update = !p->on_ground;
        /* How fast we are going down BEFORE the ground zeroes it, which is
         * what a landing has to be judged on. */
        if (p->velocity[2] < 0.0f && !p->fly) fall_speed_in = -p->velocity[2];   /* a broom lands, never falls */
        step_start_x = p->pos[0];
        step_start_y = p->pos[1];
        float oz = p->pos[2];
        p->pos[0] += p->velocity[0] * dt;
        p->pos[1] += p->velocity[1] * dt;
        p->pos[2] += p->velocity[2] * dt;
        /* Probe from the higher of old/new Z so a fast fall cannot skip the floor. */
        float probe_z = (oz > p->pos[2]) ? oz : p->pos[2];

        float gz;
        if (col && col->built) {
            /* Steep faces are walls (depenetrate), not floors. No walkable
             * triangle at the new XY means a drop — fall, do not slide back
             * onto the pad (that was the invisible wall at base edges). */
            float ph = p->phys.coll_stand + (p->phys.coll_crouch - p->phys.coll_stand) * p->crouch_t;

            /* Stick to a surface that drops away under you, BEFORE measuring
             * the body column against walls.
             *
             * Gravity alone does not keep a walking pawn on a downward slope:
             * in one frame the ground falls further than a standing start
             * falls, so the pawn leaves it, and keeps leaving it, floating a
             * little all the way down. That float lifts the head by the same
             * amount -- so a roof the pawn cleared walking UP the ramp pushes
             * it back walking DOWN, which is a doorway that only works one
             * way. It also reports "air" for the whole descent, which costs
             * the jump and the sneak speed.
             *
             * Only ever snap as far as the steepest walkable slope could have
             * carried the pawn in this frame, plus the step allowance. Walk
             * off a real ledge and the drop is further than that: you fall. */
            if (was_walking && p->velocity[2] <= 0.0f) {
                float vx = p->velocity[0], vy = p->velocity[1];
                float moved = sqrtf(vx * vx + vy * vy) * dt;
                float nz = (col->walkable_nz > 0.1f) ? col->walkable_nz : 0.50f;
                float tan_slope = sqrtf(1.0f - nz * nz) / nz;
                float reach = 0.18f + moved * tan_slope;
                float sgz;
                if (hta_collision_ground(col, p->pos[0], p->pos[1], probe_z, &sgz)
                    && p->pos[2] > sgz && p->pos[2] - sgz <= reach) {
                    p->pos[2] = sgz;
                    p->velocity[2] = 0.0f;
                    p->on_ground = true;
                }
            }

            float bx = p->pos[0], by = p->pos[1];
            hta_collision_depenetrate(col, &p->pos[0], &p->pos[1], p->pos[2], ph, p->radius);
            /* Never pushed out into the void. A one-sided wall's corner
             * (an imported map's outer wall has no back face) can shove a
             * body through it into space with nothing under it at all; a
             * push that leaves every floor behind is refused. A drop to
             * any floor, however far down, is still allowed. */
            if ((p->pos[0] != bx || p->pos[1] != by) &&
                !hta_collision_ground(col, p->pos[0], p->pos[1], probe_z, &gz) &&
                hta_collision_ground(col, bx, by, probe_z, &gz)) {
                p->pos[0] = bx; p->pos[1] = by;
            }
            /* Cancel velocity into the wall or the next frame sinks back in. */
            float pdx = p->pos[0] - bx, pdy = p->pos[1] - by;
            float plen2 = pdx * pdx + pdy * pdy;
            if (plen2 > 1e-12f) {
                float inv = 1.0f / sqrtf(plen2);
                pdx *= inv;
                pdy *= inv;
                float vn = p->velocity[0] * pdx + p->velocity[1] * pdy;
                if (vn < 0.0f) {
                    p->velocity[0] -= vn * pdx;
                    p->velocity[1] -= vn * pdy;
                }
            }

            if (hta_collision_ground(col, p->pos[0], p->pos[1], probe_z, &gz)) {
                if (p->pos[2] <= gz) {
                    p->pos[2] = gz;
                    if (p->velocity[2] < 0.0f) p->velocity[2] = 0.0f;
                    p->on_ground = true;
                } else {
                    p->on_ground = false;
                }
            } else {
                p->on_ground = false;
            }
        }
        /* no mesh: leave on_ground as-is so tag-physics tests can stay grounded */
    }

    /* Footsteps are paced by ground covered, so they keep step with you
     * whatever your speed, and stop dead when you do. A landing is its own
     * footfall however far you travelled getting there. */
    p->footstep = false;
    p->landed = false;
    if (!p->noclip && p->on_ground) {
        if (was_air_this_update) {
            p->landed = true;
            /* How hard. The vertical speed is already zeroed by the
             * landing itself, so this is what it was on the way in. */
            p->land_speed = fall_speed_in;
            p->footstep = true;
            p->step_distance = 0.0f;
        } else {
            float sdx = p->pos[0] - step_start_x, sdy = p->pos[1] - step_start_y;
            p->step_distance += sqrtf(sdx * sdx + sdy * sdy);
            if (p->step_distance >= HTA_STEP_LENGTH) {
                p->step_distance -= HTA_STEP_LENGTH;
                p->footstep = true;
            }
        }
    } else if (!p->on_ground) {
        p->step_distance = 0.0f;
    }

    cam->pos[0] = p->pos[0];
    cam->pos[1] = p->pos[1];
    cam->pos[2] = p->pos[2] + p->eye_height;
}
