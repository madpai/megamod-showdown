/* Parses Trial globals + cyborg_mp. Requires a real .map path as argv[1]. */
#include "asset/cache.h"
#include "asset/biped.h"
#include "asset/bsp.h"
#include "asset/model.h"
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
    uint32_t struct_tris = coll.index_count / 3;
    CHECK(hta_scenario_add_collision(&coll, &c, err, sizeof(err)), "object colliders append");
    printf("  %s  now %u verts %u tris\n", err, coll.vertex_count, coll.index_count / 3);
    CHECK(coll.index_count / 3 > struct_tris + 50, "scenery/vehicles add collision tris");
    hta_collision col;
    CHECK(hta_collision_build(&col, &coll), "collision grid from BSP + objects");
    hta_collision_set_slope(&col, phys.max_slope);   /* match the device */
    {
        /* First Blood Gulch warthog sits at (28.86, -90.76, 0.30). A downward
         * ray must hit the hull, not the canyon floor. */
        float orig[3] = { 28.861f, -90.757f, 5.0f };
        float dir[3]  = { 0.0f, 0.0f, -1.0f };
        float t = 0, hit[3], nrm[3];
        CHECK(hta_collision_ray(&col, orig, dir, 20.0f, &t, hit, nrm),
              "ray down onto the red-base warthog hits");
        CHECK(hit[2] > 0.55f, "hit is the hull/turret, not the ground");
        int pushed = 0;
        for (int k = 0; k < 8; k++) {
            float ang = (float)k * 0.785398f;
            float o[3] = { 28.861f + cosf(ang) * 2.5f,
                           -90.757f + sinf(ang) * 2.5f, 0.80f };
            float d[3] = { -cosf(ang), -sinf(ang), 0.0f };
            float ht, h[3], n[3];
            if (!hta_collision_ray(&col, o, d, 4.0f, &ht, h, n)) continue;
            if (fabsf(n[2]) > 0.55f) continue; /* floor/roof */
            float wx = h[0] - n[0] * 0.05f, wy = h[1] - n[1] * 0.05f;
            float x0 = wx, y0 = wy;
            hta_collision_depenetrate(&col, &wx, &wy, h[2] - 0.25f, 0.7f, 0.2f);
            float dd = sqrtf((wx - x0) * (wx - x0) + (wy - y0) * (wy - y0));
            if (dd > 0.10f) { pushed = 1; break; }
        }
        CHECK(pushed, "pill overlapping a warthog side is pushed out");
    }
    {
        /* Red-base pad is z≈1.70; canyon floor in front is z≈0.1.
         * Walking off the lip must fall, not stick on an invisible wall. */
        hta_player lp;
        hta_camera lcam;
        hta_player_init(&lp);
        hta_player_apply_physics(&lp, &phys);
        hta_camera_init(&lcam);
        lp.pos[0] = 41.54f;
        lp.pos[1] = -82.79f;
        lp.pos[2] = 1.705f;
        lp.on_ground = true;
        lcam.yaw = -1.5708f; /* -Y, toward the canyon */
        hta_player_input lin;
        memset(&lin, 0, sizeof(lin));
        lin.move_forward = 1.0f;
        for (int i = 0; i < 300; i++)
            hta_player_update(&lp, &lcam, &col, &lin, 1.0f / 60.0f);
        CHECK(lp.pos[1] < -84.5f, "walks off the red-base pad toward the canyon");
        CHECK(lp.pos[2] < 1.2f, "falls off the red-base pad instead of sticking");
    }

    /* Standing must not be shoved back by an overhanging roof. The base
     * entrances lean over the doorway at 55-70 degrees; those faces point
     * DOWN, and treating them as walls pushed the pawn horizontally, so you
     * could only get in crouched. Sweeping the map, 204 floor cells blocked a
     * standing pawn while leaving a crouching one free; the fix took that to
     * 49. Ceilings limit headroom, they do not push sideways. */
    {
        uint32_t standing_blocked = 0, crouch_blocked = 0, sampled = 0;
        for (float y = -120.0f; y <= -106.0f; y += 0.20f)
        for (float x =  98.0f;  x <= 112.0f; x += 0.20f) {
            float gz;
            if (!hta_collision_ground(&col, x, y, 40.0f, &gz)) continue;
            sampled++;
            float sx = x, sy = y;
            hta_collision_depenetrate(&col, &sx, &sy, gz + 0.02f,
                                      phys.coll_stand, phys.radius);
            float cx = x, cy = y;
            hta_collision_depenetrate(&col, &cx, &cy, gz + 0.02f,
                                      phys.coll_crouch, phys.radius);
            float ds = sqrtf((sx-x)*(sx-x) + (sy-y)*(sy-y));
            float dc = sqrtf((cx-x)*(cx-x) + (cy-y)*(cy-y));
            if (ds > 0.001f && dc <= 0.001f) standing_blocked++;
            if (dc > 0.001f) crouch_blocked++;
        }
        printf("  base entrance: %u floor cells, %u block standing only, %u block crouching\n",
               sampled, standing_blocked, crouch_blocked);
        CHECK(sampled > 100, "sampled the base entrance area");
        /* 12 before the overhang fix, 2 after. The two that remain are wall
         * corners, not roofs: near-vertical faces rising from the floor that
         * the mid-height probe catches standing but misses crouched. A proper
         * segment-vs-triangle test would clear them; 4 leaves room for that
         * without letting the 12 back in. */
        CHECK(standing_blocked <= 4,
              "standing is not shoved back where crouching walks free");
    }
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
