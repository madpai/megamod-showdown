/* Parses Trial globals + cyborg_mp. Requires a real .map path as argv[1]. */
#include "asset/cache.h"
#include "asset/biped.h"
#include "asset/bsp.h"
#include "asset/weapon.h"
#include "engine/player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

int main(int argc, char **argv)
{
    printf("biped/globals player physics\n\n");
    if (argc < 2) {
        printf("  skip: no map path (pass bloodgulch.map)\n");
        return 0;
    }
    size_t sz = 0;
    uint8_t *data = slurp(argv[1], &sz);
    CHECK(data != NULL, "map reads");
    if (!data) return 1;

    hta_cache c;
    char err[HTA_ERRLEN];
    CHECK(hta_cache_open(&c, data, sz, err, sizeof(err)), "cache opens");

    hta_player_physics phys;
    CHECK(hta_player_physics_load(&phys, &c, err, sizeof(err)), "physics load");
    printf("  run_fwd=%.3f jump=%.3f cam=%.3f r=%.3f fov=%.3f slope=%.3f accel=%.3f air=%.3f\n",
           phys.run_forward, phys.jump_speed, phys.cam_stand, phys.radius,
           phys.fov_y, phys.max_slope, phys.run_accel, phys.air_accel);

    CHECK(fabsf(phys.run_forward - 2.25f) < 0.05f, "run forward is Trial 2.25 wu/s");
    CHECK(fabsf(phys.run_back - 2.0f) < 0.05f, "run back is 2.0");
    CHECK(phys.jump_speed > 1.8f && phys.jump_speed < 2.4f, "jump is 0.07 wu/tick * 30");
    CHECK(fabsf(phys.cam_stand - 0.62f) < 0.02f, "standing camera 0.62");
    CHECK(fabsf(phys.radius - 0.20f) < 0.02f, "collision radius 0.20");
    CHECK(fabsf(phys.fov_y - 1.22173f) < 0.01f, "FOV 70 degrees");
    CHECK(phys.run_accel > 5.0f, "run accel treated as per-tick (snappy)");

    hta_player p;
    hta_camera cam;
    hta_player_init(&p);
    hta_player_apply_physics(&p, &phys);
    hta_camera_init(&cam);
    p.pos[0] = p.pos[1] = 0;
    p.pos[2] = 0;
    p.on_ground = true;
    hta_player_input in;
    memset(&in, 0, sizeof(in));
    in.move_forward = 1.0f;
    cam.yaw = 0.0f;
    float v0 = 0.0f;
    hta_player_update(&p, &cam, NULL, &in, 1.0f / 60.0f);
    v0 = sqrtf(p.velocity[0]*p.velocity[0] + p.velocity[1]*p.velocity[1]);
    CHECK(v0 > 0.05f && v0 < phys.run_forward * 0.5f, "first frame is accelerating, not max speed");
    p.on_ground = true;
    for (int i = 0; i < 90; i++) {
        p.on_ground = true;
        hta_player_update(&p, &cam, NULL, &in, 1.0f / 60.0f);
    }
    float v1 = sqrtf(p.velocity[0]*p.velocity[0] + p.velocity[1]*p.velocity[1]);
    CHECK(fabsf(v1 - phys.run_forward) < 0.1f, "reaches run-forward speed in ~1.5s");

    printf("\n[collision BSP]\n");
    hta_bsp_mesh coll;
    CHECK(hta_bsp_load_collision(&c, &coll, err, sizeof(err)), "collision BSP extracts");
    CHECK(coll.vertex_count > 100 && coll.index_count > 300, "collision mesh is non-trivial");
    printf("  collision %u verts %u tris\n", coll.vertex_count, coll.index_count / 3);
    hta_collision col;
    CHECK(hta_collision_build(&col, &coll), "collision grid from BSP");
    hta_collision_free(&col);
    hta_bsp_free(&coll);

    printf("\n[weapon]\n");
    hta_weapon_def wdef;
    CHECK(hta_weapon_load_default(&c, NULL, &wdef, NULL, err, sizeof(err)), "default weap loads");
    printf("  %s ROF %.2f cooldown %.3f fp_model=0x%X\n", wdef.path, wdef.rof, wdef.cooldown, wdef.fp_model_id);
    CHECK(wdef.rof > 1.0f && wdef.rof < 20.0f, "ROF is in a Halo range");
    CHECK(wdef.fp_model_id != 0, "has first-person model tag");

    printf("\n%d checks, %d failures\n", checks, failures);
    free(data);
    return failures ? 1 : 0;
}
