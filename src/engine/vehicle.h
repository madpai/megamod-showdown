/* Every vehicle a Trial map places, from its own tags.
 *
 * A vehicle TYPE is one palette entry: the `vehi` tag's driving floats and
 * seats, its `phys` mass points, its model split into rigid parts by node,
 * and its `coll` geometry built once into a model-space grid. A vehicle CAR
 * is one placement: where it is, how it moves, who sits in it and where its
 * gun points.
 *
 * Halo's vehicle types (`vehi+756`) each move their own way:
 *   0 human tank     -- the Scorpion: treads, pivots in place, turret aims
 *   1 human jeep     -- the Warthogs: steered front wheels, powered rear
 *   4 alien scout    -- the Ghost: hovers, strafes, turns toward the aim
 *   5 alien fighter  -- the Banshee: flies where the pilot looks
 *   6 turret         -- stationary; aims only
 * Speeds, accelerations, turn limits, seats, markers and mass points are
 * the tags'. What the tags leave at zero for a kind the engine has to
 * choose, and every such choice is a named constant below and in the
 * HANDOFF ledger.
 *
 * Collision against the world is by the `phys` mass points, as spheres
 * (planar pushes plus swept rays), and against other vehicles sphere to
 * sphere with a 2-D rigid-body impulse. There is no full 3-D rigid body:
 * a vehicle does not flip.
 *
 * Other things collide with a vehicle through `instances`: one
 * hta_collision_instance per car, pointing at its type's model-space grid.
 *
 * Portable: no renderer. The platform draws `hta_vehicles_parts`.
 */
#ifndef HTA_VEHICLE_H
#define HTA_VEHICLE_H
#include "player.h"
#include "../asset/model.h"
#include "../asset/anim.h"

#define HTA_VEHICLE_MAX 32u
#define HTA_VEHICLE_TYPES 8u
#define HTA_VEHICLE_PLACEMENTS 512u
#define HTA_VEHICLE_MASS_POINTS 32u
#define HTA_VEHICLE_SEATS 6u
#define HTA_VEHICLE_NODES 32u
#define HTA_VEHICLE_NONE (-1)

/* `vehi+756`, the tag's own enumeration. */
enum {
    HTA_VK_TANK = 0, HTA_VK_JEEP = 1, HTA_VK_BOAT = 2, HTA_VK_PLANE = 3,
    HTA_VK_SCOUT = 4, HTA_VK_FIGHTER = 5, HTA_VK_TURRET = 6
};

/* UnitSeatFlags, bit by bit as Invader lists them (`locked` included). */
#define HTA_SEAT_INVISIBLE       (1u << 0)
#define HTA_SEAT_LOCKED          (1u << 1)
#define HTA_SEAT_DRIVER          (1u << 2)
#define HTA_SEAT_GUNNER          (1u << 3)
#define HTA_SEAT_THIRD_PERSON    (1u << 4)
#define HTA_SEAT_ALLOWS_WEAPONS  (1u << 5)
#define HTA_SEAT_THIRD_ON_ENTER  (1u << 6)
#define HTA_SEAT_SLAVED_TO_GUN   (1u << 7)
#define HTA_SEAT_NEEDS_DRIVER    (1u << 9)

/* Engine choices, not tag fields. */
#define HTA_VEHICLE_ENTER_REACH 0.9f
#define HTA_VEHICLE_STEP (1.0f / 120.0f)
#define HTA_VEHICLE_CAMERA_BACK 2.5f
#define HTA_VEHICLE_CAMERA_UP 0.7f
#define HTA_VEHICLE_CLEARANCE 0.04f
#define HTA_VEHICLE_MAX_SLOPE 0.75f
#define HTA_VEHICLE_EXIT_SPEED 0.5f
#define HTA_VEHICLE_ADHESION_SPEED_FRACTION 0.25f
#define HTA_VEHICLE_SETTLE_TIME 0.5f
/* No collision restitution or yaw damping is carried by the Trial tags. */
#define HTA_VEHICLE_RESTITUTION 0.20f
#define HTA_VEHICLE_YAW_DAMP 2.0f
/* The Ghost's tag has a forward speed and nothing for reverse or slide.
 * Halo's Ghost backs up and strafes; these fractions of forward are ours. */
