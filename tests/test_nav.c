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
