/* Camera / projection tests. Pure math — verifies that world points land where
 * we expect in Vulkan clip space before any GPU is involved. */
#include "engine/camera.h"
#include <stdio.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static bool feq(float a, float b) { return fabsf(a - b) < 1e-3f; }

/* project a world point, return NDC + w */
static void project(const hta_camera *c, float x, float y, float z,
                    float *nx, float *ny, float *nz, float *w)
{
    hta_mat4 vp = hta_camera_view_proj(c);
    float in[4] = { x, y, z, 1.0f }, out[4];
    hta_mat4_transform(&vp, in, out);
    *w = out[3];
    if (fabsf(out[3]) > 1e-9f) {
        *nx = out[0] / out[3]; *ny = out[1] / out[3]; *nz = out[2] / out[3];
    } else { *nx = *ny = *nz = 0.0f; }
}

int main(void)
{
    printf("camera tests\n\n[matrix basics]\n");
    hta_mat4 i = hta_mat4_identity();
    float in[4] = {1,2,3,1}, out[4];
    hta_mat4_transform(&i, in, out);
    CHECK(feq(out[0],1)&&feq(out[1],2)&&feq(out[2],3)&&feq(out[3],1), "identity is identity");

    hta_mat4 p = hta_mat4_perspective_zup(1.2217305f, 16.0f/9.0f, 0.1f, 100.0f);
    hta_mat4 r = hta_mat4_mul(&i, &p);
    bool same = true;
    for (int k = 0; k < 16; k++) if (!feq(r.m[k], p.m[k])) same = false;
    CHECK(same, "identity * M == M");

    printf("\n[camera basis vectors, +Z up]\n");
    hta_camera c;
    hta_camera_init(&c);
    c.yaw = 0.0f; c.pitch = 0.0f;
    float f[3], rt[3];
    hta_camera_forward(&c, f);
    hta_camera_right(&c, rt);
    CHECK(feq(f[0],1)&&feq(f[1],0)&&feq(f[2],0), "yaw 0 looks along +X");
    CHECK(feq(rt[2],0), "right vector is flat (no vertical strafe drift)");
    CHECK(feq(f[0]*rt[0]+f[1]*rt[1]+f[2]*rt[2], 0), "forward and right are perpendicular");

    c.pitch = 0.7f;
    hta_camera_forward(&c, f);
    hta_camera_right(&c, rt);
    CHECK(f[2] > 0, "positive pitch looks upward (+Z)");
    CHECK(feq(rt[2], 0), "right stays flat even when pitched");

    c.yaw = 1.5707963f; c.pitch = 0.0f;
    hta_camera_forward(&c, f);
    CHECK(feq(f[0],0)&&feq(f[1],1), "yaw 90deg looks along +Y (north)");

    printf("\n[pitch clamping]\n");
    hta_camera_init(&c);
    hta_camera_look(&c, 0.0f, 100.0f);
    CHECK(c.pitch < 1.5708f && c.pitch > 1.55f, "pitch clamps just below straight up");
    hta_camera_look(&c, 0.0f, -100.0f);
    CHECK(c.pitch > -1.5708f && c.pitch < -1.55f, "pitch clamps just above straight down");

    hta_camera_init(&c);
    for (int k = 0; k < 10000; k++) hta_camera_look(&c, 0.5f, 0.0f);
    CHECK(fabsf(c.yaw) <= 6.2832f, "yaw stays bounded over many turns");

    printf("\n[projection]\n");
    hta_camera_init(&c);
    c.pos[0]=0; c.pos[1]=0; c.pos[2]=0;
    c.yaw=0; c.pitch=0; c.aspect=1.0f; c.znear=0.1f; c.zfar=100.0f;

    float nx,ny,nz,w;
    project(&c, 10.0f, 0.0f, 0.0f, &nx,&ny,&nz,&w);
    CHECK(w > 0, "point in front has positive w (not clipped)");
    CHECK(feq(nx,0)&&feq(ny,0), "point dead ahead projects to screen centre");
    CHECK(nz > 0.0f && nz < 1.0f, "depth of a mid-range point is inside [0,1]");

    project(&c, -10.0f, 0.0f, 0.0f, &nx,&ny,&nz,&w);
    CHECK(w < 0, "point behind the camera has negative w (clipped away)");

    /* Vulkan depth convention: near plane -> 0, far plane -> 1 */
    project(&c, 0.1f, 0.0f, 0.0f, &nx,&ny,&nz,&w);
    CHECK(feq(nz, 0.0f), "near plane maps to depth 0");
    project(&c, 100.0f, 0.0f, 0.0f, &nx,&ny,&nz,&w);
    CHECK(feq(nz, 1.0f), "far plane maps to depth 1");

    /* +Y in Vulkan clip space points DOWN, so a point above us must give -Y */
    project(&c, 10.0f, 0.0f, 3.0f, &nx,&ny,&nz,&w);
    CHECK(ny < 0, "a point above the camera lands in the upper half (clip Y is flipped)");
    project(&c, 10.0f, 0.0f, -3.0f, &nx,&ny,&nz,&w);
    CHECK(ny > 0, "a point below the camera lands in the lower half");

    /* +Y world (north/left when facing +X) must land on screen-left */
    project(&c, 10.0f, 3.0f, 0.0f, &nx,&ny,&nz,&w);
    CHECK(nx < 0, "a point to the world-left lands on screen-left (no mirroring)");

    printf("\n[aspect ratio]\n");
    c.aspect = 2.0f;
    project(&c, 10.0f, 3.0f, 0.0f, &nx,&ny,&nz,&w);
    float wide_x = nx;
    c.aspect = 1.0f;
    project(&c, 10.0f, 3.0f, 0.0f, &nx,&ny,&nz,&w);
    CHECK(fabsf(wide_x) < fabsf(nx), "wider aspect compresses horizontal NDC");

    printf("\n[degenerate input is survivable]\n");
    hta_mat4 bad = hta_mat4_perspective_zup(0.0f, 0.0f, 0.1f, 100.0f);
    bool finite = true;
    for (int k = 0; k < 16; k++) if (!isfinite(bad.m[k])) finite = false;
    CHECK(finite, "zero fov/aspect does not produce NaN or inf");

    float eye[3] = {0,0,0}, tgt[3] = {0,0,1};   /* straight up: degenerate cross */
    hta_mat4 lk = hta_mat4_look_at_zup(eye, tgt);
    finite = true;
    for (int k = 0; k < 16; k++) if (!isfinite(lk.m[k])) finite = false;
    CHECK(finite, "looking straight up does not produce NaN");

    printf("\n%s — %d checks, %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
