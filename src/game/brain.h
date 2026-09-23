/* A bot's mind: see somebody, go after them, shoot them; otherwise go and
 * get something better to shoot them with.
 *
 * Halo CE never shipped multiplayer bots -- the Covenant's AI lives in the
 * campaign and needs the campaign's hand-placed firing positions, which
 * Blood Gulch does not have. So this is ours, built only from what a player
 * has: eyes (a ray to the target's chest), the map (the nav grid), and the
 * same controls a thumb has. It aims with a turn rate and an error that
 * settle while it tracks, so it misses the way people miss.
 *
 * Everything it does goes through the unit's input record, the same one a
 * remote player's packets fill. It never touches a body directly.
 */
#ifndef HTA_BRAIN_H
#define HTA_BRAIN_H

#include <stdbool.h>
#include <stdint.h>

#define HTA_BRAIN_PATH 96u

typedef struct {
    int32_t  target;          /* unit being fought, -1 for none */
    float    seen_ago;        /* seconds since the target was last in sight */
    float    seen_pos[3];
    bool     visible;

    uint32_t path[HTA_BRAIN_PATH];
    uint32_t path_len, path_i;
    uint32_t goal;            /* nav node being walked to, or NONE */
    int32_t  goal_item;       /* pickup slot being fetched, -1 for none */
    float    goal_pos[3];     /* where the game's objective was last planned to */
    bool     goal_game;       /* the path leads to the objective */
    float    replan;          /* seconds until the path is worked out again */

    float    look_timer;      /* seconds until the next look around */
    float    ride_alone;      /* seconds on a gun with nobody driving */
    float    react;           /* seconds before a newly seen target is fired on */
    float    aim_err[2];      /* yaw, pitch offset the aim is carrying */
    float    strafe, strafe_timer;
    float    grenade_timer;
    float    stuck_timer, last_pos[3];
    float    wander_yaw;

    uint8_t  skill;           /* 0 easy .. 3 legendary */
    uint32_t rng;
} hta_brain;

struct hta_game;

void hta_brain_init(hta_brain *b, uint8_t skill, uint32_t seed);

/* Fill g->units[unit].in for this update. */
void hta_brain_think(struct hta_game *g, int32_t unit, hta_brain *b, float dt);

/* Forget the plan: the unit has just respawned. */
void hta_brain_reset(hta_brain *b);

#endif
