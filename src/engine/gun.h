/* Offline single-player gun: hitscan + impact marks. Not Halo's weapon system. */
#ifndef HTA_GUN_H
#define HTA_GUN_H

#include "player.h"
#include "camera.h"
#include "../asset/bsp.h"

#define HTA_GUN_MAX_HITS 48
#define HTA_GUN_COOLDOWN 0.14f
#define HTA_GUN_RANGE    180.0f

typedef struct {
    float pos[3];
    float nrm[3];
} hta_hitmark;

typedef struct {
    hta_hitmark hits[HTA_GUN_MAX_HITS];
    uint32_t    n, next;
    float       cooldown;
    float       fire_interval; /* seconds; from weap ROF */
    int         dirty;
    hta_bsp_mesh mesh;

    /* Shot spread, from the weapon trigger's own error fields. Halo does not
     * bloom the reticle -- there is no field for it in the HUD tag -- it
     * widens the cone the shot actually goes down. `error` runs 0..1 and is
     * what the cone half-angle lerps along. */
    float    error;             /* 0 settled .. 1 fully bloomed */
    float    error_angle[2];    /* radians, settled -> bloomed */
    float    error_accel;       /* seconds of fire to reach 1 */
    float    error_decel;       /* seconds of rest to return to 0 */
    float    since_shot;        /* seconds since the last round left */
    uint32_t rng;
} hta_gun;

/* Take the trigger's error fields. Without this the gun keeps its defaults
 * and shoots a perfect ray. */
void hta_gun_set_error(hta_gun *g, const float error_angle[2],
                       float accel, float decel);

/* The cone half-angle a shot would use right now, in radians. */
float hta_gun_spread(const hta_gun *g);

/* The direction the next round would take from an aim direction: inside the
 * current cone, and a different one each call. Exposed so a test can measure
 * the scatter without a collision mesh. */
void hta_gun_shot_dir(hta_gun *g, const float aim[3], float out[3]);

void hta_gun_init(hta_gun *g);
void hta_gun_free(hta_gun *g);
/* Would a trigger pull fire right now? Ammo has to be checked before
 * hta_gun_fire, because that call spends the cooldown whether or not the
 * magazine could pay for the shot. */
int  hta_gun_ready(const hta_gun *g);
/* Returns 1 if a shot fired (and possibly hit). */
int  hta_gun_fire(hta_gun *g, const hta_collision *col, const hta_camera *cam);
void hta_gun_update(hta_gun *g, float dt);
/* Rebuilds g->mesh from impact marks. Call when dirty before GPU upload. */
void hta_gun_build_mesh(hta_gun *g);

#endif
