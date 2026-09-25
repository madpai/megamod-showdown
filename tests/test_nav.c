/* Where a bot can walk. Needs the Trial's own map for everything past the
 * argument checks: the walkable surface is rebuilt from Blood Gulch's
 * collision, and the proof is a biped on the player's own physics actually
 * walking a planned path from one base to the other.
 */
#include "game/nav.h"
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/biped.h"
#include "asset/model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    printf("nav tests\n");
    static hta_nav nav;
    char err[HTA_ERRLEN] = {0};
    hta_nav_params prm = { 0.175f, 0.7f, 0.7f, 1.0f };
    CHECK(!hta_nav_build(&nav, NULL, NULL, NULL, &prm, err, sizeof(err)),
          "no collision builds nothing");
    hta_nav_free(&nav);
    CHECK(hta_nav_nearest(&nav, (float[3]){0,0,0}, 1.0f) == HTA_NAV_NONE,
          "an empty grid has no nearest node");

    /* No game data: a flat floor with a crate wall across the middle.
     * Paths and straight lines go round it while it stands and through
     * where it stood once it breaks. */
    {
        static hta_vertex V[6];
        static uint32_t I[6];
        const float q[6][3] = { {-5,-5,0}, {5,-5,0}, {5,5,0}, {-5,-5,0}, {5,5,0}, {-5,5,0} };
        for (int i = 0; i < 6; i++) { memset(&V[i], 0, sizeof(V[i])); memcpy(V[i].pos, q[i], 12); I[i] = (uint32_t)i; }
        hta_bsp_mesh m;
        memset(&m, 0, sizeof(m));
        m.vertices = V; m.vertex_count = 6; m.indices = I; m.index_count = 6;
        m.bounds_min[0] = m.bounds_min[1] = -5; m.bounds_max[0] = m.bounds_max[1] = 5; m.bounds_max[2] = 1;
        hta_collision col;
        CHECK(hta_collision_build(&col, &m), "floor collision");
        static hta_nav fl;
        hta_nav_params fp = { 0.175f, 0.7f, 0.7f, 1.0f, HTA_NAV_CELL };
        CHECK(hta_nav_build(&fl, &col, m.bounds_min, m.bounds_max, &fp, err, sizeof(err)), "floor grid builds");
        uint32_t a = hta_nav_nearest(&fl, (float[3]){-3,0,0}, 1.0f), b = hta_nav_nearest(&fl, (float[3]){3,0,0}, 1.0f);
        CHECK(a != HTA_NAV_NONE && b != HTA_NAV_NONE && hta_nav_straight(&fl, a, b), "open floor: straight across");
        hta_props P;
        hta_props_init(&P, 4);
        uint32_t crate = hta_props_add(&P, (float[3]){0,0,0.5f}, (float[3]){0.3f,1.5f,0.5f}, 0, HTA_RMAT_WOOD, 0, 1);
        CHECK(hta_nav_sync_props(&fl, &P, fp.radius) && fl.blocked_nodes > 10, "a whole crate blocks nodes");
        CHECK(!hta_nav_sync_props(&fl, &P, fp.radius), "an unchanged crate needs no resync");
        CHECK(!hta_nav_straight(&fl, a, b), "no straight line through the crate");
        uint32_t path[256];
        uint32_t len = hta_nav_path(&fl, a, b, path, 256, 100000);
        bool around = len > 2;
        for (uint32_t i = 0; i < len; i++) {
            float p[3];
            hta_nav_pos(&fl, path[i], p);
            if (fabsf(p[0]) < 0.3f && fabsf(p[1]) < 1.5f) around = false;
            if (hta_nav_is_blocked(&fl, path[i])) around = false;
        }
        CHECK(around, "the path goes round the crate");
        /* A goal inside the crate's padding is still reachable. */
        uint32_t near = hta_nav_nearest(&fl, (float[3]){-0.4f,0,0}, 0.5f);
        CHECK(near != HTA_NAV_NONE && hta_nav_is_blocked(&fl, near) &&
              hta_nav_path(&fl, a, near, path, 256, 100000) > 1, "a blocked goal is reached");
        /* A field steers round it too. */
        uint32_t *next = malloc(fl.node_count * sizeof(uint32_t));
        hta_nav_field(&fl, b, next);
        len = hta_nav_field_path(&fl, next, a, path, 256);
        around = len > 2 && path[len - 1] == b;
        for (uint32_t i = 0; i < len; i++) if (hta_nav_is_blocked(&fl, path[i])) around = false;
        CHECK(around, "a field goes round the crate");
        free(next);
        /* Broken: the way is open again. */
        P.props[crate].health = 0.0f;
        hta_props_damage(&P, crate, 1000, P.props[crate].centre, (float[3]){-3,0,0.5f}, NULL, NULL);
        CHECK(P.props[crate].broken && hta_nav_sync_props(&fl, &P, fp.radius) && fl.blocked_nodes == 0 &&
              hta_nav_straight(&fl, a, b), "a broken crate unblocks");
        hta_props_free(&P);
        hta_nav_free(&fl);
        hta_collision_free(&col);
    }

    if (argc < 2) {
        printf("\n  skip: pass bloodgulch.map\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }
    hta_cache c;
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) { printf("  FAIL: %s\n", err); return 1; }

    hta_bsp_mesh mesh = {0}, cm = {0};
    hta_collision col = {0};
    bool ok = hta_bsp_load_first(&c, &mesh, err, sizeof(err)) &&
              hta_bsp_load_collision(&c, &cm, err, sizeof(err)) &&
              hta_scenario_add_collision(&cm, &c, err, sizeof(err)) &&
              hta_collision_build(&col, &cm);
    CHECK(ok, "Blood Gulch's collision loads");
    if (!ok) return 1;
    hta_player_physics phys;
    hta_player_physics_load(&phys, &c, err, sizeof(err));
    hta_collision_set_slope(&col, phys.max_slope);

    printf("\n[the walkable surface]\n");
    prm.radius = phys.radius;
    prm.height = phys.coll_stand;
    prm.max_slope = phys.max_slope;
    double t0 = now_s();
    ok = hta_nav_build(&nav, &col, mesh.bounds_min, mesh.bounds_max, &prm, err, sizeof(err));
    double built = now_s() - t0;
    printf("  %s in %.0f ms\n", err, built * 1000.0);
    CHECK(ok, "it builds");
    if (!ok) return 1;
    CHECK(nav.node_count > 20000u, "tens of thousands of places to stand");
    CHECK(nav.link_count > nav.node_count * 3u, "and most of them lead somewhere");

    hta_spawn_point sp[64];
    uint32_t nsp = hta_scenario_spawns(&c, sp, 64);
    uint32_t on_main = 0;
    for (uint32_t i = 0; i < nsp; i++) {
        float f[3] = { sp[i].position[0], sp[i].position[1], sp[i].position[2] };
        float gz;
        if (hta_collision_ground(&col, f[0], f[1], f[2] + 1.0f, &gz)) f[2] = gz;
        uint32_t nd = hta_nav_nearest(&nav, f, 1.0f);
        if (nd != HTA_NAV_NONE && nav.nodes[nd].region == nav.main_region) on_main++;
    }
    printf("  %u of %u spawn points on the main walkable region\n", on_main, nsp);
    CHECK(nsp && on_main * 10u >= nsp * 9u, "nearly every spawn point is on the map proper");

    /* The two spawns furthest apart: one base to the other. */
    uint32_t fa = 0, fb = 0;
    float far = 0.0f;
    for (uint32_t i = 0; i < nsp; i++)
        for (uint32_t j = i + 1u; j < nsp; j++) {
            float d = hypotf(sp[i].position[0] - sp[j].position[0],
                             sp[i].position[1] - sp[j].position[1]);
            if (d > far) { far = d; fa = i; fb = j; }
        }
    printf("\n[base to base, %.0f wu apart]\n", far);
    float a[3] = { sp[fa].position[0], sp[fa].position[1], sp[fa].position[2] };
    float b[3] = { sp[fb].position[0], sp[fb].position[1], sp[fb].position[2] };
    float gz;
    if (hta_collision_ground(&col, a[0], a[1], a[2] + 1.0f, &gz)) a[2] = gz;
    if (hta_collision_ground(&col, b[0], b[1], b[2] + 1.0f, &gz)) b[2] = gz;
    uint32_t na = hta_nav_nearest(&nav, a, 1.0f), nb = hta_nav_nearest(&nav, b, 1.0f);
    CHECK(na != HTA_NAV_NONE && nb != HTA_NAV_NONE, "both ends are on the grid");
    static uint32_t path[4096];
    t0 = now_s();
    uint32_t len = hta_nav_path(&nav, na, nb, path, 4096, 200000);
    printf("  %u grid steps in %.1f ms\n", len, (now_s() - t0) * 1000.0);
    CHECK(len > 20u, "a path exists");
    uint32_t smooth = hta_nav_smooth(&nav, path, len);
    printf("  %u corners after pulling it taut\n", smooth);
    CHECK(smooth > 1u && smooth < len / 2u, "and most of it is straight lines");

    /* Walk it, on the player's own physics. */
    hta_player pl;
    hta_player_init(&pl);
    hta_player_apply_physics(&pl, &phys);
    for (int k = 0; k < 3; k++) pl.pos[k] = a[k];
    pl.on_ground = true;
    hta_camera cam;
    hta_camera_init(&cam);
    uint32_t next = 1;
    float t = 0.0f, dt = 1.0f / 60.0f;
    float closest = 1e9f;
    uint32_t stuck_frames = 0;
    float last[3] = { pl.pos[0], pl.pos[1], pl.pos[2] };
    while (t < 240.0f && next < smooth) {
        float w[3];
        hta_nav_pos(&nav, path[next], w);
        float dx = w[0] - pl.pos[0], dy = w[1] - pl.pos[1];
        float d = hypotf(dx, dy);
        if (d < 0.3f) { next++; continue; }
        float want = atan2f(dy, dx);
        float turn = want - cam.yaw;
        while (turn > 3.14159265f) turn -= 6.2831853f;
        while (turn < -3.14159265f) turn += 6.2831853f;
        hta_player_input in = {0};
        in.look_yaw = turn;
        in.move_forward = 1.0f;
        if (++stuck_frames > 45) {
            float moved = hypotf(pl.pos[0] - last[0], pl.pos[1] - last[1]);
            if (moved < 0.1f) in.jump = true;
            stuck_frames = 0;
            for (int k = 0; k < 3; k++) last[k] = pl.pos[k];
        }
        hta_player_update(&pl, &cam, &col, &in, dt);
        t += dt;
        float gd = hypotf(b[0] - pl.pos[0], b[1] - pl.pos[1]);
        if (gd < closest) closest = gd;
    }
    float end = hypotf(b[0] - pl.pos[0], b[1] - pl.pos[1]);
    printf("  walked for %.1f s, finished %.2f wu from the far spawn (closest %.2f)\n",
           t, end, closest);
    CHECK(end < 1.0f, "a biped on the Trial's physics walks it end to end");
    CHECK(t < 120.0f, "at running pace, not by wandering");

    printf("\n[for cars]\n");
    {
        /* Clearance: none on a node hugging a wall, and never more than
         * one past the least of its neighbours. */
        uint32_t wide = 0, bad = 0;
        for (uint32_t i = 0; i < nav.node_count; i++) {
            const hta_nav_node *nd = &nav.nodes[i];
            if ((nd->flags & HTA_NAV_NEAR_WALL) && nd->clear) bad++;
            if (nd->clear >= 2u) wide++;
            for (int d = 0; d < 8 && nd->clear; d++) {
                uint32_t j = nd->link[d];
                if (j == HTA_NAV_NONE || nav.nodes[j].clear + 1u < nd->clear) { bad++; break; }
            }
        }
        printf("  %u of %u nodes have room for a Warthog\n", wide, nav.node_count);
        CHECK(bad == 0, "clearance falls off one cell at a time toward every wall and edge");
        CHECK(wide > nav.node_count / 2u, "most of Blood Gulch is open ground");
        uint8_t hog = hta_nav_car_clear(1.12f), tank = hta_nav_car_clear(2.05f);
        printf("  a Warthog wants %u cells of room, a Scorpion %u\n", hog, tank);
        CHECK(hog >= 1u && tank > hog && tank <= HTA_NAV_CLEAR_MAX, "a bigger car wants more room");

        /* Base to base, inside the budget a bot plans with. */
        static uint32_t wp[4096];
        for (int k = 0; k < 2; k++) {
            uint8_t cl = k ? tank : hog;
            uint32_t ca = hta_nav_nearest_wide(&nav, a, 15.0f, cl);
            uint32_t cb = hta_nav_nearest_wide(&nav, b, 15.0f, cl);
            t0 = now_s();
            uint32_t wl = ca != HTA_NAV_NONE && cb != HTA_NAV_NONE
                        ? hta_nav_path_wide(&nav, ca, cb, wp, 4096, 60000u, cl) : 0;
            double ms = (now_s() - t0) * 1000.0;
            /* Narrow ground only on the way out of a base and in to the
             * other: the path's first and last 10 wu. */
            uint32_t narrow = 0, stray = 0;
            float pa[3], pb[3], pq[3];
            if (wl) { hta_nav_pos(&nav, wp[0], pa); hta_nav_pos(&nav, wp[wl - 1u], pb); }
            for (uint32_t q = 0; q < wl; q++) {
                if (nav.nodes[wp[q]].clear >= cl) continue;
                narrow++;
                hta_nav_pos(&nav, wp[q], pq);
                if (hypotf(pq[0] - pa[0], pq[1] - pa[1]) > 12.0f &&
                    hypotf(pq[0] - pb[0], pq[1] - pb[1]) > 12.0f) stray++;
            }
            printf("  %s: %u grid steps in %.1f ms, %u too narrow near its ends, %u elsewhere\n",
                   k ? "Scorpion" : "Warthog", wl, ms, narrow - stray, stray);
            CHECK(wl > 20u, k ? "a Scorpion's path from one base to the other, inside the bots' budget"
                              : "a Warthog's path from one base to the other, inside the bots' budget");
            CHECK(wl && stray == 0, "on open ground between the bases");
            /* Every leg of the taut path is open ground for it, or one
             * grid step the search itself took. */
            uint32_t ws = hta_nav_smooth_wide(&nav, wp, wl, cl);
            bool straight = true;
            for (uint32_t q = 1; q < ws; q++) {
                const hta_nav_node *p0 = &nav.nodes[wp[q - 1u]], *p1 = &nav.nodes[wp[q]];
                bool step = abs((int)p0->cx - (int)p1->cx) <= 1 && abs((int)p0->cy - (int)p1->cy) <= 1;
                if (!step && !hta_nav_straight_wide(&nav, wp[q - 1u], wp[q], cl)) straight = false;
            }
            printf("  %u legs pulled taut\n", ws - 1u);
            CHECK(ws > 1u && ws < wl / 4u && straight, "pulled taut without cutting across a corner too narrow for it");
        }
        uint32_t rng2 = 11;
        uint32_t rr = hta_nav_random_wide(&nav, &rng2, tank);
        CHECK(rr != HTA_NAV_NONE && nav.nodes[rr].clear >= tank, "somewhere to roam has room for a tank");
    }

    printf("\n[kept on disk]\n");
    {
        const char *tmp = "test_nav_cache.bin";
        static hta_nav back;
        CHECK(hta_nav_save(&nav, tmp, 1234u), "a built grid saves");
        t0 = now_s();
        bool loaded = hta_nav_load(&back, tmp, 1234u);
        printf("  read back in %.1f ms\n", (now_s() - t0) * 1000.0);
        CHECK(loaded && back.node_count == nav.node_count && back.main_region == nav.main_region,
              "and reads back the same");
        bool same_room = loaded;
        for (uint32_t i = 0; same_room && i < nav.node_count; i++)
            same_room = back.nodes[i].clear == nav.nodes[i].clear;
        CHECK(same_room, "room for cars included");
        static uint32_t p2[4096];
        CHECK(loaded && hta_nav_path(&back, na, nb, p2, 4096, 200000) == len,
              "and plans the same path");
        hta_nav_free(&back);
        CHECK(!hta_nav_load(&back, tmp, 99u), "a file for another map or physics is refused");
        FILE *f = fopen(tmp, "r+b");
        if (f) { fseek(f, 0, SEEK_END); long sz = ftell(f); fclose(f); if (truncate(tmp, sz / 2) != 0) {} }
        CHECK(!hta_nav_load(&back, tmp, 1234u), "and so is a truncated one");
        remove(tmp);
    }

    printf("\n[odds and ends]\n");
    uint32_t rng = 7;
    uint32_t r = hta_nav_random(&nav, &rng);
    CHECK(r != HTA_NAV_NONE && nav.nodes[r].region == nav.main_region,
          "a random destination is somewhere real");
    CHECK(hta_nav_path(&nav, na, na, path, 8, 10) == 1u, "a path to where you are is one step");

    hta_nav_free(&nav);
    hta_collision_free(&col);
    hta_bsp_free(&mesh);
    hta_bsp_free(&cm);
    free(data);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
