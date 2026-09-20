#include "gun.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void hta_gun_init(hta_gun *g)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->fire_interval = HTA_GUN_COOLDOWN;
    g->hit_material = HTA_MATERIAL_NONE;
    g->error_accel = 0.6f;
    g->error_decel = 1.0f;
    g->since_shot = 1e6f;
    g->rng = 0x2545F491u;
}

void hta_gun_set_error(hta_gun *g, const float error_angle[2],
                       float accel, float decel)
{
    if (!g || !error_angle) return;
    g->error_angle[0] = error_angle[0];
    g->error_angle[1] = error_angle[1];
    /* A tag with no ramp time would divide by zero and snap straight to the
     * wide cone on the first round. */
    if (accel > 1e-3f) g->error_accel = accel;
    if (decel > 1e-3f) g->error_decel = decel;
}

float hta_gun_spread(const hta_gun *g)
{
    if (!g) return 0.0f;
    float a = g->error_angle[0];
    float b = g->error_angle[1];
    if (b < a) b = a;
    return a + (b - a) * g->error;
}

void hta_gun_free(hta_gun *g)
{
    if (!g) return;
    hta_bsp_free(&g->mesh);
    free(g->decal_rgba);
    memset(g, 0, sizeof(*g));
}

void hta_gun_set_decal(hta_gun *g, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    if (!g) return;
    free(g->decal_rgba);
    g->decal_rgba = NULL;
    g->decal_w = g->decal_h = 0;
    if (rgba && w && h) {
        size_t bytes = (size_t)w * h * 4u;
        g->decal_rgba = (uint8_t *)malloc(bytes);
        if (g->decal_rgba) {
            memcpy(g->decal_rgba, rgba, bytes);
            g->decal_w = w;
            g->decal_h = h;
        }
    }
    g->dirty = 1;
}

void hta_gun_update(hta_gun *g, float dt)
{
    if (!g) return;
    if (dt < 0.0f) dt = 0.0f;
    g->cooldown -= dt;
    if (g->cooldown < 0.0f) g->cooldown = 0.0f;

    /* Still firing if a round left within about one shot's time; a burst at
     * the tagged rate of fire therefore keeps blooming between rounds. */
    g->since_shot += dt;
    float gap = g->fire_interval > 0.01f ? g->fire_interval : HTA_GUN_COOLDOWN;
    if (g->since_shot <= gap * 1.5f) g->error += dt / g->error_accel;
    else                             g->error -= dt / g->error_decel;
    if (g->error > 1.0f) g->error = 1.0f;
    if (g->error < 0.0f) g->error = 0.0f;
}

/* A direction inside a cone of `half_angle` about `in`. The polar angle is
 * drawn as angle*sqrt(u) so shots land evenly across the disc rather than
 * bunching at the centre. */
static void spread_dir(hta_gun *g, const float in[3], float half_angle, float out[3])
{
    out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
    if (half_angle <= 1e-6f) return;

    /* An orthonormal basis around the aim direction. */
    float up[3] = { 0.0f, 0.0f, 1.0f };
    if (fabsf(in[2]) > 0.99f) { up[0] = 1.0f; up[2] = 0.0f; }
    float r[3] = { in[1]*up[2] - in[2]*up[1],
                   in[2]*up[0] - in[0]*up[2],
                   in[0]*up[1] - in[1]*up[0] };
    float rl = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    if (rl < 1e-6f) return;
    r[0] /= rl; r[1] /= rl; r[2] /= rl;
    float u2[3] = { in[1]*r[2] - in[2]*r[1],
                    in[2]*r[0] - in[0]*r[2],
                    in[0]*r[1] - in[1]*r[0] };

    g->rng = g->rng * 1664525u + 1013904223u;
    float a = (float)((g->rng >> 8) & 0xFFFFFFu) / (float)0x1000000;
    g->rng = g->rng * 1664525u + 1013904223u;
    float b = (float)((g->rng >> 8) & 0xFFFFFFu) / (float)0x1000000;

    float theta = half_angle * sqrtf(a);
    float phi = b * 6.28318531f;
    float st = sinf(theta), ct = cosf(theta);
    float cp = cosf(phi), sp = sinf(phi);
    for (int i = 0; i < 3; i++)
        out[i] = in[i] * ct + (r[i] * cp + u2[i] * sp) * st;
    float l = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (l > 1e-6f) { out[0] /= l; out[1] /= l; out[2] /= l; }
}

int hta_gun_ready(const hta_gun *g)
{
    return g && g->cooldown <= 0.0f;
}

