/* A match on an imported map: the pieces that move Blood Gulch's scenario
 * onto foreign ground. Synthetic geometry only -- a street where the starts
 * are, and a bigger rooftop nobody can reach, the trap de_dust2 set.
 */
#include "game/external_world.h"
#include "asset/external_map.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

/* Two floors: the street, x 0..10, y 0..4 at z 0; the roof, x 0..12,
 * y 7..17 at z 3. Nothing joins them. */
static void quad(hta_bsp_mesh *m, float x0, float y0, float x1, float y1, float z)
{
    uint32_t b = m->vertex_count;
    float c[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    for (int i = 0; i < 4; i++) {
        hta_vertex *v = &m->vertices[b + i];
        memset(v, 0, sizeof(*v));
        v->pos[0] = c[i][0]; v->pos[1] = c[i][1]; v->pos[2] = z;
        v->normal[2] = 1.0f;
    }
    uint32_t ix[6] = { b, b + 1, b + 2, b, b + 2, b + 3 };
    memcpy(m->indices + m->index_count, ix, sizeof(ix));
    m->vertex_count += 4;
    m->index_count += 6;
}

int main(void)
{
    printf("external world tests\n");
    static hta_vertex verts[8];
    static uint32_t idx[12];
    hta_bsp_mesh m;
    memset(&m, 0, sizeof(m));
    m.vertices = verts; m.indices = idx;
    quad(&m, 0, 0, 10, 4, 0);
    quad(&m, 0, 7, 12, 17, 3);
    m.bounds_min[0] = 0; m.bounds_min[1] = 0; m.bounds_min[2] = 0;
    m.bounds_max[0] = 12; m.bounds_max[1] = 17; m.bounds_max[2] = 3;
    hta_collision col;
    CHECK(hta_collision_build(&col, &m), "collision over the two floors");
    static hta_nav nav;
    char err[256];
    hta_nav_params prm = { 0.12f, 0.7f, 1.0f, 1.0f };
    CHECK(hta_nav_build(&nav, &col, m.bounds_min, m.bounds_max, &prm, err, sizeof(err)), err);
    float roof_probe[3] = { 6, 12, 3 };
    uint32_t r = hta_nav_nearest(&nav, roof_probe, 1.0f);
    CHECK(r != HTA_NAV_NONE && nav.nodes[r].region == nav.main_region,
          "the bigger rooftop is the largest region");

    hta_spawn_point sp[4];
    memset(sp, 0, sizeof(sp));
    float at[4][2] = { { 1, 1 }, { 1.5f, 2 }, { 9, 1 }, { 8.5f, 3 } };
    for (int i = 0; i < 4; i++) {
        sp[i].position[0] = at[i][0]; sp[i].position[1] = at[i][1];
        sp[i].team_index = i < 2 ? 0 : 1;
    }
    CHECK(hta_nav_main_from_spawns(&nav, sp, 4), "the starts are on the grid");
    uint32_t street = hta_nav_nearest(&nav, sp[0].position, 1.0f);
    CHECK(street != HTA_NAV_NONE && nav.nodes[street].region == nav.main_region,
          "the street the starts stand in becomes the map");

    float out[12][3];
    uint32_t got = hta_nav_spread(&nav, NULL, NULL, 0, out, 12);
    int on_street = 1;
    float closest = 1e9f;
    for (uint32_t i = 0; i < got; i++) {
        if (out[i][2] > 0.5f || out[i][1] > 4.0f) on_street = 0;
        for (uint32_t j = 0; j < i; j++) {
            float d = hypotf(out[i][0] - out[j][0], out[i][1] - out[j][1]);
            if (d < closest) closest = d;
        }
    }
    CHECK(got == 12, "twelve places found");
    CHECK(on_street, "every item lands on the reachable street");
    CHECK(closest > 1.0f, "items are spread, not piled up");
    float again[12][3];
    hta_nav_spread(&nav, NULL, NULL, 0, again, 12);
    CHECK(!memcmp(out, again, sizeof(out)), "the spread is the same every time (every phone agrees)");

    static hta_game g;
    memset(&g, 0, sizeof(g));
    hta_game_use_external(&g, sp, 4, &nav, NULL);
    CHECK(g.spawn_count == 4 && g.spawns[3].team_index == 1, "the map's starts replace the scenario's");
    CHECK(g.flags[0].present && g.flags[1].present, "each team gets a flag stand");
    CHECK(g.flags[0].home[0] < 4.0f && g.flags[1].home[0] > 6.0f,
          "each flag stands among its own team's starts");
    sp[0].team_index = sp[1].team_index = sp[2].team_index = sp[3].team_index = HTA_EXTERNAL_TEAM_ANY;
    hta_game_use_external(&g, sp, 4, &nav, NULL);
    CHECK(!g.flags[0].present && !g.flags[1].present, "no team starts, no CTF");

    {
        /* The imported grid's finest setting still covers the far edge. */
        hta_collision fine;
        float z = -1.0f;
        CHECK(hta_collision_build_cells(&fine, &m, HTA_COLLISION_CELLS_IMPORTED) &&
              fine.nx <= 256u && fine.ny <= 256u &&
              hta_collision_ground(&fine, 11.99f, 16.99f, 3.5f, &z) && fabsf(z - 3.0f) < 1e-4f,
              "a fine grid finds the floor in the far corner");
        hta_collision_free(&fine);
    }
    hta_collision_free(&col);
    hta_nav_free(&nav);

    /* A ledge you can drop off but not climb back onto shares the street's
     * region (regions are undirected). Nothing may be put on it. */
    {
        static hta_vertex v2[8];
        static uint32_t i2[12];
        hta_bsp_mesh m2;
        memset(&m2, 0, sizeof(m2));
        m2.vertices = v2; m2.indices = i2;
        quad(&m2, 0, 0, 10, 4, 0);
        quad(&m2, 10, 0, 16, 4, 0.6f);
        m2.bounds_max[0] = 16; m2.bounds_max[1] = 4; m2.bounds_max[2] = 0.6f;
        hta_collision c2;
        CHECK(hta_collision_build(&c2, &m2), "street and ledge collide");
        static hta_nav n2;
        CHECK(hta_nav_build(&n2, &c2, m2.bounds_min, m2.bounds_max, &prm, err, sizeof(err)), err);
        float ledge[3] = { 13, 2, 0.6f }, street[3] = { 3, 2, 0 };
        uint32_t lk = hta_nav_nearest(&n2, ledge, 1.0f), sk = hta_nav_nearest(&n2, street, 1.0f);
        CHECK(lk != HTA_NAV_NONE && sk != HTA_NAV_NONE && n2.nodes[lk].region == n2.nodes[sk].region,
              "the ledge shares the street's region");
        hta_spawn_point s2[1];
        memset(s2, 0, sizeof(s2));
        s2[0].position[0] = 2; s2[0].position[1] = 2;
        uint32_t np = 0;
        uint8_t *mask = hta_nav_playable(&n2, s2, 1, &np);
        CHECK(mask && !mask[lk] && mask[sk], "the ledge is not playable, the street is");
        float o2[10][3];
        uint32_t g2 = hta_nav_spread(&n2, mask, NULL, 0, o2, 10);
        int low = g2 == 10;
        for (uint32_t i = 0; i < g2; i++) if (o2[i][2] > 0.3f) low = 0;
        CHECK(low, "items spread over playable ground only");
        free(mask);
        hta_collision_free(&c2);
        hta_nav_free(&n2);
    }
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
