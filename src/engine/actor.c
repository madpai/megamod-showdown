#include "actor.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Object: model at +40, animation graph at +56, both TagDependency. */
#define OBJ_MODEL        40u
#define OBJ_ANIM_GRAPH   56u

/* Halo names the cyborg's deaths by how hard the blow was and where it came
 * from. There is no "die" clip: these ARE the deaths. */
static const char *const DEATH_CLIPS[] = {
    "s-kill front gut", "s-kill back gut", "s-kill left gut",
    "s-kill right gut", "s-kill front chest",
    "h-kill front gut", "h-kill back gut", "h-kill front head"
};

void hta_actor_free(hta_actor *a)
{
    if (!a) return;
    hta_anim_free(&a->graph);
    hta_bsp_free(&a->mesh);
    free(a->posed);
    free(a->skin);
    memset(a, 0, sizeof(*a));
    a->clip = -1;
}

bool hta_actor_load(hta_actor *a, const hta_cache *c,
                    const hta_resource_map *bitmaps, uint32_t bipd_tag_id,
                    char *err, size_t errlen)
{
    if (!a || !c || !bipd_tag_id) {
        if (err) snprintf(err, errlen, "bad arguments");
        return false;
    }
    memset(a, 0, sizeof(*a));
    a->clip = -1;

    int32_t ti = hta_cache_find_tag_by_id(c, bipd_tag_id);
    if (ti < 0) { if (err) snprintf(err, errlen, "no such biped"); return false; }
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) {
        if (err) snprintf(err, errlen, "biped is indexed");
        return false;
    }
    uint32_t base = 0;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) {
        if (err) snprintf(err, errlen, "biped data out of range");
        return false;
    }
    uint32_t model = 0, antr = 0;
    hta_rd_u32(c, base + OBJ_MODEL + 12u, &model);
    hta_rd_u32(c, base + OBJ_ANIM_GRAPH + 12u, &antr);
    if (!model || model == 0xFFFFFFFFu || !antr || antr == 0xFFFFFFFFu) {
        if (err) snprintf(err, errlen, "biped has no model or no graph");
        return false;
    }

    if (!hta_anim_load(&a->graph, c, antr, err, errlen)) return false;
    /* The texture table has to exist before anything interns into it --
     * hta_model_append_skinned fills it, it does not create it. Without
     * this the cyborg loads with four submeshes and no art at all. */
    a->mesh.textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
    if (!a->mesh.textures) {
        hta_anim_free(&a->graph);
        if (err) snprintf(err, errlen, "out of memory");
        return false;
    }
    if (!hta_model_append_skinned(&a->mesh, &a->skin, c, bitmaps, model,
                                  &a->graph, a->rest_inv, a->have_rest,
                                  err, errlen)) {
        hta_anim_free(&a->graph);
        return false;
    }
    a->posed = (hta_vertex *)malloc((size_t)a->mesh.vertex_count * sizeof(hta_vertex));
    if (!a->posed) {
        if (err) snprintf(err, errlen, "out of memory posing the actor");
        hta_actor_free(a);
        return false;
    }
    memcpy(a->posed, a->mesh.vertices,
           (size_t)a->mesh.vertex_count * sizeof(hta_vertex));

    a->loaded = true;
    if (err && errlen)
        snprintf(err, errlen, "%u verts, %u node(s), %u animation(s)",
                 a->mesh.vertex_count, a->graph.node_count, a->graph.anim_count);
    return true;
}

bool hta_actor_play(hta_actor *a, const char *name, bool hold)
{
    if (!a || !a->loaded || !name) return false;
    int32_t ci = hta_anim_find(&a->graph, name);
    if (ci < 0) return false;
    a->clip = ci;
    a->frame = 0.0f;
    a->hold_last = hold;
    a->finished = false;
    return true;
}

