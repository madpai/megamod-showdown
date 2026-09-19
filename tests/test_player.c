/* Player + collision tests against the grid fixture (real geometry). */
#include "engine/player.h"
#include "engine/gun.h"
#include "asset/cache.h"
#include "asset/bsp.h"
#include "fixture.h"
#include <stdio.h>
#include <stdlib.h>
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
    {
        float x = p.pos[0], y = p.pos[1];
        hta_collision_depenetrate(&col, &p.pos[0], &p.pos[1], p.pos[2], 0.7f, 0.2f);
        CHECK(fabsf(p.pos[0] - x) < 0.05f && fabsf(p.pos[1] - y) < 0.05f,
              "depenetrate on a floor does not shove the player");
    }
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

    printf("\n[hitscan]\n");
    {
        float orig[3] = { 40.0f, 40.0f, 20.0f };
        float dir[3]  = { 0.0f, 0.0f, -1.0f };
        float t = 0, hit[3], nrm[3];
        CHECK(hta_collision_ray(&col, orig, dir, 100.0f, &t, hit, nrm),
              "downward ray hits the grid");
        CHECK(t > 10.0f && t < 30.0f, "hit distance is between camera and below-ground");
        CHECK(fabsf(nrm[2]) > 0.3f, "hit normal has a Z component");
    }

    printf("\n[wall pill]\n");
    {
        /* Floor 0..10 plus a vertical wall at x=5, y=0..10, z=0..3. */
        static hta_vertex wv[8];
        static uint32_t wi[12];
        memset(wv, 0, sizeof(wv));
        float pts[8][3] = {
            {0,0,0},{10,0,0},{10,10,0},{0,10,0},
            {5,0,0},{5,10,0},{5,10,3},{5,0,3}
        };
        for (int i = 0; i < 8; i++) {
            wv[i].pos[0] = pts[i][0];
            wv[i].pos[1] = pts[i][1];
            wv[i].pos[2] = pts[i][2];
            wv[i].normal[2] = 1.0f;
        }
        uint32_t tris[12] = { 0,1,2, 0,2,3, 4,5,6, 4,6,7 };
        memcpy(wi, tris, sizeof(tris));
        hta_bsp_mesh wallm;
        memset(&wallm, 0, sizeof(wallm));
        wallm.vertices = wv;
        wallm.vertex_count = 8;
        wallm.indices = wi;
        wallm.index_count = 12;
        wallm.bounds_min[0] = 0; wallm.bounds_min[1] = 0; wallm.bounds_min[2] = 0;
        wallm.bounds_max[0] = 10; wallm.bounds_max[1] = 10; wallm.bounds_max[2] = 3;

        hta_collision wcol;
        CHECK(hta_collision_build(&wcol, &wallm), "wall-room collision builds");
        float gz = -1.0f;
        CHECK(hta_collision_ground(&wcol, 2.0f, 5.0f, 2.0f, &gz) && fabsf(gz) < 0.05f,
              "floor still resolves on the west side of the wall");

        float wx = 4.95f, wy = 5.0f;
        hta_collision_depenetrate(&wcol, &wx, &wy, 0.0f, 0.7f, 0.2f);
        CHECK(wx < 5.0f - 0.19f && wx > 5.0f - 0.25f,
              "pill overlapping a wall is pushed out to radius");
        CHECK(fabsf(wy - 5.0f) < 0.02f, "wall push is along the face normal (no Y drift)");

        hta_player wp;
        hta_camera wcam;
        hta_player_init(&wp);
        hta_camera_init(&wcam);
        wp.phys.radius = 0.2f;
        wp.radius = 0.2f;
        wp.pos[0] = 2.0f; wp.pos[1] = 5.0f; wp.pos[2] = 0.0f;
        wp.on_ground = true;
        wcam.yaw = 0.0f; /* face +X, into the wall */
        hta_player_input win;
        memset(&win, 0, sizeof(win));
        win.move_forward = 1.0f;
        for (int i = 0; i < 180; i++)
            hta_player_update(&wp, &wcam, &wcol, &win, 1.0f / 60.0f);
        CHECK(wp.pos[0] < 5.0f - 0.15f, "walking into a wall stops before the surface");
        CHECK(wp.pos[0] > 2.5f, "walking into a wall still reaches it");
        CHECK(fabsf(wp.pos[1] - 5.0f) < 0.3f, "wall slide does not throw the pawn sideways");

        /* --- overhangs must not shove the pawn sideways ---------------------
         * A sloped roof above your head is a headroom limit, not a wall. The
         * pawn is a cylinder from the feet to `height`; anything entirely
         * above that cannot touch it. This is what made the Blood Gulch base
         * doorways impassable standing: the old test probed a single height
         * and clamped the result, so a leaning roof read as near. */
        {
            /* One steeply sloped quad (normal tilted ~68deg, facing down),
             * spanning x 4.8..5.2, parked at a height we can slide up/down. */
            for (int above = 0; above < 2; above++) {
                float base_z = above ? 1.40f : 0.25f;
                hta_vertex ov[4];
                memset(ov, 0, sizeof(ov));
                ov[0].pos[0]=4.8f; ov[0].pos[1]=4.0f; ov[0].pos[2]=base_z;
                ov[1].pos[0]=5.2f; ov[1].pos[1]=4.0f; ov[1].pos[2]=base_z+1.0f;
                ov[2].pos[0]=5.2f; ov[2].pos[1]=6.0f; ov[2].pos[2]=base_z+1.0f;
                ov[3].pos[0]=4.8f; ov[3].pos[1]=6.0f; ov[3].pos[2]=base_z;
                uint32_t oi[6] = { 0,2,1, 0,3,2 };
                hta_bsp_mesh om;
                memset(&om, 0, sizeof(om));
                om.vertices = ov; om.vertex_count = 4;
                om.indices = oi;  om.index_count = 6;
                om.bounds_min[0]=0; om.bounds_min[1]=0; om.bounds_min[2]=0;
                om.bounds_max[0]=10; om.bounds_max[1]=10; om.bounds_max[2]=4;
                hta_collision oc;
                if (!hta_collision_build(&oc, &om)) { CHECK(0, "overhang collision builds"); break; }
                float ox = 4.95f, oy = 5.0f;
                hta_collision_depenetrate(&oc, &ox, &oy, 0.0f, 0.7f, 0.2f);
                float moved = fabsf(ox - 4.95f) + fabsf(oy - 5.0f);
                if (above)
                    CHECK(moved < 0.001f,
                          "a roof above the head does not push the pawn sideways");
                else
                    CHECK(moved > 0.01f,
                          "the same roof at chest height still blocks");
                hta_collision_free(&oc);
            }
        }
        float vwall = wp.velocity[0];
        CHECK(vwall < 0.3f, "inbound velocity is cancelled after the wall push");
        hta_collision_free(&wcol);
    }

    printf("\n[walk off a ledge]\n");
    {
        /* Raised pad 0..6 at z=2, canyon floor 6..12 at z=0, and the pad's
         * vertical drop at x=6 from z=0..2. Walking off must fall, not stop
         * as if the lip were a wall. */
        static hta_vertex lv[8];
        static uint32_t li[18];
        memset(lv, 0, sizeof(lv));
        float pts2[8][3] = {
            {0,0,2},{6,0,2},{6,10,2},{0,10,2},
            {6,0,0},{12,0,0},{12,10,0},{6,10,0}
        };
        for (int i = 0; i < 8; i++) {
            lv[i].pos[0] = pts2[i][0];
            lv[i].pos[1] = pts2[i][1];
            lv[i].pos[2] = pts2[i][2];
            lv[i].normal[2] = 1.0f;
        }
        uint32_t ltris[18] = {
            0,1,2, 0,2,3,           /* pad */
            4,5,6, 4,6,7,           /* canyon */
            1,4,7, 1,7,2            /* vertical lip at x=6 */
        };
        memcpy(li, ltris, sizeof(ltris));
        hta_bsp_mesh ledgem;
        memset(&ledgem, 0, sizeof(ledgem));
        ledgem.vertices = lv;
        ledgem.vertex_count = 8;
        ledgem.indices = li;
        ledgem.index_count = 18;
        ledgem.bounds_min[0] = 0; ledgem.bounds_min[1] = 0; ledgem.bounds_min[2] = 0;
        ledgem.bounds_max[0] = 12; ledgem.bounds_max[1] = 10; ledgem.bounds_max[2] = 2;

        hta_collision lcol;
        CHECK(hta_collision_build(&lcol, &ledgem), "ledge collision builds");
        float lx = 5.90f, ly = 5.0f;
        hta_collision_depenetrate(&lcol, &lx, &ly, 2.0f, 0.7f, 0.2f);
        CHECK(lx > 5.85f, "standing on the pad lip is not shoved inland");

        hta_player lp;
        hta_camera lcam;
        hta_player_init(&lp);
        hta_camera_init(&lcam);
        lp.phys.radius = 0.2f;
        lp.radius = 0.2f;
        lp.pos[0] = 4.0f; lp.pos[1] = 5.0f; lp.pos[2] = 2.0f;
        lp.on_ground = true;
        lcam.yaw = 0.0f;
        hta_player_input lin;
        memset(&lin, 0, sizeof(lin));
        lin.move_forward = 1.0f;
        for (int i = 0; i < 240; i++)
            hta_player_update(&lp, &lcam, &lcol, &lin, 1.0f / 60.0f);
        CHECK(lp.pos[0] > 6.2f, "walking off the pad crosses the lip");
        CHECK(lp.pos[2] < 1.5f, "walking off the pad falls toward the canyon");
        hta_collision_free(&lcol);
    }

    /* Walking DOWN a slope must hug it, exactly as walking up does.
     *
     * Gravity alone does not: the surface falls away faster in one frame than
     * a standing start falls, so the pawn leaves the ramp and floats a little
     * the whole way down. That float raises the head by the same amount, so a
     * leaning lintel with clearance between the standing height and that
     * height blocks the pawn one way and not the other -- a doorway you can
     * walk up through but not back down. It also reports "air" for the whole
     * descent, which costs the jump and the sneak speed.
     *
     * Pinned on synthetic geometry because it is a property of the
     * integrator, not of any one map. The lintel leans (60 degrees), like
     * Blood Gulch's base entrances: a flat ceiling is correctly skipped for
     * horizontal push, so only a leaning one can show the symmetry. */
    printf("\n[down a slope]\n");
    {
        static hta_vertex rv[8];
        static uint32_t   ri[12];
        memset(rv, 0, sizeof(rv));
        const float slope = 0.36f;      /* ~20-degree ramp descending along +X */
        #define RAMPZ(X) (2.0f - slope * (X))
        /* Lintel low edge, measured where the pawn's cylinder first touches it
         * (x = 5 minus the 0.2 radius) -- the tight side, because the ramp
         * descends and the uphill approach carries the head highest. 0.707 of
         * clearance: a 0.700 standing head passes, the 0.7148 head a floating
         * pawn has does not. */
        const float lz = RAMPZ(4.8f) + 0.707f;
        float rp[8][3] = {
            {0,0,RAMPZ(0)}, {10,0,RAMPZ(10)}, {10,6,RAMPZ(10)}, {0,6,RAMPZ(0)},
            {5.0f,0,lz}, {5.5f,0,lz+0.87f}, {5.5f,6,lz+0.87f}, {5.0f,6,lz},
        };
        for (int i = 0; i < 8; i++) {
            rv[i].pos[0] = rp[i][0];
            rv[i].pos[1] = rp[i][1];
            rv[i].pos[2] = rp[i][2];
            rv[i].normal[2] = 1.0f;
        }
        uint32_t rtris[12] = { 0,1,2, 0,2,3,  4,5,6, 4,6,7 };
        memcpy(ri, rtris, sizeof(rtris));
        hta_bsp_mesh rampm;
        memset(&rampm, 0, sizeof(rampm));
        rampm.vertices = rv;
        rampm.vertex_count = 8;
        rampm.indices = ri;
        rampm.index_count = 12;
        rampm.bounds_min[0] = 0; rampm.bounds_min[1] = 0; rampm.bounds_min[2] = -2;
        rampm.bounds_max[0] = 10; rampm.bounds_max[1] = 6; rampm.bounds_max[2] = 4;

        hta_collision rcol;
        CHECK(hta_collision_build(&rcol, &rampm), "ramp collision builds");
        hta_collision_set_slope(&rcol, 45.0f * 0.01745329f);   /* the biped's */

        float reached[2] = {0.0f, 0.0f};
        for (int dir = 0; dir < 2; dir++) {
            hta_player rpl;
            hta_camera rcam;
            hta_player_init(&rpl);
            hta_camera_init(&rcam);
            rpl.phys.radius = 0.2f;
            rpl.radius = 0.2f;
            rpl.phys.coll_stand = 0.70f;
            rpl.phys.coll_crouch = 0.50f;
            rpl.pos[0] = dir ? 8.4f : 1.6f;
            rpl.pos[1] = 3.0f;
            rpl.pos[2] = RAMPZ(rpl.pos[0]);
            rpl.on_ground = true;
            rcam.yaw = dir ? 3.14159265f : 0.0f;   /* 0: downhill, 1: uphill */
            hta_player_input rin;
            memset(&rin, 0, sizeof(rin));
            rin.move_forward = 1.0f;

            float worst_float = 0.0f;
            int airborne = 0;
            /* Stop at the far end of the ramp: walking off it is a fall,
             * and a fall is airborne for good reason. */
            int frames = 0;
            for (int i = 0; i < 480; i++) {
                if (rpl.pos[0] > 8.5f || rpl.pos[0] < 1.5f) break;
                hta_player_update(&rpl, &rcam, &rcol, &rin, 1.0f / 60.0f);
                float f = rpl.pos[2] - RAMPZ(rpl.pos[0]);
                if (f > worst_float) worst_float = f;
                if (!rpl.on_ground) airborne++;
                frames++;
            }
            reached[dir] = rpl.pos[0];
            printf("  %s: reached x %.2f, worst float %.4f, %d/%d frames airborne\n",
                   dir ? "up  " : "down", rpl.pos[0], worst_float, airborne, frames);
            if (dir == 0) {
                CHECK(worst_float < 0.005f, "walking down a slope stays on its surface");
                CHECK(airborne * 20 < frames, "walking down a slope does not go airborne");
            } else {
                CHECK(worst_float < 0.005f, "walking up a slope stays on its surface");
            }
        }
        /* The symmetry the reporter actually felt. These are a loose backstop,
         * not the sharp edge of this test: at 0.007 of spare clearance the
         * 0.0148 float only just fails to push, so it is the float checks
         * above that catch the regression. A larger float gets stuck here. */
        CHECK(reached[1] <= 1.5f, "the lintel does not block the pawn walking UP");
        CHECK(reached[0] >= 8.5f, "the lintel does not block it walking DOWN");
        #undef RAMPZ
        hta_collision_free(&rcol);
    }

    /* A lip over a descending ramp: the base ramp, in miniature.
     *
     * The pawn is a cylinder with a ROUNDED crown, not a flat disc. A flat top
     * meets an overhead lip a full radius early, and on a ramp the floor is
     * still (radius * slope) higher back there -- 0.12 wu here -- so it bangs
     * its head on a lip it clears completely a step later. The clearance at
     * the lip is 0.76 against a 0.70 pawn: it fits, and must be allowed to.
     *
     * Crouching must not be the only way through; that was the report. */
    printf("\n[lip over a ramp]\n");
    {
        static hta_vertex pv[12];
        static uint32_t   pi[18];
        memset(pv, 0, sizeof(pv));
        const float sl = 0.6f;          /* ~31-degree ramp descending along +X */
        const float lipx = 2.1f;        /* lip face here; clearance 0.76 under it */
        #define GZ(X) (2.0f - sl * (X))
        float pp[12][3] = {
            /* ramp */
            {0,0,GZ(0)}, {6,0,GZ(6)}, {6,6,GZ(6)}, {0,6,GZ(0)},
            /* the lip's vertical front face, 1.50 .. 1.70, normal toward -X */
            {lipx,0,1.50f}, {lipx,0,1.70f}, {lipx,6,1.70f}, {lipx,6,1.50f},
            /* the slab underside beyond it: a ceiling, correctly ignored */
            {lipx,0,1.50f}, {6,0,1.50f}, {6,6,1.50f}, {lipx,6,1.50f},
        };
        for (int i = 0; i < 12; i++) {
            pv[i].pos[0] = pp[i][0];
            pv[i].pos[1] = pp[i][1];
            pv[i].pos[2] = pp[i][2];
            pv[i].normal[2] = 1.0f;
        }
        uint32_t ptris[18] = { 0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,10,9, 8,11,10 };
        memcpy(pi, ptris, sizeof(ptris));
        hta_bsp_mesh lipm;
        memset(&lipm, 0, sizeof(lipm));
        lipm.vertices = pv;
        lipm.vertex_count = 12;
        lipm.indices = pi;
        lipm.index_count = 18;
        lipm.bounds_min[0] = 0; lipm.bounds_min[1] = 0; lipm.bounds_min[2] = -2;
        lipm.bounds_max[0] = 6; lipm.bounds_max[1] = 6; lipm.bounds_max[2] = 2;

        hta_collision lcol2;
        CHECK(hta_collision_build(&lcol2, &lipm), "lip collision builds");
        hta_collision_set_slope(&lcol2, 45.0f * 0.01745329f);

        /* Clearance under the lip really is more than a standing pawn needs:
         * if it were not, being blocked would be correct. */
        CHECK(1.50f - GZ(lipx) > 0.70f, "the lip is high enough to walk under");

        float got[2] = {0.0f, 0.0f};
        for (int crouched = 0; crouched < 2; crouched++) {
            hta_player lp2;
            hta_camera lc2;
            hta_player_init(&lp2);
            hta_camera_init(&lc2);
            lp2.phys.radius = 0.2f;
            lp2.radius = 0.2f;
            lp2.phys.coll_stand = 0.70f;
            lp2.phys.coll_crouch = 0.50f;
            lp2.pos[0] = 0.5f; lp2.pos[1] = 3.0f; lp2.pos[2] = GZ(0.5f);
            lp2.on_ground = true;
            lc2.yaw = 0.0f;                 /* +X, down the ramp and under the lip */
            hta_player_input li2;
            memset(&li2, 0, sizeof(li2));
            li2.move_forward = 1.0f;
            li2.crouch = crouched ? true : false;
            for (int i = 0; i < 600; i++)
                hta_player_update(&lp2, &lc2, &lcol2, &li2, 1.0f / 120.0f);
            got[crouched] = lp2.pos[0];
            printf("  %s reached x %.3f (lip at %.2f)\n",
                   crouched ? "crouching:" : "standing: ", lp2.pos[0], lipx);
        }
        CHECK(got[1] > 3.5f, "crouching gets under the lip");
        CHECK(got[0] > 3.5f, "STANDING gets under the lip too");
        #undef GZ
        hta_collision_free(&lcol2);
    }

    /* Sustained fire widens the shot cone, exactly as the weapon trigger's
     * own error fields say -- Halo puts the reactivity in where the round
     * goes, not in the reticle, which has no field for it. */
    printf("\n[shot spread]\n");
    {
        hta_gun gun;
        hta_gun_init(&gun);
        float ea[2] = { 0.0349f, 0.1134f };     /* the Trial AR: 2 -> 6.5 deg */
        hta_gun_set_error(&gun, ea, 0.60f, 1.00f);
        gun.fire_interval = 1.0f / 15.0f;

        CHECK(fabsf(hta_gun_spread(&gun) - 0.0349f) < 1e-4f,
              "a rested weapon shoots its tightest cone");

        hta_camera gcam;
        hta_camera_init(&gcam);
        /* Hold the trigger for the tagged bloom time. */
        for (int i = 0; i < 60; i++) {
            hta_gun_update(&gun, 1.0f / 60.0f);
            if (hta_gun_ready(&gun)) hta_gun_fire(&gun, NULL, &gcam);
        }
        printf("  after 1.0 s of fire: %.3f deg\n", hta_gun_spread(&gun) * 57.2957795f);
        CHECK(fabsf(hta_gun_spread(&gun) - 0.1134f) < 1e-3f,
              "holding the trigger reaches the widest cone");

        /* And it settles again over the tagged deceleration time. */
        for (int i = 0; i < 30; i++) hta_gun_update(&gun, 1.0f / 60.0f);
        printf("  half a second later:  %.3f deg\n", hta_gun_spread(&gun) * 57.2957795f);
        CHECK(hta_gun_spread(&gun) < 0.1134f && hta_gun_spread(&gun) > 0.0349f,
              "letting go settles it part way back");
        for (int i = 0; i < 60; i++) hta_gun_update(&gun, 1.0f / 60.0f);
        CHECK(fabsf(hta_gun_spread(&gun) - 0.0349f) < 1e-4f,
              "and all the way back, not past it");

        /* The bloom must not outrun the tag: a single shot is still tight. */
        hta_gun g2;
        hta_gun_init(&g2);
        hta_gun_set_error(&g2, ea, 0.60f, 1.00f);
        g2.fire_interval = 1.0f / 15.0f;
        hta_gun_fire(&g2, NULL, &gcam);
        hta_gun_update(&g2, 1.0f / 60.0f);
        CHECK(hta_gun_spread(&g2) < 0.045f, "one shot barely moves the cone");

        /* A tag with no ramp time must not divide by zero. */
        hta_gun g3;
        hta_gun_init(&g3);
        hta_gun_set_error(&g3, ea, 0.0f, 0.0f);
        hta_gun_update(&g3, 1.0f / 60.0f);
        CHECK(hta_gun_spread(&g3) >= 0.0f && hta_gun_spread(&g3) <= 0.1134f,
              "a zero ramp time falls back to a sane default");

        /* Rounds must actually scatter, and stay inside the cone. */
        hta_gun g4;
        hta_gun_init(&g4);
        hta_gun_set_error(&g4, ea, 0.60f, 1.00f);
        g4.fire_interval = 1.0f / 15.0f;
        g4.error = 1.0f;                     /* fully bloomed */
        float fwd[3];
        hta_camera_forward(&gcam, fwd);
        float worst = 0.0f;
        int distinct = 0;
        float prev = -2.0f;
        for (int i = 0; i < 200; i++) {
            float d[3];
            hta_gun_shot_dir(&g4, fwd, d);
            float dot = d[0]*fwd[0] + d[1]*fwd[1] + d[2]*fwd[2];
            if (dot > 1.0f) dot = 1.0f;
            float ang = acosf(dot);
            if (ang > worst) worst = ang;
            if (fabsf(dot - prev) > 1e-6f) distinct++;
            prev = dot;
        }
        printf("  200 rounds at full bloom: worst %.3f deg of %.3f\n",
               worst * 57.2957795f, 0.1134f * 57.2957795f);
        CHECK(worst <= 0.1134f + 1e-4f, "no round leaves the tagged cone");
        CHECK(worst > 0.05f, "and they really do scatter across it");
        CHECK(distinct > 190, "each round gets its own direction");
    }

    printf("\n[rebind after realloc]\n");
    {
        uint32_t nv = mesh.vertex_count, ni = mesh.index_count;
        hta_vertex *nvtx = (hta_vertex *)realloc(mesh.vertices, (nv + 8) * sizeof(hta_vertex));
        uint32_t *nidx = (uint32_t *)realloc(mesh.indices, (ni + 8) * sizeof(uint32_t));
        CHECK(nvtx && nidx, "realloc mesh arrays (simulates scenery append)");
        if (nvtx) mesh.vertices = nvtx;
        if (nidx) mesh.indices = nidx;
        hta_collision_rebind(&col, mesh.vertices, mesh.indices);
        float gz = 0.0f;
        CHECK(hta_collision_ground(&col, 40.0f, 40.0f, 100.0f, &gz),
              "ground query still works after realloc+rebind");
    }

    hta_collision_free(&col);
    CHECK(col.tri_index == NULL, "collision free clears state");
    hta_bsp_free(&mesh);

    printf("\n%s — %d checks, %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
