#include "camera.h"
#include <math.h>
#include <string.h>

#define PITCH_LIMIT 1.5533431f   /* ~89 degrees */

void hta_camera_init(hta_camera *c)
{
    memset(c, 0, sizeof(*c));
    c->fov_y  = 1.2217305f;      /* 70 degrees */
    c->aspect = 16.0f / 9.0f;
    c->znear  = 0.05f;
    c->zfar   = 2000.0f;
}

void hta_camera_forward(const hta_camera *c, float out[3])
{
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    out[0] = cosf(c->yaw) * cp;
    out[1] = sinf(c->yaw) * cp;
    out[2] = sp;
}

void hta_camera_right(const hta_camera *c, float out[3])
{
    /* right = normalize(cross(forward, worldUp)), worldUp = +Z.
     * For a Z-up yaw this reduces to a flat vector, which is what an FPS
     * wants: strafing must not drift vertically when you look up. */
    out[0] =  sinf(c->yaw);
    out[1] = -cosf(c->yaw);
    out[2] =  0.0f;
}

void hta_camera_up(const hta_camera *c, float out[3])
{
    float f[3], r[3];
    hta_camera_forward(c, f);
    hta_camera_right(c, r);
    out[0] = r[1]*f[2] - r[2]*f[1];
    out[1] = r[2]*f[0] - r[0]*f[2];
    out[2] = r[0]*f[1] - r[1]*f[0];
}

void hta_camera_look(hta_camera *c, float dyaw, float dpitch)
{
    c->yaw += dyaw;
    /* keep yaw bounded so long sessions cannot lose float precision */
    const float TWO_PI = 6.2831853f;
    if (c->yaw >  TWO_PI) c->yaw = fmodf(c->yaw, TWO_PI);
    if (c->yaw < -TWO_PI) c->yaw = fmodf(c->yaw, TWO_PI);

    c->pitch += dpitch;
    if (c->pitch >  PITCH_LIMIT) c->pitch =  PITCH_LIMIT;
    if (c->pitch < -PITCH_LIMIT) c->pitch = -PITCH_LIMIT;
}

hta_mat4 hta_mat4_identity(void)
{
    hta_mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

hta_mat4 hta_mat4_mul(const hta_mat4 *a, const hta_mat4 *b)
{
    /* column-major: result = a * b */
    hta_mat4 r;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a->m[k * 4 + row] * b->m[col * 4 + k];
            r.m[col * 4 + row] = s;
        }
    }
    return r;
}

static void v3sub(const float a[3], const float b[3], float o[3])
{ o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2]; }

static void v3cross(const float a[3], const float b[3], float o[3])
{
    o[0]=a[1]*b[2]-a[2]*b[1];
    o[1]=a[2]*b[0]-a[0]*b[2];
    o[2]=a[0]*b[1]-a[1]*b[0];
}

static float v3dot(const float a[3], const float b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

static void v3norm(float v[3])
{
    float l = sqrtf(v3dot(v, v));
    if (l > 1e-8f) { v[0]/=l; v[1]/=l; v[2]/=l; }
}

hta_mat4 hta_mat4_look_at_zup(const float eye[3], const float target[3])
{
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    float f[3], s[3], u[3];
    v3sub(target, eye, f); v3norm(f);

    v3cross(f, up, s);
    if (v3dot(s, s) < 1e-12f) {
        /* looking straight up or down: pick any stable side vector */
        const float alt[3] = { 1.0f, 0.0f, 0.0f };
        v3cross(f, alt, s);
    }
    v3norm(s);
    v3cross(s, f, u);

    hta_mat4 r = hta_mat4_identity();
    r.m[0]=s[0]; r.m[4]=s[1]; r.m[8] =s[2];
    r.m[1]=u[0]; r.m[5]=u[1]; r.m[9] =u[2];
    /* Vulkan looks down -Z in view space, so negate forward */
    r.m[2]=-f[0]; r.m[6]=-f[1]; r.m[10]=-f[2];
    r.m[12]=-v3dot(s, eye);
    r.m[13]=-v3dot(u, eye);
    r.m[14]= v3dot(f, eye);
    return r;
}

hta_mat4 hta_mat4_perspective_zup(float fov_y, float aspect, float znear, float zfar)
{
    hta_mat4 r;
    memset(&r, 0, sizeof(r));
    float t = tanf(fov_y * 0.5f);
    if (t < 1e-6f) t = 1e-6f;
    if (aspect < 1e-6f) aspect = 1e-6f;

    r.m[0]  = 1.0f / (aspect * t);
    /* Vulkan clip space has +Y downward, so flip here rather than in shaders */
    r.m[5]  = -1.0f / t;
    r.m[10] = zfar / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (znear * zfar) / (znear - zfar);
    return r;
}

hta_mat4 hta_camera_view_proj(const hta_camera *c)
{
    float f[3], target[3];
    hta_camera_forward(c, f);
    target[0] = c->pos[0] + f[0];
    target[1] = c->pos[1] + f[1];
    target[2] = c->pos[2] + f[2];

    hta_mat4 view = hta_mat4_look_at_zup(c->pos, target);
    hta_mat4 proj = hta_mat4_perspective_zup(c->fov_y, c->aspect, c->znear, c->zfar);
    return hta_mat4_mul(&proj, &view);
}

void hta_mat4_transform(const hta_mat4 *m, const float in[4], float out[4])
{
    for (int row = 0; row < 4; row++) {
        float s = 0.0f;
        for (int k = 0; k < 4; k++) s += m->m[k * 4 + row] * in[k];
        out[row] = s;
    }
}
