/* Player physics pulled from Trial tags (globals player information +
 * characters\cyborg_mp). Layouts from Invader's matg/bipd/unit definitions;
 * values measured on bloodgulch.map.
 *
 * This is the pawn recipe Halo already shipped. We do not reimplement the
 * original engine — we obey these numbers.
 */
#ifndef HTA_BIPED_H
#define HTA_BIPED_H

#include "cache.h"

#define HTA_TICK_HZ  30.0f

/* matg.player_information reflexive — measured at +0x170 on Trial globals. */
#define HTA_MATG_PLAYER_INFO     0x170u
#define HTA_PINFO_WALK           44u
#define HTA_PINFO_SIZE           160u

/* bipd inherits unit inherits object. Object 380 + unit 372 = 752. */
#define HTA_BIPD_UNIT            380u
#define HTA_BIPD_BODY            752u
#define HTA_UNIT_FOV             36u
#define HTA_BIPD_TURN            0u
#define HTA_BIPD_FLAGS           4u
#define HTA_BIPD_SLOPE           104u
#define HTA_BIPD_DOWN_SCALE      116u
#define HTA_BIPD_UP_SCALE        128u
#define HTA_BIPD_JUMP            196u   /* world units per tick */
#define HTA_BIPD_CAM_STAND       272u
#define HTA_BIPD_CAM_CROUCH      276u
#define HTA_BIPD_CROUCH_TIME     280u
#define HTA_BIPD_COLL_STAND      308u
#define HTA_BIPD_COLL_CROUCH     312u
#define HTA_BIPD_COLL_RADIUS     316u
#define HTA_BIPD_FLAG_PLAYER_PHYS (1u << 1)

typedef struct {
    float run_forward, run_back, run_side;
    float run_accel;
    float sneak_forward, sneak_back, sneak_side;
    float sneak_accel;
    float air_accel;
    float walk_speed;          /* unused for MP run, kept from tag */
    float jump_speed;          /* wu/s, converted from per-tick */
    float gravity;
    float cam_stand, cam_crouch, crouch_time;
    float coll_stand, coll_crouch, radius;
    float max_slope;           /* radians */
    float downhill_scale, uphill_scale;
    float fov_y;
    int   loaded;
} hta_player_physics;

void hta_player_physics_defaults(hta_player_physics *p);

/* Overlay tag values on defaults. Prefers cyborg_mp. Missing tags are not fatal. */
bool hta_player_physics_load(hta_player_physics *p, const hta_cache *c,
                             char *err, size_t errlen);

#endif
