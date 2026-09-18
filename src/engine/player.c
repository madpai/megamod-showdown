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
        if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0;
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
        if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0;
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
    const float HEAD_ROOM = 0.6f;   /* allow stepping slightly up */
    for (uint32_t k = s; k < e; k++) {
        uint32_t t = c->tri_index[k];
        const float *a = c->verts[c->indices[t*3+0]].pos;
        const float *b = c->verts[c->indices[t*3+1]].pos;
        const float *d = c->verts[c->indices[t*3+2]].pos;
        float z;
        if (!tri_height(a, b, d, x, y, &z)) continue;
        if (z <= z_from + HEAD_ROOM && z > best) { best = z; found = true; }
    }
    if (found) *out_z = best;
    return found;
}

/* ------------------------------ player ------------------------------ */

void hta_player_init(hta_player *p)
{
    memset(p, 0, sizeof(*p));
    /* Halo world units: 1 wu == 10 feet == 3.048 m. A ~1.8 m player is ~0.6 wu.
     * NOTE: this scale is inferred from community documentation and has not
     * been verified against real Trial data yet. */
    p->eye_height = 0.60f;
    p->radius     = 0.12f;
    p->walk_speed = 1.05f;    /* ~3.2 m/s */
    p->jump_speed = 1.25f;
    p->gravity    = 3.5f;
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

    float fwd[3], right[3];
    hta_camera_forward(cam, fwd);
    hta_camera_right(cam, right);
    /* flatten forward so looking up does not slow you down */
    float fl = sqrtf(fwd[0]*fwd[0] + fwd[1]*fwd[1]);
    if (fl > 1e-5f) { fwd[0] /= fl; fwd[1] /= fl; }
    fwd[2] = 0.0f;

    float wish[3];
    wish[0] = fwd[0]*in->move_forward + right[0]*in->move_right;
    wish[1] = fwd[1]*in->move_forward + right[1]*in->move_right;
    wish[2] = 0.0f;
    float wl = sqrtf(wish[0]*wish[0] + wish[1]*wish[1]);
    if (wl > 1.0f) { wish[0] /= wl; wish[1] /= wl; }

    if (p->noclip) {
        float f3[3];
        hta_camera_forward(cam, f3);
        p->pos[0] += (f3[0]*in->move_forward + right[0]*in->move_right) * p->walk_speed * 6.0f * dt;
        p->pos[1] += (f3[1]*in->move_forward + right[1]*in->move_right) * p->walk_speed * 6.0f * dt;
        p->pos[2] += (f3[2]*in->move_forward) * p->walk_speed * 6.0f * dt;
        if (in->jump) p->pos[2] += p->walk_speed * 6.0f * dt;
        p->velocity[0] = p->velocity[1] = p->velocity[2] = 0.0f;
        p->on_ground = false;
    } else {
        p->velocity[0] = wish[0] * p->walk_speed;
        p->velocity[1] = wish[1] * p->walk_speed;

        if (in->jump && p->on_ground) { p->velocity[2] = p->jump_speed; p->on_ground = false; }
        p->velocity[2] -= p->gravity * dt;
        if (p->velocity[2] < -40.0f) p->velocity[2] = -40.0f;

        p->pos[0] += p->velocity[0] * dt;
        p->pos[1] += p->velocity[1] * dt;
        p->pos[2] += p->velocity[2] * dt;

        float gz;
        if (col && col->built && hta_collision_ground(col, p->pos[0], p->pos[1], p->pos[2], &gz)) {
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

    cam->pos[0] = p->pos[0];
    cam->pos[1] = p->pos[1];
    cam->pos[2] = p->pos[2] + p->eye_height;
}
