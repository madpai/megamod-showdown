#include "pickup.h"

#include "../asset/model.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

bool hta_pickups_load(hta_pickups *p, const hta_cache *c)
{
    if (!p || !c) return false;
    memset(p, 0, sizeof(*p));
    p->rng = 0x2545F491u;
    p->respawned = -1;
    p->count = hta_scenario_items(c, p->spawn, HTA_ITEM_MAX_PLACEMENTS);
    if (!p->count) return false;
    hta_pickups_reset(p);
    p->loaded = true;
    return true;
}

void hta_pickups_reset(hta_pickups *p)
{
    if (!p) return;
    p->respawned = -1;
    for (uint32_t i = 0; i < p->count; i++) {
        p->slot[i].present = true;
        p->slot[i].timer = 0.0f;
        p->slot[i].choice = hta_item_pick(&p->spawn[i], &p->rng);
    }
    p->dirty = true;
}

void hta_pickups_update(hta_pickups *p, float dt)
{
    if (!p || !p->loaded) return;
    p->respawned = -1;
    if (dt <= 0.0f) return;
    for (uint32_t i = 0; i < p->count; i++) {
        if (p->slot[i].present) continue;
        p->slot[i].timer -= dt;
        if (p->slot[i].timer > 0.0f) continue;
        p->slot[i].present = true;
        p->slot[i].timer = 0.0f;
        /* A fresh draw, not the same thing back. The pedestal in the middle
         * of Blood Gulch is overshield or camouflage fifty-fifty and rolls
         * again every time. */
        p->slot[i].choice = hta_item_pick(&p->spawn[i], &p->rng);
        p->respawned = (int32_t)i;
        p->dirty = true;
    }
}

static int32_t nearest(const hta_pickups *p, const float pos[3],
                       hta_item_kind kind, bool any_kind)
{
    if (!p || !p->loaded || !pos) return -1;
    int32_t best = -1;
    float best_d2 = HTA_PICKUP_REACH * HTA_PICKUP_REACH;
    for (uint32_t i = 0; i < p->count; i++) {
        if (!p->slot[i].present) continue;
        const hta_item_choice *ch = &p->spawn[i].choice[p->slot[i].choice];
        if (!any_kind && ch->kind != kind) continue;
        float dx = p->spawn[i].position[0] - pos[0];
        float dy = p->spawn[i].position[1] - pos[1];
        float dz = p->spawn[i].position[2] - pos[2];
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > best_d2) continue;
        best_d2 = d2;
        best = (int32_t)i;
    }
    return best;
}

int32_t hta_pickups_at(const hta_pickups *p, const float pos[3])
{
    return nearest(p, pos, HTA_ITEM_NONE, true);
}

int32_t hta_pickups_at_kind(const hta_pickups *p, const float pos[3],
                            hta_item_kind kind)
{
    return nearest(p, pos, kind, false);
}

const hta_item_choice *hta_pickups_item(const hta_pickups *p, int32_t slot)
{
    if (!p || !p->loaded || slot < 0 || (uint32_t)slot >= p->count) return NULL;
    if (!p->slot[slot].present) return NULL;
    const hta_item_spawn *s = &p->spawn[slot];
    uint32_t ci = p->slot[slot].choice;
    if (ci >= s->choice_count) return NULL;
    return &s->choice[ci];
}

void hta_pickups_take(hta_pickups *p, int32_t slot)
{
    if (!p || !p->loaded || slot < 0 || (uint32_t)slot >= p->count) return;
    if (!p->slot[slot].present) return;
    p->slot[slot].present = false;
    p->slot[slot].timer = p->spawn[slot].respawn;
    p->dirty = true;
}

/* How far above the placement an item sits. The scenario's z is the ground
 * it rests on, and a weapon drawn exactly on it sinks halfway in. Ours; the
 * tags carry no such offset. */
#define HTA_PICKUP_LIFT 0.06f

void hta_pickups_free(hta_pickups *p)
{
    if (!p) return;
    hta_bsp_free(&p->mesh);
    free(p->posed);
    p->posed = NULL;
    p->have_mesh = false;
}

bool hta_pickups_build(hta_pickups *p, const hta_cache *c,
                       const hta_resource_map *bitmaps,
                       char *err, size_t errlen)
{
    if (!p || !p->loaded || !c) {
        if (err) snprintf(err, errlen, "nothing to build");
        return false;
    }
    hta_pickups_free(p);
    p->mesh.textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
    if (!p->mesh.textures) {
        if (err) snprintf(err, errlen, "out of memory");
        return false;
    }

    uint32_t built = 0;
    for (uint32_t i = 0; i < p->count; i++) {
        p->first_vertex[i] = p->mesh.vertex_count;
        p->vertex_count[i] = 0;

        const hta_item_choice *ch = &p->spawn[i].choice[p->slot[i].choice];
        if (!ch->model_id) continue;

        /* Built at the origin and moved in hta_pickups_pose, so the spin
         * costs a rotation about the item's own centre rather than about
         * the corner of the map. */
        const float zero[3] = { 0.0f, 0.0f, 0.0f };
        const float rot[3]  = { 0.0f, 0.0f, 0.0f };
        char merr[HTA_ERRLEN];
        if (!hta_model_instance(&p->mesh, c, bitmaps, ch->model_id,
                                zero, rot, merr, sizeof(merr)))
            continue;
        p->vertex_count[i] = p->mesh.vertex_count - p->first_vertex[i];
        if (p->vertex_count[i]) built++;
    }
    if (!p->mesh.vertex_count) {
        if (err) snprintf(err, errlen, "no item models loaded");
        hta_pickups_free(p);
        return false;
    }
    p->posed = (hta_vertex *)malloc((size_t)p->mesh.vertex_count * sizeof(hta_vertex));
    if (!p->posed) {
        if (err) snprintf(err, errlen, "out of memory posing items");
        hta_pickups_free(p);
        return false;
    }
    p->have_mesh = true;
    p->dirty = true;
    hta_pickups_pose(p);
    if (err && errlen)
        snprintf(err, errlen, "%u of %u item(s), %u verts, %u texture(s)",
                 built, p->count, p->mesh.vertex_count, p->mesh.texture_count);
    return true;
}

bool hta_pickups_dirty(const hta_pickups *p)
{
    return p && p->have_mesh && p->dirty;
}

void hta_pickups_pose(hta_pickups *p)
{
    if (!p || !p->have_mesh) return;
    p->dirty = false;
    for (uint32_t i = 0; i < p->count; i++) {
        uint32_t first = p->first_vertex[i], n = p->vertex_count[i];
        if (!n) continue;
        if (!p->slot[i].present) {
            /* Collapsed to a point: no fill, no draw-call bookkeeping, and
             * the index buffer stays exactly as it was built. */
            memset(&p->posed[first], 0, (size_t)n * sizeof(hta_vertex));
            continue;
        }
        float a = p->spawn[i].facing;
        float ca = cosf(a), sa = sinf(a);
        const float *at = p->spawn[i].position;
        for (uint32_t v = 0; v < n; v++) {
            const hta_vertex *src = &p->mesh.vertices[first + v];
            hta_vertex *out = &p->posed[first + v];
            *out = *src;
            out->pos[0] = at[0] + src->pos[0] * ca - src->pos[1] * sa;
            out->pos[1] = at[1] + src->pos[0] * sa + src->pos[1] * ca;
            out->pos[2] = at[2] + src->pos[2] + HTA_PICKUP_LIFT;
            out->normal[0] = src->normal[0] * ca - src->normal[1] * sa;
            out->normal[1] = src->normal[0] * sa + src->normal[1] * ca;
        }
    }
}