int hta_gun_fire(hta_gun *g, const hta_collision *col, const hta_camera *cam)
{
    if (!g || !cam) return 0;
    if (g->cooldown > 0.0f) return 0;
    g->cooldown = (g->fire_interval > 0.02f) ? g->fire_interval : HTA_GUN_COOLDOWN;
    float aim[3], dir[3];
    hta_camera_forward(cam, aim);
    /* Where the round actually goes: inside the trigger's own error cone,
     * which widens while the trigger is held. */
    hta_gun_shot_dir(g, aim, dir);
    g->since_shot = 0.0f;
    float hit[3], nrm[3], t;
    g->hit_material = HTA_MATERIAL_NONE;
    if (!hta_collision_ray_material(col, cam->pos, dir, HTA_GUN_RANGE, &t, hit, nrm,
                                    &g->hit_material))
        return 1; /* shot fired, missed */
    hta_gun_add_mark(g, hit, nrm, HTA_MARK_SIZE);
    return 1;
}

void hta_gun_add_mark(hta_gun *g, const float hit[3], const float nrm[3],
                      float size)
{
    if (!g || !hit || !nrm) return;
    if (!(size > 0.0f)) size = HTA_MARK_SIZE;
    uint32_t i = g->next % HTA_GUN_MAX_HITS;
    g->hits[i].pos[0] = hit[0] + nrm[0] * 0.02f;
    g->hits[i].pos[1] = hit[1] + nrm[1] * 0.02f;
    g->hits[i].pos[2] = hit[2] + nrm[2] * 0.02f;
    g->hits[i].nrm[0] = nrm[0];
    g->hits[i].nrm[1] = nrm[1];
    g->hits[i].nrm[2] = nrm[2];
    g->hits[i].size = size;
    for (int k = 0; k < 3; k++) {
        g->last_hit[k] = hit[k];
        g->last_nrm[k] = nrm[k];
    }
    g->next++;
    if (g->n < HTA_GUN_MAX_HITS) g->n++;
    g->dirty = 1;
}

int hta_gun_launch(hta_gun *g, const hta_camera *cam, float out_dir[3])
{
    if (!g || !cam || !out_dir) return 0;
    if (!hta_gun_ready(g)) return 0;
    g->cooldown = (g->fire_interval > 0.02f) ? g->fire_interval : HTA_GUN_COOLDOWN;
    float aim[3];
    hta_camera_forward(cam, aim);
    hta_gun_shot_dir(g, aim, out_dir);
    g->since_shot = 0.0f;
    g->hit_material = HTA_MATERIAL_NONE;
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
    /* Halo's own decal art when we have it -- the marks were a flat 1x1
     * square before, which is why bullet holes did not look like holes. */
    if (g->decal_rgba && g->decal_w && g->decal_h) {
        size_t bytes = (size_t)g->decal_w * g->decal_h * 4u;
        uint8_t *px = (uint8_t *)malloc(bytes);
        if (px) {
            memcpy(px, g->decal_rgba, bytes);
            g->mesh.textures[0].rgba = px;
            g->mesh.textures[0].width = g->decal_w;
            g->mesh.textures[0].height = g->decal_h;
            g->mesh.textures[0].tag_id = 1;
        }
    }
    if (!g->mesh.textures[0].rgba) {
        uint8_t *px = (uint8_t *)malloc(4);
        if (px) { px[0] = 32; px[1] = 28; px[2] = 24; px[3] = 255; }
        g->mesh.textures[0].rgba = px;
        g->mesh.textures[0].width = g->mesh.textures[0].height = 1;
        g->mesh.textures[0].tag_id = 1;
    }
    g->mesh.texture_count = 1;

    for (uint32_t i = 0; i < n; i++) {
        const float S = g->hits[i].size > 0.0f ? g->hits[i].size : HTA_MARK_SIZE;
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
    hta_submesh_init(&g->mesh.submeshes[0]);
    g->mesh.submeshes[0].albedo_tex = 0;
    g->mesh.submeshes[0].lightmap_tex = ~0u;
    /* A decal is a hole in a sheet of alpha, not a square of paint. Drawn
     * opaque, the transparent part of the bitmap came out as a solid patch
     * on the wall -- which is what "the bullet holes look strange" was. */
    g->mesh.submeshes[0].draw_mode = HTA_DRAW_ALPHA;
    g->dirty = 0;
}

void hta_gun_shot_dir(hta_gun *g, const float aim[3], float out[3])
{
    if (!g || !aim || !out) return;
    spread_dir(g, aim, hta_gun_spread(g), out);
}
