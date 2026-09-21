/* First drivable slice: tag-driven human jeeps, kinematic chassis with
 * wheel-ground support. Not a rigid-body/suspension simulation. */
#ifndef HTA_VEHICLE_H
#define HTA_VEHICLE_H
#include "player.h"
#include "../asset/model.h"

#define HTA_VEHICLE_MAX 32u
#define HTA_VEHICLE_PLACEMENTS 512u
#define HTA_VEHICLE_MASS_POINTS 32u
/* Engine choices, not tag fields. */
#define HTA_VEHICLE_ENTER_REACH 0.9f
#define HTA_VEHICLE_STEP (1.0f / 120.0f)
#define HTA_VEHICLE_CAMERA_BACK 2.5f
#define HTA_VEHICLE_CAMERA_UP 0.7f
#define HTA_VEHICLE_CLEARANCE 0.04f
#define HTA_VEHICLE_MAX_SLOPE 0.75f
#define HTA_VEHICLE_EXIT_SPEED 0.5f

typedef struct { float pos[3], radius; bool wheel; } hta_vehicle_point;
typedef struct {
    uint32_t tag_id, model_id, placement;
    float forward, reverse, accel, decel, turn_left, turn_right, turn_rate;
    float circumference, gravity_scale;
    float seat[3], pos[3], yaw, pitch, roll, speed, steering, fall_speed;
    float wheel_spin;
    hta_vehicle_point points[HTA_VEHICLE_MASS_POINTS];
    uint32_t point_count;
    float wheelbase, body_radius;
    uint32_t first_vertex, vertex_count, first_coll, coll_count;
    bool grounded;
} hta_vehicle;
typedef struct {
    hta_vehicle cars[HTA_VEHICLE_MAX];
    uint32_t count;
    uint8_t skip[HTA_VEHICLE_PLACEMENTS];
    int32_t driver;
    hta_bsp_mesh mesh, coll_mesh;
    hta_vertex *rest, *coll_rest;
    hta_collision collision;
    float look_yaw, look_pitch;
    uint32_t upload_frames;
    bool loaded;
} hta_vehicles;

bool hta_vehicle_read(hta_vehicle *v, const hta_cache *c, uint32_t tag);
bool hta_vehicles_load(hta_vehicles *v, const hta_cache *c,
    const hta_resource_map *bm, char *err, size_t n);
void hta_vehicles_free(hta_vehicles *v);
void hta_vehicles_pose(hta_vehicles *v);
int32_t hta_vehicles_near(const hta_vehicles *v, const hta_collision *world,
    const float feet[3]);
bool hta_vehicles_enter(hta_vehicles *v, int32_t car);
bool hta_vehicles_exit(hta_vehicles *v, const hta_collision *world, hta_player *p,
    hta_camera *cam);
void hta_vehicles_update(hta_vehicles *v, const hta_collision *world,
    float throttle, float steer, bool brake, float gravity, float dt);
void hta_vehicles_camera(hta_vehicles *v, const hta_collision *world,
    hta_player *p, hta_camera *cam, float dyaw, float dpitch);
#endif
