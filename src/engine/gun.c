#include "gun.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void hta_gun_init(hta_gun *g)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->fire_interval = HTA_GUN_COOLDOWN;
}

void hta_gun_free(hta_gun *g)
{
    if (!g) return;
    hta_bsp_free(&g->mesh);
    memset(g, 0, sizeof(*g));
}

void hta_gun_update(hta_gun *g, float dt)
{
    if (!g) return;
    if (dt < 0.0f) dt = 0.0f;
    g->cooldown -= dt;
    if (g->cooldown < 0.0f) g->cooldown = 0.0f;
}

int hta_gun_fire(hta_gun *g, const hta_collision *col, const hta_camera *cam)
{
    if (!g || !cam) return 0;
    if (g->cooldown > 0.0f) return 0;
    g->cooldown = (g->fire_interval > 0.02f) ? g->fire_interval : HTA_GUN_COOLDOWN;
    float dir[3];
    hta_camera_forward(cam, dir);
    float hit[3], nrm[3], t;
    if (!hta_collision_ray(col, cam->pos, dir, HTA_GUN_RANGE, &t, hit, nrm))
        return 1; /* shot fired, missed */
    uint32_t i = g->next % HTA_GUN_MAX_HITS;
    g->hits[i].pos[0] = hit[0] + nrm[0] * 0.02f;
    g->hits[i].pos[1] = hit[1] + nrm[1] * 0.02f;
    g->hits[i].pos[2] = hit[2] + nrm[2] * 0.02f;
    g->hits[i].nrm[0] = nrm[0];
    g->hits[i].nrm[1] = nrm[1];
    g->hits[i].nrm[2] = nrm[2];
    g->next++;
    if (g->n < HTA_GUN_MAX_HITS) g->n++;
    g->dirty = 1;
    return 1;
}

static void cross(float o[3], const float a[3], const float b[3])
{
    o[0] = a[1]*b[2] - a[2]*b[1];
    o[1] = a[2]*b[0] - a[0]*b[2];
    o[2] = a[0]*b[1] - a[1]*b[0];
}

static void nrm3(float v[3])
{
    float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (l < 1e-8f) return;
    v[0] /= l; v[1] /= l; v[2] /= l;
}

void hta_gun_build_mesh(hta_gun *g)
{
    if (!g) return;
    hta_bsp_free(&g->mesh);
    uint32_t n = g->n;
    if (n == 0) return;
    g->mesh.vertices = (hta_vertex *)calloc((size_t)n * 4u, sizeof(hta_vertex));
    g->mesh.indices  = (uint32_t *)calloc((size_t)n * 6u, sizeof(uint32_t));
    g->mesh.submeshes = (hta_submesh *)calloc(1, sizeof(hta_submesh));
    g->mesh.textures = (hta_bsp_texture *)calloc(1, sizeof(hta_bsp_texture));
    if (!g->mesh.vertices || !g->mesh.indices || !g->mesh.submeshes || !g->mesh.textures)
        return;
    uint8_t *px = (uint8_t *)malloc(4);
    if (px) { px[0] = 32; px[1] = 28; px[2] = 24; px[3] = 255; }
    g->mesh.textures[0].rgba = px;
    g->mesh.textures[0].width = g->mesh.textures[0].height = 1;
    g->mesh.textures[0].tag_id = 1;
    g->mesh.texture_count = 1;

    const float S = 0.035f;
    for (uint32_t i = 0; i < n; i++) {
        const float *p = g->hits[i].pos;
        float N[3] = { g->hits[i].nrm[0], g->hits[i].nrm[1], g->hits[i].nrm[2] };
        float up[3] = { 0, 0, 1 };
        if (fabsf(N[2]) > 0.92f) { up[0] = 1; up[2] = 0; }
        float r[3], f[3];
        cross(r, up, N); nrm3(r);
        cross(f, N, r); nrm3(f);
        hta_vertex *v = &g->mesh.vertices[i * 4];
        const float c[4][2] = { {-1,-1},{1,-1},{1,1},{-1,1} };
        for (int k = 0; k < 4; k++) {
            v[k].pos[0] = p[0] + (r[0]*c[k][0] + f[0]*c[k][1]) * S;
            v[k].pos[1] = p[1] + (r[1]*c[k][0] + f[1]*c[k][1]) * S;
            v[k].pos[2] = p[2] + (r[2]*c[k][0] + f[2]*c[k][1]) * S;
            v[k].normal[0] = N[0]; v[k].normal[1] = N[1]; v[k].normal[2] = N[2];
            v[k].uv[0] = (c[k][0]+1)*0.5f; v[k].uv[1] = (c[k][1]+1)*0.5f;
            v[k].lm_uv[0] = v[k].lm_uv[1] = 0;
        }
        uint32_t b = i * 4, *idx = &g->mesh.indices[i * 6];
        idx[0]=b; idx[1]=b+1; idx[2]=b+2; idx[3]=b; idx[4]=b+2; idx[5]=b+3;
    }
    g->mesh.vertex_count = n * 4;
    g->mesh.index_count = n * 6;
    g->mesh.submesh_count = 1;
    g->mesh.submeshes[0].first_index = 0;
    g->mesh.submeshes[0].index_count = n * 6;
    g->mesh.submeshes[0].albedo_tex = 0;
    g->mesh.submeshes[0].lightmap_tex = ~0u;
    g->mesh.submeshes[0].draw_mode = HTA_DRAW_OPAQUE;
    g->dirty = 0;
}