bool hta_actor_play_death(hta_actor *a, uint32_t *rng)
{
    if (!a || !a->loaded) return false;
    uint32_t seed = rng ? *rng : 1u;
    seed = seed * 1103515245u + 12345u;
    if (rng) *rng = seed;
    const uint32_t n = (uint32_t)(sizeof(DEATH_CLIPS) / sizeof(DEATH_CLIPS[0]));
    uint32_t start = (seed >> 16) % n;
    /* Any of them will do, so take the first that this graph actually has
     * rather than failing on a name a different character does not use. */
    for (uint32_t i = 0; i < n; i++)
        if (hta_actor_play(a, DEATH_CLIPS[(start + i) % n], true)) return true;
    return false;
}

void hta_actor_update(hta_actor *a, float dt)
{
    if (!a || !a->loaded || a->clip < 0 || dt <= 0.0f) return;
    const hta_animation *an = &a->graph.anims[a->clip];
    float last = (float)(an->frame_count > 1 ? an->frame_count - 1 : 0);

    a->frame += dt * HTA_ANIM_FPS;
    if (a->hold_last) {
        /* A death holds its final frame. Halo's kill clips are four to six
         * frames -- a fifth of a second -- so without this the body would
         * snap back upright before you had seen it fall. */
        if (a->frame >= last) { a->frame = last; a->finished = true; }
    } else if (last > 0.0f) {
        a->frame = fmodf(a->frame, last);
    } else {
        a->frame = 0.0f;
    }
}

void hta_actor_place(hta_actor *a, const float pos[3], float yaw)
{
    if (!a || !a->loaded || !a->posed) return;
    if (pos) for (int k = 0; k < 3; k++) a->pos[k] = pos[k];
    a->yaw = yaw;

    hta_transform local[HTA_ANIM_MAX_NODES];
    hta_transform world[HTA_ANIM_MAX_NODES];
    if (a->clip < 0 || !hta_anim_sample(&a->graph, (uint32_t)a->clip,
                                        a->frame, local)) {
        for (uint32_t i = 0; i < a->graph.node_count; i++)
            hta_xf_identity(&local[i]);
    }
    hta_anim_world(&a->graph, local, world);

    /* Where the body stands: a turn about +Z, then a move. Applied to the
     * skinned result rather than to every node, which is the same thing and
     * one rotation instead of nineteen. */
    const float h = a->yaw * 0.5f;
    hta_transform root;
    root.q[0] = 0.0f; root.q[1] = 0.0f;
    root.q[2] = sinf(h); root.q[3] = cosf(h);
    root.t[0] = a->pos[0]; root.t[1] = a->pos[1]; root.t[2] = a->pos[2];
    root.s = 1.0f;

    hta_transform d[HTA_ANIM_MAX_NODES];
    for (uint32_t i = 0; i < a->graph.node_count; i++) {
        if (a->have_rest[i]) {
            hta_transform bone;
            hta_xf_mul(&bone, &world[i], &a->rest_inv[i]);
            hta_xf_mul(&d[i], &root, &bone);
        } else {
            d[i] = root;
        }
    }

    for (uint32_t v = 0; v < a->mesh.vertex_count; v++) {
        const hta_vertex      *src = &a->mesh.vertices[v];
        const hta_skin_vertex *sw  = &a->skin[v];
        hta_vertex *out = &a->posed[v];
        *out = *src;
        float p[3] = {0,0,0}, nn[3] = {0,0,0};
        float wsum = 0.0f;
        for (int k = 0; k < 2; k++) {
            uint16_t gn = sw->node[k];
            float w = sw->weight[k];
            if (gn == HTA_SKIN_NONE || w <= 0.0f || gn >= a->graph.node_count)
                continue;
            float tp[3], tn[3];
            hta_xf_point(tp, &d[gn], src->pos);
            hta_xf_vector(tn, &d[gn], src->normal);
            for (int j = 0; j < 3; j++) { p[j] += tp[j]*w; nn[j] += tn[j]*w; }
            wsum += w;
        }
        if (wsum <= 0.0f) {
            /* An unbound vertex still belongs to the body, not to the
             * origin of the map. */
            hta_xf_point(p, &root, src->pos);
            hta_xf_vector(nn, &root, src->normal);
        }
        out->pos[0]=p[0]; out->pos[1]=p[1]; out->pos[2]=p[2];
        out->normal[0]=nn[0]; out->normal[1]=nn[1]; out->normal[2]=nn[2];
    }
}
