/* Player + collision tests against the grid fixture (real geometry). */
#include "engine/player.h"
#include "asset/cache.h"
#include "asset/bsp.h"
#include "fixture.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

int main(void)
{
    static bsp_fixture bf;
    hta_cache c;
    hta_bsp_mesh mesh;
    char err[HTA_ERRLEN];

    printf("player/collision tests\n\n[setup]\n");
    fixture_build_grid(&bf, 48, 48, 4);
    CHECK(hta_cache_open(&c, bf.f.buf, bf.f.size, err, sizeof(err)), "grid fixture opens");
    bool ok = hta_bsp_load_first(&c, &mesh, err, sizeof(err));
    if (!ok) printf("  (loader: %s)\n", err);
    CHECK(ok, "grid geometry extracted");
    if (!ok) return 1;

    hta_collision col;
    CHECK(hta_collision_build(&col, &mesh), "collision grid builds");
    CHECK(col.nx > 1 && col.ny > 1, "collision grid has sensible dimensions");

    printf("\n[ground queries]\n");
    /* the fixture surface is z = 6*sin(i*0.35)*cos(j*0.30) with 4 units/cell */
    float z = 0.0f;
    CHECK(hta_collision_ground(&col, 40.0f, 40.0f, 100.0f, &z), "finds ground under a point inside the grid");
    CHECK(z > -7.0f && z < 7.0f, "ground height is within the heightfield range");
    CHECK(!hta_collision_ground(&col, -500.0f, -500.0f, 100.0f, &z), "no ground far outside the grid");

    /* sample many points: all must land on the analytic surface */
    int sampled = 0, matched = 0;
    for (int i = 4; i < 40; i += 3) {
        for (int j = 4; j < 40; j += 3) {
            float x = (float)i * 4.0f, y = (float)j * 4.0f;
            float gz;
            if (hta_collision_ground(&col, x, y, 100.0f, &gz)) {
                float expect = 6.0f * sinf((float)i * 0.35f) * cosf((float)j * 0.30f);
                sampled++;
                if (fabsf(gz - expect) < 0.35f) matched++;
            }
        }
    }
    CHECK(sampled > 100, "sampled many interior points");
    CHECK(matched >= sampled * 9 / 10, "ground height matches the analytic surface at >=90% of samples");

    printf("\n[gravity and landing]\n");
    hta_player p;
    hta_camera cam;
    hta_player_init(&p);
    hta_camera_init(&cam);
    p.pos[0] = 40.0f; p.pos[1] = 40.0f; p.pos[2] = 50.0f;   /* high above terrain */

    hta_player_input in;
    memset(&in, 0, sizeof(in));
    for (int i = 0; i < 600; i++) hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
    CHECK(p.on_ground, "player falls and lands");
    CHECK(p.pos[2] > -8.0f && p.pos[2] < 8.0f, "lands on the terrain, not through it");
    CHECK(fabsf(p.velocity[2]) < 0.001f, "vertical velocity zeroed on landing");
    CHECK(fabsf(cam.pos[2] - (p.pos[2] + p.eye_height)) < 1e-4f, "camera sits at eye height above feet");

    printf("\n[movement]\n");
    float x0 = p.pos[0], y0 = p.pos[1];
    cam.yaw = 0.0f;      /* face +X */
    in.move_forward = 1.0f;
    for (int i = 0; i < 60; i++) hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
    CHECK(p.pos[0] > x0 + 0.3f, "moving forward advances along +X when facing +X");
    CHECK(fabsf(p.pos[1] - y0) < 0.3f, "no sideways drift while walking straight");

    in.move_forward = 0.0f; in.move_right = 1.0f;
    float y1 = p.pos[1];
    for (int i = 0; i < 60; i++) hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
    CHECK(fabsf(p.pos[1] - y1) > 0.3f, "strafing moves sideways");

    /* looking up must not change horizontal speed */
    memset(&in, 0, sizeof(in));
    in.move_forward = 1.0f;
    cam.pitch = 1.2f;
    float bx = p.pos[0], by = p.pos[1], bz = p.pos[2];
    for (int i = 0; i < 60; i++) hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
    float horiz = sqrtf((p.pos[0]-bx)*(p.pos[0]-bx) + (p.pos[1]-by)*(p.pos[1]-by));
    CHECK(horiz > 0.3f, "still moves at speed while looking up");
    CHECK(fabsf(p.pos[2] - bz) < 3.0f, "looking up does not launch the player");

    printf("\n[jump]\n");
    memset(&in, 0, sizeof(in));
    for (int i = 0; i < 120; i++) hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
    CHECK(p.on_ground, "settled on ground before jumping");
    float ground_z = p.pos[2];
    in.jump = true;
    hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
    in.jump = false;
    CHECK(!p.on_ground, "jump leaves the ground");
    float peak = p.pos[2];
    for (int i = 0; i < 200; i++) {
        hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
        if (p.pos[2] > peak) peak = p.pos[2];
        if (p.on_ground) break;
    }
    CHECK(peak > ground_z + 0.05f, "jump gains height");
    CHECK(p.on_ground, "player lands again after jumping");

    printf("\n[robustness]\n");
    memset(&in, 0, sizeof(in));
    hta_player_update(&p, &cam, &col, &in, 0.0f);
    CHECK(isfinite(p.pos[0]) && isfinite(p.pos[2]), "zero dt is a no-op, not a NaN");
    hta_player_update(&p, &cam, &col, &in, 1000.0f);
    CHECK(isfinite(p.pos[2]), "huge dt is clamped, not a teleport");
    hta_player_update(&p, &cam, NULL, &in, 1.0f/60.0f);
    CHECK(isfinite(p.pos[2]), "NULL collision does not crash");

    /* walking off the edge of the world should fall, not explode */
    p.pos[0] = 10000.0f; p.pos[1] = 10000.0f; p.pos[2] = 5.0f;
    for (int i = 0; i < 120; i++) hta_player_update(&p, &cam, &col, &in, 1.0f/60.0f);
    CHECK(!p.on_ground && isfinite(p.pos[2]), "outside the world the player falls without NaN");

    hta_collision_free(&col);
    CHECK(col.tri_index == NULL, "collision free clears state");
    hta_bsp_free(&mesh);

    printf("\n%s — %d checks, %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
