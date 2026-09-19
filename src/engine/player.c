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

bool hta_collision_build(hta_collision *c, const hta_bsp_mesh *mesh)
{
    if (!c || !mesh || !mesh->vertices || !mesh->indices || mesh->index_count < 3) return false;
    memset(c, 0, sizeof(*c));

    c->verts     = mesh->vertices;
    c->indices   = mesh->indices;
    c->tri_count = mesh->index_count / 3;
    c->walkable_nz = 0.50f;

    float ex = mesh->bounds_max[0] - mesh->bounds_min[0];
    float ey = mesh->bounds_max[1] - mesh->bounds_min[1];
    if (!(ex > 0.0f) || !(ey > 0.0f)) return false;

    float span = ex > ey ? ex : ey;
    c->cell = span / (float)GRID_TARGET_CELLS;
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

bool hta_collision_ground(const hta_collision *c, float x, float y, float z_from,
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

static void closest_on_tri(const float a[3], const float b[3], const float c[3],
                           const float p[3], float q[3])
{
    float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float ac[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    float ap[3] = { p[0]-a[0], p[1]-a[1], p[2]-a[2] };
    float d1 = ab[0]*ap[0]+ab[1]*ap[1]+ab[2]*ap[2];
    float d2 = ac[0]*ap[0]+ac[1]*ap[1]+ac[2]*ap[2];
    if (d1 <= 0.0f && d2 <= 0.0f) { q[0]=a[0]; q[1]=a[1]; q[2]=a[2]; return; }
    float bp[3] = { p[0]-b[0], p[1]-b[1], p[2]-b[2] };
    float d3 = ab[0]*bp[0]+ab[1]*bp[1]+ab[2]*bp[2];
    float d4 = ac[0]*bp[0]+ac[1]*bp[1]+ac[2]*bp[2];
    if (d3 >= 0.0f && d4 <= d3) { q[0]=b[0]; q[1]=b[1]; q[2]=b[2]; return; }
    float vc = d1*d4 - d3*d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        q[0]=a[0]+ab[0]*v; q[1]=a[1]+ab[1]*v; q[2]=a[2]+ab[2]*v; return;
    }
    float cp[3] = { p[0]-c[0], p[1]-c[1], p[2]-c[2] };
    float d5 = ab[0]*cp[0]+ab[1]*cp[1]+ab[2]*cp[2];
    float d6 = ac[0]*cp[0]+ac[1]*cp[1]+ac[2]*cp[2];
    if (d6 >= 0.0f && d5 <= d6) { q[0]=c[0]; q[1]=c[1]; q[2]=c[2]; return; }
    float vb = d5*d2 - d1*d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        q[0]=a[0]+ac[0]*w; q[1]=a[1]+ac[1]*w; q[2]=a[2]+ac[2]*w; return;
    }
    float va = d3*d6 - d5*d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        q[0]=b[0]+(c[0]-b[0])*w; q[1]=b[1]+(c[1]-b[1])*w; q[2]=b[2]+(c[2]-b[2])*w; return;
    }
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    q[0]=a[0]+ab[0]*v+ac[0]*w; q[1]=a[1]+ab[1]*v+ac[1]*w; q[2]=a[2]+ab[2]*v+ac[2]*w;
}

void hta_collision_depenetrate(const hta_collision *c,
                               float *x, float *y, float z_feet,
                               float height, float radius)
{
    if (!c || !c->built || !x || !y || radius <= 0.0f) return;
    float walk = (c->walkable_nz > 0.1f) ? c->walkable_nz : 0.50f;
    float z0 = z_feet;
    float z1 = z_feet + (height > 1e-4f ? height : 0.7f);
    float zc = 0.5f * (z0 + z1);
    float r = radius;
    float r2 = r * r;
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
                float q[3], p[3] = { px, py, zc };
                closest_on_tri(a, b, d, p, q);
                float az = q[2];
                if (az < z0) az = z0;
                if (az > z1) az = z1;
                float dx = px - q[0], dy = py - q[1], dz = az - q[2];
                float xy2 = dx * dx + dy * dy;
                float dist2 = (fabsf(dz) < 1e-4f) ? xy2 : (xy2 + dz * dz);
                if (dist2 >= r2) continue;
                float hx, hy, hl, push;
                if (dist2 < 1e-12f) {
                    hx = nx / nlen;
                    hy = ny / nlen;
                    hl = sqrtf(hx * hx + hy * hy);
                    if (hl < 1e-5f) continue;
                    hx /= hl;
                    hy /= hl;
                    push = r;
                } else {
                    float dist = sqrtf(dist2);
                    push = r - dist;
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

bool hta_collision_ray(const hta_collision *c,
                       const float orig[3], const float dir[3], float max_t,
                       float *out_t, float hit[3], float nrm[3])
{
    if (!c || !c->built || !orig || !dir) return false;
    float best = max_t;
    int found = 0;
    float bn[3] = {0, 0, 1};
    const float EPS = 1e-7f;
    for (uint32_t t = 0; t < c->tri_count; t++) {
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
        if (det > -EPS && det < EPS) continue;
        float inv = 1.0f / det;
        float tvec[3] = { orig[0]-v0[0], orig[1]-v0[1], orig[2]-v0[2] };
        float u = (tvec[0]*pvec[0] + tvec[1]*pvec[1] + tvec[2]*pvec[2]) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        float qvec[3] = {
            tvec[1]*e1[2] - tvec[2]*e1[1],
            tvec[2]*e1[0] - tvec[0]*e1[2],
            tvec[0]*e1[1] - tvec[1]*e1[0]
        };
        float v = (dir[0]*qvec[0] + dir[1]*qvec[1] + dir[2]*qvec[2]) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        float tt = (e2[0]*qvec[0] + e2[1]*qvec[1] + e2[2]*qvec[2]) * inv;
        if (tt <= EPS || tt >= best) continue;
        best = tt;
        found = 1;
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
    return true;
}

/* ------------------------------ player ------------------------------ */

void hta_player_init(hta_player *p)
{
    memset(p, 0, sizeof(*p));
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

void hta_player_update(hta_player *p, hta_camera *cam, const hta_collision *col,
                       const hta_player_input *in, float dt)
{
    if (!p || !cam || !in) return;
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;    /* never let a hitch teleport the player */

    hta_camera_look(cam, in->look_yaw, in->look_pitch);
    cam->fov_y = p->phys.fov_y;

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
        accel_toward(&p->velocity[0], &p->velocity[1], wishx, wishy, accel, dt);

        if (in->jump && p->on_ground) { p->velocity[2] = p->jump_speed; p->on_ground = false; }
        p->velocity[2] -= p->gravity * dt;
        if (p->velocity[2] < -40.0f) p->velocity[2] = -40.0f;

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
            float bx = p->pos[0], by = p->pos[1];
            hta_collision_depenetrate(col, &p->pos[0], &p->pos[1], p->pos[2], ph, p->radius);
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

    cam->pos[0] = p->pos[0];
    cam->pos[1] = p->pos[1];
    cam->pos[2] = p->pos[2] + p->eye_height;
}