#define HTA_SCOUT_REVERSE_FRACTION 0.5f
#define HTA_SCOUT_STRAFE_FRACTION 0.75f
/* The Banshee's tag has no slide either. */
#define HTA_FIGHTER_STRAFE_FRACTION 0.5f
/* How far the Banshee's nose follows the pilot's look, up or down. Ours. */
#define HTA_FIGHTER_MAX_PITCH 1.0f
/* How much a Banshee banks into a turn, per rad/s of yaw rate. Ours. */
#define HTA_FIGHTER_BANK 0.35f
/* An empty vehicle away from where the map put it goes home after this
 * long. A gametype setting in Halo; not in any map. Ours. */
#define HTA_VEHICLE_RESPAWN 60.0f
/* Momentum. The tags' `deceleration` is how hard a vehicle BRAKES (the
 * Warthog's 9.9 wu/s^2 stops it from full speed in under a second, and the
 * Banshee's 14.4 in half of one); let go of the stick and Halo's vehicles
 * roll and glide on. Coasting slows at this fraction of it. Ours. */
#define HTA_VEHICLE_COAST_JEEP    0.15f
#define HTA_VEHICLE_COAST_TANK    0.45f
#define HTA_VEHICLE_COAST_HOVER   0.30f
#define HTA_VEHICLE_COAST_FLYER   0.20f
/* A Ghost and a Banshee slide: the hull turns under the velocity, and
 * velocity across the hull is corrected separately from velocity along it,
 * at this fraction of the acceleration -- so a hard turn at speed carries
 * you wide before it bites. Ours. */
#define HTA_VEHICLE_DRIFT_GRIP    1.0f
/* Suspension: the body pitches under acceleration and rolls out of a turn,
 * radians per wu/s^2, on a spring of this stiffness and damping. Ours. */
#define HTA_VEHICLE_SWAY_JEEP     0.018f
#define HTA_VEHICLE_SWAY_TANK     0.008f
#define HTA_VEHICLE_SWAY_HOVER    0.014f
#define HTA_VEHICLE_SWAY_MAX      0.14f
#define HTA_VEHICLE_SWAY_SPRING   45.0f
#define HTA_VEHICLE_SWAY_DAMP     7.0f
/* How fast the Warthog's chaingun barrels spin while firing, rad/s. Ours. */
#define HTA_VEHICLE_BARREL_SPIN 30.0f

typedef struct {
    float pos[3], radius, visual_radius, travel;
    uint16_t node;
    bool wheel;          /* a powered point: supports the chassis */
} hta_vehicle_point;

typedef struct {
    uint32_t flags;
    char     label[32];        /* the seat's animation prefix: "W-driver" */
    int16_t  node;             /* the marker's node: a gunner turns with the gun */
    float    pos[3];           /* node-local, then model space via the node */
    float    rot[4];           /* marker rotation, conjugated to our sense */
    float    enter[3];         /* model space: where you stand to get in */
    int16_t  camera_node;      /* -1: chase camera */
    float    camera[3];        /* node-local */
    float    yaw_rate, pitch_rate;   /* rad/s */
    float    pitch_min, pitch_max, yaw_min, yaw_max;
    int16_t  hud_text;         /* index into the HUD message text */
} hta_vehicle_seat;

typedef struct {
    uint32_t tag_id, model_id, weapon_tag, anim_id;
    uint16_t kind;
    char     name[40];
    hta_vehicle_seat seats[HTA_VEHICLE_SEATS];
    uint32_t seat_count;
    float    rider_damage;     /* unit+388: share of a blast its riders take */
    int16_t  hud_name;         /* object+316: its name in hud_icon_messages */
    float    fixed_gun_pitch;  /* vehi+868 */
    /* Model nodes: parent and model-space pivot. */
    uint32_t node_count;
    int16_t  node_parent[HTA_VEHICLE_NODES];
    float    node_pivot[HTA_VEHICLE_NODES][3];
    hta_transform node_rest[HTA_VEHICLE_NODES];
    int16_t  yaw_node, pitch_node, barrel_node;
    int16_t  trigger_node[2];
    float    trigger[2][3];    /* node-local primary / secondary trigger */
    bool     has_trigger[2];
    /* Rendering: this type's model once, in model space, its submeshes
     * grouped by the node that moves them. `part_node[p]` is the node
     * whose motion carries part p. */
    hta_bsp_mesh mesh;
    uint32_t part_count;
    int16_t  part_node[HTA_VEHICLE_NODES];
    uint32_t part_first[HTA_VEHICLE_NODES], part_submeshes[HTA_VEHICLE_NODES];
    /* Collision, once, in model space. */
    hta_bsp_mesh coll_mesh;
    hta_collision coll;
    float    coll_radius;
    bool     loaded;
} hta_vehicle_type;

