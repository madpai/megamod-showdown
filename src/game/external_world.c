#include "external_world.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool usable(const hta_nav *n, uint32_t i)
{
    return n->nodes[i].region == n->main_region && !(n->nodes[i].flags & HTA_NAV_NEAR_WALL);
}

uint32_t hta_nav_spread(const hta_nav *n, const float (*seed)[3], uint32_t seeds,
                        float (*out)[3], uint32_t count)
{
    if (!n || !n->built || !n->node_count || !out || !count) return 0;
    float *d = (float *)malloc((size_t)n->node_count * sizeof(float));
    if (!d) return 0;
    for (uint32_t i = 0; i < n->node_count; i++) d[i] = 1e30f;
    float p[3];
    for (uint32_t s = 0; s < seeds; s++)
        for (uint32_t i = 0; i < n->node_count; i++) {
            hta_nav_pos(n, i, p);
            float dx = p[0] - seed[s][0], dy = p[1] - seed[s][1], dz = p[2] - seed[s][2];
            float dd = dx * dx + dy * dy + dz * dz;
            if (dd < d[i]) d[i] = dd;
        }
    uint32_t found = 0;
    for (; found < count; found++) {
        uint32_t best = HTA_NAV_NONE;
        for (uint32_t i = 0; i < n->node_count; i++)
            if (usable(n, i) && (best == HTA_NAV_NONE || d[i] > d[best])) best = i;
        if (best == HTA_NAV_NONE || d[best] <= 0.0f) break;
        hta_nav_pos(n, best, out[found]);
        for (uint32_t i = 0; i < n->node_count; i++) {
            hta_nav_pos(n, i, p);
            float dx = p[0] - out[found][0], dy = p[1] - out[found][1], dz = p[2] - out[found][2];
            float dd = dx * dx + dy * dy + dz * dz;
            if (dd < d[i]) d[i] = dd;
        }
    }
    free(d);
    return found;
}

bool hta_nav_main_from_spawns(hta_nav *n, const hta_spawn_point *spawns, uint32_t count)
{
    if (!n || !n->built) return false;
    uint32_t votes[256] = { 0 };
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = hta_nav_nearest(n, spawns[i].position, 1.5f);
        if (k != HTA_NAV_NONE && n->nodes[k].region) votes[n->nodes[k].region]++;
    }
    uint32_t best = 0;
    for (uint32_t r = 1; r < 256; r++) if (votes[r] > votes[best]) best = r;
    if (!votes[best]) return false;
    n->main_region = (uint8_t)best;
    return true;
}

void hta_pickups_relocate(hta_pickups *p, const hta_nav *n)
{
    if (!p || !p->count) return;
    float (*at)[3] = (float (*)[3])calloc(p->count, sizeof(*at));
    if (!at) return;
    uint32_t got = hta_nav_spread(n, NULL, 0, at, p->count);
    /* What does not fit is taken off the map rather than left floating
     * where Blood Gulch had it. */
    for (uint32_t i = 0; i < p->count; i++) {
        if (i < got) memcpy(p->spawn[i].position, at[i], sizeof(at[i]));
        else p->spawn[i].choice_count = 0;
    }
    if (got < p->count) p->count = got;
    free(at);
}

static bool nearest(const hta_nav *n, const float want[3], float out[3])
{
    float best = 1e30f, q[3];
    bool any = false;
    for (uint32_t i = 0; n && n->built && i < n->node_count; i++) {
        if (!usable(n, i)) continue;
        hta_nav_pos(n, i, q);
        float dx = q[0] - want[0], dy = q[1] - want[1], dz = q[2] - want[2];
        float dd = dx * dx + dy * dy + dz * dz;
        if (dd < best) { best = dd; memcpy(out, q, sizeof(q)); any = true; }
    }
    return any;
}

void hta_game_use_external(hta_game *g, const hta_spawn_point *spawns, uint32_t count,
                           const hta_nav *n)
{
    if (!g) return;
    uint32_t cap = (uint32_t)(sizeof(g->spawns) / sizeof(g->spawns[0]));
    g->spawn_count = count < cap ? count : cap;
    if (g->spawn_count) memcpy(g->spawns, spawns, g->spawn_count * sizeof(*spawns));
    for (int t = 0; t < 2; t++) {
        hta_game_flag *f = &g->flags[t];
        f->present = false;
        float mid[3] = { 0, 0, 0 };
        uint32_t k = 0;
        for (uint32_t i = 0; i < g->spawn_count; i++) {
            if (g->spawns[i].team_index != (uint16_t)t) continue;
            for (int a = 0; a < 3; a++) mid[a] += g->spawns[i].position[a];
            k++;
        }
        if (!k) continue;
        for (int a = 0; a < 3; a++) mid[a] /= (float)k;
        if (!nearest(n, mid, f->home)) continue;
        f->home_yaw = 0.0f;
        f->present = true;
    }
}
