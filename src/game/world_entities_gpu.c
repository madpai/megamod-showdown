/* World entities on the GPU (world_entities_gpu.h). */
#include "world_entities_gpu.h"
#include <stdlib.h>
#include <string.h>

/* The groups of entity `e` from the world mesh, as a mesh of their own:
 * vertices stay in world space (the instance adds the mover's offset),
 * only the textures they use come along. */
static hta_gfx_mesh *upload_one(hta_gfx *gfx, const hta_bsp_mesh *world, const uint16_t *owner, uint16_t e)
{
    uint32_t ni = 0, ns = 0;
    for (uint32_t i = 0; i < world->submesh_count; i++)
        if (owner[i] == e) { ni += world->submeshes[i].index_count; ns++; }
    if (!ni) return NULL;
    hta_bsp_mesh m;
    memset(&m, 0, sizeof(m));
    uint32_t *remap = malloc((size_t)world->vertex_count * sizeof(uint32_t));
    m.vertices = malloc((size_t)ni * sizeof(hta_vertex));
    m.indices = malloc((size_t)ni * sizeof(uint32_t));
    m.submeshes = calloc(ns, sizeof(hta_submesh));
    m.textures = calloc(ns, sizeof(hta_bsp_texture));
    hta_gfx_mesh *out = NULL;
    if (!remap || !m.vertices || !m.indices || !m.submeshes || !m.textures) goto done;
    memset(remap, 0xFF, (size_t)world->vertex_count * sizeof(uint32_t));
    for (int k = 0; k < 3; k++) { m.bounds_min[k] = 1e30f; m.bounds_max[k] = -1e30f; }
    for (uint32_t i = 0; i < world->submesh_count; i++) {
        if (owner[i] != e) continue;
        const hta_submesh *src = &world->submeshes[i];
        hta_submesh *dst = &m.submeshes[m.submesh_count];
        hta_submesh_init(dst);
        dst->first_index = m.index_count;
        dst->index_count = src->index_count;
        dst->draw_mode = src->draw_mode == HTA_DRAW_ALPHA ? HTA_DRAW_ALPHA : HTA_DRAW_OPAQUE;
        if (src->albedo_tex < world->texture_count) {
            uint32_t t = 0;
            while (t < m.texture_count && m.textures[t].rgba != world->textures[src->albedo_tex].rgba) t++;
            if (t == m.texture_count) m.textures[m.texture_count++] = world->textures[src->albedo_tex];
            dst->albedo_tex = t;
        }
        for (uint32_t k = 0; k < src->index_count; k++) {
            uint32_t v = world->indices[src->first_index + k];
            if (remap[v] == UINT32_MAX) {
                remap[v] = m.vertex_count;
                m.vertices[m.vertex_count] = world->vertices[v];
                for (int c = 0; c < 3; c++) {
                    float p = world->vertices[v].pos[c];
                    if (p < m.bounds_min[c]) m.bounds_min[c] = p;
                    if (p > m.bounds_max[c]) m.bounds_max[c] = p;
                }
                m.vertex_count++;
            }
            m.indices[m.index_count++] = remap[v];
        }
        m.submesh_count++;
    }
    char err[128];
    out = hta_gfx_mesh_upload(gfx, &m, err, sizeof(err));
done:
    free(remap); free(m.vertices); free(m.indices); free(m.submeshes);
    free(m.textures);   /* shallow copies: the pixels are the world's */
    return out;
}

uint32_t hta_went_gpu_upload(hta_went_gpu *g, hta_gfx *gfx, const hta_bsp_mesh *world,
                             hta_gfx_mesh *world_gpu, const uint16_t *submesh_entity,
                             const hta_world_entities *w, const hta_asset_table *assets)
{
    if (!g) return 0;
    memset(g, 0, sizeof(*g));
    if (!gfx || !world || !submesh_entity || !w || !w->loaded) return 0;
    /* X5: props' models, each once, however many props place it. */
    if (assets && assets->model_count && (g->model = calloc(assets->model_count, sizeof(*g->model)))) {
        g->model_count = assets->model_count;
        for (uint32_t e = 0; e < w->defs->count; e++) {
            const hta_wdef *d = &w->defs->entity[e];
            if (d->kind != HTA_WDEF_PROP || !d->model || d->model > g->model_count || g->model[d->model - 1]) continue;
            char err[128];
            g->model[d->model - 1] = hta_gfx_mesh_upload(gfx, &assets->model[d->model - 1].mesh, err, sizeof(err));
            if (g->model[d->model - 1]) g->models_uploaded++;
        }
    }
    for (uint32_t e = 0; e < w->defs->count; e++) {
        if (w->defs->entity[e].kind != HTA_WDEF_MOVER) continue;
        g->mesh[e] = upload_one(gfx, world, submesh_entity, (uint16_t)(e + 1));
        if (g->mesh[e]) g->count++;
    }
    /* The world mesh no longer draws what moves. */
    for (uint32_t i = 0; world_gpu && i < world->submesh_count; i++)
        if (submesh_entity[i] && submesh_entity[i] <= w->defs->count && g->mesh[submesh_entity[i] - 1])
            hta_gfx_mesh_set_draw_mode(world_gpu, i, HTA_DRAW_SKIP);
    return g->count;
}

uint32_t hta_went_gpu_instances(const hta_went_gpu *g, const hta_world_entities *w,
                                hta_gfx_instance *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t e = 0; g && g->count && w && w->loaded && out && e < w->defs->count && n < cap; e++) {
        if (!g->mesh[e]) continue;
        float off[3];
        hta_went_offset(w, e, off);
        hta_gfx_instance *in = &out[n++];
        memset(in, 0, sizeof(*in));
        in->mesh = g->mesh[e];
        in->model[0] = in->model[5] = in->model[10] = in->model[15] = 1.0f;
        in->model[12] = off[0]; in->model[13] = off[1]; in->model[14] = off[2];
        /* Shaded as the world it was cut from (no scene light on top). */
        in->lit = false;
    }
    for (uint32_t e = 0; g && g->model && w && w->loaded && out && e < w->defs->count && n < cap; e++) {
        const hta_wdef *d = &w->defs->entity[e];
        if (d->kind != HTA_WDEF_PROP || !d->model || d->model > g->model_count || !g->model[d->model - 1]) continue;
        hta_gfx_instance *in = &out[n++];
        memset(in, 0, sizeof(*in));
        in->mesh = g->model[d->model - 1];
        in->model[0] = in->model[5] = in->model[10] = in->model[15] = 1.0f;
        in->model[12] = d->pos[0]; in->model[13] = d->pos[1]; in->model[14] = d->pos[2];
        in->lit = true;          /* a model is lit by the scene, like a body */
    }
    return n;
}

void hta_went_gpu_free(hta_went_gpu *g, hta_gfx *gfx)
{
    if (!g) return;
    for (uint32_t e = 0; e < HTA_WDEF_MAX_ENTITIES; e++)
        if (g->mesh[e] && gfx) hta_gfx_mesh_free(gfx, g->mesh[e]);
    for (uint32_t m = 0; g->model && m < g->model_count; m++)
        if (g->model[m] && gfx) hta_gfx_mesh_free(gfx, g->model[m]);
    free(g->model);
    memset(g, 0, sizeof(*g));
}