/* What the unit in the driver's seat asks for. */
typedef struct {
    float throttle;   /* -1 .. 1, forward */
    float strafe;     /* -1 .. 1, right: the Warthog's and tank's steering */
    float yaw, pitch; /* where the driver looks, world radians */
    bool  brake;
    bool  driven;     /* somebody is in the driver's seat */
} hta_vehicle_control;

typedef struct {
    uint32_t tag_id, model_id, placement;
    uint16_t type, kind;
    uint8_t  team;              /* scenario +88 */
    uint16_t spawn_flags;       /* scenario +90 */
    bool     active;            /* spawned in this game */
    float home_pos[3], home_yaw, home_pitch, home_roll;
    float forward, reverse, accel, decel, turn_left, turn_right, turn_rate;
    float circumference, gravity_scale, ground_depth, ground_friction, mass, yaw_inertia;
    float center_of_mass[3];
    float seat[3], pos[3], yaw, pitch, roll, speed, steering, fall_speed, rise_speed;
    float rest_time, lateral_vel[2], yaw_rate;
    float wheel_spin, wheel_speed;
    /* The turret, relative to the hull: yaw about its node's up, pitch up. */
    float aim_yaw, aim_pitch, barrel_spin, barrel_speed;
    float aim_world[2];         /* what the gunner wants, world yaw/pitch */
    bool  aiming;
    float bank;                 /* the Banshee's visual roll into a turn */
    float sway[2], sway_vel[2]; /* suspension pitch and roll, and their rates */
    float tumble[2];            /* pitch and roll rates while thrown in the air */
    float idle;                 /* seconds empty and away from home */
    float blocked;              /* seconds, ever, a driven move was refused: a
                                 * driver compares it across a second to tell
                                 * a jam from a slow turn */
    hta_vehicle_control ctl;
    int8_t occupant[HTA_VEHICLE_SEATS];   /* unit index, -1 empty */
    hta_vehicle_point points[HTA_VEHICLE_MASS_POINTS];
    uint32_t point_count;
    float wheelbase, body_radius;
    bool grounded, traction;
} hta_vehicle;

typedef struct {
    hta_vehicle_type types[HTA_VEHICLE_TYPES];
    uint32_t type_count;
    hta_vehicle cars[HTA_VEHICLE_MAX];
    uint32_t count;
    uint8_t skip[HTA_VEHICLE_PLACEMENTS];
    /* One per car, for the world grid's `instances`. */
    hta_collision_instance inst[HTA_VEHICLE_MAX];
    bool loaded;
} hta_vehicles;

/* A rigid piece of one car to draw this frame. */
typedef struct {
    uint16_t type;
    uint32_t first_submesh, submesh_count;
    float    model[16];   /* column-major, model space to world */
} hta_vehicle_part;

/* Which placements a game spawns. `hta_vehicles_roster`. */
enum {
    HTA_VROSTER_NONE = 0,      /* no vehicles */
    HTA_VROSTER_DEFAULT,       /* the map's own "slayer default" placements */
    HTA_VROSTER_ALL,           /* every placement the map allows in slayer */
    HTA_VROSTER_WARTHOGS, HTA_VROSTER_GHOSTS, HTA_VROSTER_SCORPIONS,
    HTA_VROSTER_ROCKET_WARTHOGS, HTA_VROSTER_BANSHEES,
    HTA_VROSTER_COUNT
};

/* Reads one car's physics from its `vehi` tag. False for a tag this engine
 * cannot move (a boat, a plane) or that is malformed. */
bool hta_vehicle_read(hta_vehicle *v, const hta_cache *c, uint32_t tag);
bool hta_vehicles_load(hta_vehicles *v, const hta_cache *c,
    const hta_resource_map *bm, char *err, size_t n);
