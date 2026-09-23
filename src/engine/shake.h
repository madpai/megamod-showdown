/* The camera being thrown about: a tank cannon's shock wave, a rocket
 * landing next to you, the Warthog's chaingun rattling the gunner.
 *
 * Every motion is a damage effect's own (hta_damage_shake): a SHAKE, random
 * translation and rotation that fade over its duration, and an IMPULSE, a
 * kick of the view away from the blast that springs back. The camera is
 * never moved for good -- the platform draws through a shaken copy.
 *
 * Portable: no renderer.
 */
#ifndef HTA_SHAKE_H
#define HTA_SHAKE_H
#include "../asset/effect.h"
#include "camera.h"

#define HTA_SHAKE_MAX 8u

typedef struct {
    float left, time;        /* seconds */
    float move, rot;         /* amplitude: wu and radians */
    float kick_yaw, kick_pitch, push;   /* impulse, already aimed */
    bool  impulse;
    float seed;
} hta_shake_one;

typedef struct {
    hta_shake_one one[HTA_SHAKE_MAX];
    float clock;
} hta_shake;

void hta_shake_init(hta_shake *s);
/* What a damage effect does to a camera at `cam_pos`, from `at` (NULL: it
 * is the camera's own -- a firing kick). Strength falls off across the
 * effect's radius bounds. */
void hta_shake_add(hta_shake *s, const hta_damage_shake *d, const hta_camera *cam,
                   const float at[3]);
void hta_shake_update(hta_shake *s, float dt);
/* Apply this frame's offsets to a copy of the camera. */
void hta_shake_apply(const hta_shake *s, hta_camera *cam);
/* How hard the camera is moving now, for tests: radians plus wu. */
float hta_shake_amount(const hta_shake *s);
#endif
