/* Native arcade vehicle motion for original racing content. No Halo tags.
 * The host advances this state; clients may interpolate its public pose. */
#ifndef HTA_ARCADE_RACER_H
#define HTA_ARCADE_RACER_H

#include <stdbool.h>
#include "player.h"

typedef struct {
    float max_speed, reverse_speed, acceleration, brake_acceleration;
    float coast_drag, grip, drift_grip;
    float steer_low, steer_high, steer_fade_speed, drift_yaw;
    float boost_speed, boost_acceleration, boost_seconds;
    float jump_gravity, air_steer, wall_restitution, wall_speed_loss;
    float radius, height, ground_clearance;
} hta_arcade_tuning;

typedef struct {
    float throttle, steer; /* finite, clamped to -1..1 */
    bool brake, drift;
} hta_arcade_input;

typedef struct {
    float pos[3], vel[3], yaw, pitch, roll;
    float drift_time, drift_work, boost_time;
    unsigned boost_tier;
    bool grounded, drifting;
    float collision_speed;
} hta_arcade_racer;

hta_arcade_tuning hta_arcade_default_tuning(void);
bool hta_arcade_tuning_valid(const hta_arcade_tuning *t);
void hta_arcade_reset(hta_arcade_racer *r, const float pos[3], float yaw);
unsigned hta_arcade_charge_tier(const hta_arcade_racer *r);
void hta_arcade_step(hta_arcade_racer *r, const hta_arcade_tuning *t,
                     hta_arcade_input in, const hta_collision *world, float dt);
void hta_arcade_boost_pad(hta_arcade_racer *r, const hta_arcade_tuning *t);
void hta_arcade_contact(hta_arcade_racer *a, hta_arcade_racer *b,
                        const hta_arcade_tuning *t);
void hta_arcade_contact_swept(hta_arcade_racer *a, hta_arcade_racer *b,
                              const float old_a[3], const float old_b[3],
                              const hta_arcade_tuning *t);

#endif