void hta_vehicles_free(hta_vehicles *v);

/* Spawn the placements `roster` picks (HTA_VROSTER_*); the rest are gone.
 * Every car goes home, empty. */
void hta_vehicles_roster(hta_vehicles *v, int roster);
/* One car back to where the map put it. */
void hta_vehicles_reset(hta_vehicles *v, uint32_t car);

/* Seats. */
const hta_vehicle_seat *hta_vehicles_seat(const hta_vehicles *v, uint32_t car, uint32_t seat);
uint32_t hta_vehicles_seat_count(const hta_vehicles *v, uint32_t car);
int32_t  hta_vehicles_driver_seat(const hta_vehicles *v, uint32_t car);
int32_t  hta_vehicles_gunner_seat(const hta_vehicles *v, uint32_t car);
/* The nearest free seat whose door is within reach of `feet`, or -1.
 * A seat that needs a driver is only offered while one is in. */
int32_t hta_vehicles_near(const hta_vehicles *v, const hta_collision *world,
    const float feet[3], int32_t *out_seat);
/* Sit `unit` down. False when the seat is taken or the car is going fast. */
bool hta_vehicles_enter(hta_vehicles *v, uint32_t car, uint32_t seat, int32_t unit);
/* Where `unit` gets out: safe ground beside its seat. False while unsafe
 * (moving, no room). Frees the seat on success. */
bool hta_vehicles_exit(hta_vehicles *v, const hta_collision *world, uint32_t car,
    uint32_t seat, float radius, float height, float out_feet[3], float *out_yaw);
/* Free the seat without a safe spot: death, disconnect, a reset. */
void hta_vehicles_vacate(hta_vehicles *v, int32_t unit);
/* Where a unit sits: `out_root` is its body's root (the seat marker in the
 * world). The body's feet are at root.t. */
bool hta_vehicles_seat_transform(const hta_vehicles *v, uint32_t car, uint32_t seat,
    hta_transform *out_root);
/* The car's model-space node poses this frame (turret, wheels). */
void hta_vehicles_nodes(const hta_vehicles *v, uint32_t car,
    hta_transform out[HTA_VEHICLE_NODES]);
/* The car's pose, model space to world. */
void hta_vehicles_world(const hta_vehicles *v, uint32_t car, hta_transform *out);
/* A trigger marker in the world and the way it points (0 primary, 1
 * secondary). Falls back to the primary and then to the hull. */
bool hta_vehicles_trigger(const hta_vehicles *v, uint32_t car, uint32_t trigger,
    float out_pos[3], float out_dir[3]);

/* One physics step for every active car, each by its own `ctl`. */
void hta_vehicles_update(hta_vehicles *v, const hta_collision *world,
    float gravity, float dt);
/* Throw a car: `vel` wu/s added to its motion (up lifts it off the
 * ground), `spin` rad/s of tumble while it is in the air, `kick` radians
 * of suspension jolt along the push. A blast, a cannon's recoil. */
void hta_vehicles_push(hta_vehicles *v, uint32_t car, const float vel[3], float spin,
                       float kick);
/* Aim a car's turret toward a world yaw/pitch, within its seat's range. */
void hta_vehicles_aim(hta_vehicles *v, uint32_t car, float yaw, float pitch, float dt);
/* Keep `instances` matching the cars' poses. Cheap: matrices only. */
void hta_vehicles_sync(hta_vehicles *v);

/* The camera for `seat`: a chase camera behind the hull, or the seat's own
 * camera marker. `yaw`/`pitch` are the look, world radians. */
void hta_vehicles_camera(const hta_vehicles *v, const hta_collision *world,
    uint32_t car, uint32_t seat, float yaw, float pitch, hta_camera *cam);
/* Does this seat look through the chase camera? */
bool hta_vehicles_third_person(const hta_vehicles *v, uint32_t car, uint32_t seat);
/* How fast a car is going, wu/s. */
float hta_vehicles_speed(const hta_vehicles *v, uint32_t car);

/* The rigid pieces to draw this frame. Returns how many were written. */
uint32_t hta_vehicles_parts(const hta_vehicles *v, hta_vehicle_part *out, uint32_t max);

/* Radians wrapped into -pi..pi. */
float hta_angle_wrap(float a);
#endif
