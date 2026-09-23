#include "shake.h"
#include <math.h>
#include <string.h>

void hta_shake_init(hta_shake *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

static hta_shake_one *slot(hta_shake *s)
{
    hta_shake_one *best = &s->one[0];
    for (uint32_t i = 0; i < HTA_SHAKE_MAX; i++) {
        if (s->one[i].left <= 0.0f) return &s->one[i];
        if (s->one[i].left < best->left) best = &s->one[i];
    }
    return best;
}

void hta_shake_add(hta_shake *s, const hta_damage_shake *d, const hta_camera *cam,
                   const float at[3])
{
    if (!s || !d || !cam) return;
    float k = 1.0f, dir[3] = { 0, 0, 0 };
    if (at) {
        float v[3] = { cam->pos[0]-at[0], cam->pos[1]-at[1], cam->pos[2]-at[2] };
        float dist = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        if (!(d->radius[1] > 0.0f) || dist >= d->radius[1]) return;
        if (dist > d->radius[0])
            k = 1.0f - (dist - d->radius[0]) / (d->radius[1] - d->radius[0]);
        if (dist > 1e-4f) for (int i = 0; i < 3; i++) dir[i] = v[i] / dist;
    }
    if (k <= 0.0f) return;
    if (d->shake_time > 0.0f && (d->shake_move > 0.0f || d->shake_rot > 0.0f)) {
        hta_shake_one *o = slot(s);
        memset(o, 0, sizeof(*o));
        o->left = o->time = d->shake_time;
        o->move = d->shake_move * k;
        o->rot = d->shake_rot * k;
        o->seed = s->clock * 7.31f + (float)(o - s->one) * 1.7f;
    }
    if (d->impulse_time > 0.0f && (d->impulse_rot > 0.0f || d->impulse_push > 0.0f)) {
        hta_shake_one *o = slot(s);
        memset(o, 0, sizeof(*o));
        o->impulse = true;
        o->left = o->time = d->impulse_time;
        /* The view is thrown away from the blast: up and aside by where it
         * came from relative to the look; a kick of one's own tips up. */
        float fwd[3], right[3];
        hta_camera_forward(cam, fwd);
        hta_camera_right(cam, right);
        float side = at ? dir[0]*right[0] + dir[1]*right[1] + dir[2]*right[2] : 0.0f;
        o->kick_pitch = d->impulse_rot * k;
        o->kick_yaw = -d->impulse_rot * k * side;
        o->push = d->impulse_push * k;
        (void)fwd;
    }
}

void hta_shake_update(hta_shake *s, float dt)
{
    if (!s || !(dt > 0.0f)) return;
    s->clock += dt;
    for (uint32_t i = 0; i < HTA_SHAKE_MAX; i++)
        if (s->one[i].left > 0.0f) s->one[i].left -= dt;
}

/* Smooth noise in -1..1: a few incommensurate sines. */
static float wobble(float t, float seed)
{
    return 0.5f * sinf(t * 37.0f + seed) + 0.3f * sinf(t * 61.0f + seed * 2.3f) +
           0.2f * sinf(t * 97.0f + seed * 3.7f);
}

void hta_shake_apply(const hta_shake *s, hta_camera *cam)
{
    if (!s || !cam) return;
    float fwd[3];
    hta_camera_forward(cam, fwd);
    for (uint32_t i = 0; i < HTA_SHAKE_MAX; i++) {
        const hta_shake_one *o = &s->one[i];
        if (o->left <= 0.0f || !(o->time > 0.0f)) continue;
        float f = o->left / o->time;            /* 1 .. 0 */
        if (o->impulse) {
            /* Snaps out at once, springs back over the duration. */
            float e = f * f;
            cam->pitch += o->kick_pitch * e;
            cam->yaw += o->kick_yaw * e;
            for (int k = 0; k < 3; k++) cam->pos[k] -= fwd[k] * o->push * e;
        } else {
            float t = s->clock;
            cam->yaw += o->rot * f * wobble(t, o->seed);
            cam->pitch += o->rot * f * wobble(t, o->seed + 11.0f);
            cam->pos[0] += o->move * f * wobble(t, o->seed + 23.0f);
            cam->pos[1] += o->move * f * wobble(t, o->seed + 31.0f);
            cam->pos[2] += o->move * f * wobble(t, o->seed + 43.0f);
        }
    }
    if (cam->pitch > 1.55f) cam->pitch = 1.55f;
    if (cam->pitch < -1.55f) cam->pitch = -1.55f;
}

float hta_shake_amount(const hta_shake *s)
{
    float a = 0.0f;
    for (uint32_t i = 0; s && i < HTA_SHAKE_MAX; i++) {
        const hta_shake_one *o = &s->one[i];
        if (o->left <= 0.0f || !(o->time > 0.0f)) continue;
        float f = o->left / o->time;
        a += o->impulse ? (fabsf(o->kick_pitch) + fabsf(o->kick_yaw) + o->push) * f * f
                        : (o->move + o->rot) * f;
    }
    return a;
}
