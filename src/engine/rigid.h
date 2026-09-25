/* Rigid bodies for debris: chunks of a broken prop, gibs, a wrecked hull's
 * panels, rocks thrown by a blast. Portable and game-agnostic.
 *
 * Boxes and spheres, colliding with the world's triangle grid (the same
 * hta_collision the players walk on, including its moving `extra` grid and
 * placed vehicle instances) and loosely with each other. Contacts are
 * solved with sequential impulses -- restitution, Coulomb friction, and
 * torque from where the contact is, so a plank lands on a corner, tips,
 * and settles flat. Resting bodies fall asleep and cost nothing.
 *
 * This is COSMETIC physics. Nothing here is replicated: each device runs
 * its own debris from the same events, which is what every shooter does
 * for gibs and rubble, and why nothing may hang gameplay on where a chunk
 * ends up. Bodies are a fixed pool; spawning into a full pool recycles the
 * oldest.
 *
 * Units are the caller's. Defaults assume Halo's: 1 world unit = 3.048 m,
 * so gravity is 9.8 / 3.048 = 3.215 wu/s^2. */
#ifndef HTA_RIGID_H
#define HTA_RIGID_H

#include <stdbool.h>
#include <stdint.h>
#include "player.h"

typedef enum { HTA_RIGID_SPHERE = 0, HTA_RIGID_BOX = 1 } hta_rigid_shape;

/* What a body is made of: picks its bounce, friction, density and, for the
 * caller, its sound, texture and whether it leaves blood. */
typedef enum {
    HTA_RMAT_WOOD = 0,
    HTA_RMAT_METAL,
    HTA_RMAT_CONCRETE,
    HTA_RMAT_GLASS,
    HTA_RMAT_FLESH,
    HTA_RMAT_DIRT,
    HTA_RMAT_COUNT
} hta_rigid_material;

typedef struct {
    bool     active, asleep;
    uint8_t  shape;          /* hta_rigid_shape */
    uint8_t  material;       /* hta_rigid_material */
    float    half[3];        /* box half extents; a sphere's radius is half[0] */
    float    pos[3], vel[3];
    float    rot[4];         /* unit quaternion w, x, y, z: body -> world */
    float    ang[3];         /* angular velocity, world space, rad/s */
    float    inv_mass;
    float    inv_inertia[3]; /* body-space diagonal */
    float    restitution, friction;
    float    age, life;      /* life 0 = until recycled */
    float    fade;           /* seconds to fade out after `life` */
    float    still_for;      /* seconds nearly at rest, toward sleep */
    uint32_t user;           /* the caller's: which gib, which prop */
    uint32_t generation;     /* bumps on every reuse of the slot */
} hta_rigid_body;

/* A hard hit this step: where to thud, splash blood, chip paint. */
typedef struct {
    uint32_t body;
    float    point[3], normal[3];
    float    speed;          /* closing speed along the normal, wu/s */
    uint8_t  material;
} hta_rigid_impact;

#define HTA_RIGID_MAX_IMPACTS 64u

typedef struct {
    hta_rigid_body *bodies;
    uint32_t cap;
    const hta_collision *world;   /* borrowed; may be NULL (then only a floor) */
    float    gravity;             /* wu/s^2, down -Z; default 3.215 */
    float    floor_z;             /* used when world is NULL; -1e9 disables */
    float    linear_drag;         /* per second; default 0.05 */
    float    angular_drag;        /* per second; default 0.1 */
    float    impact_min;          /* report impacts faster than this; default 0.6 */
    bool     collide_bodies;      /* bodies push each other apart */
    hta_rigid_impact impacts[HTA_RIGID_MAX_IMPACTS];
    uint32_t impact_count;        /* this step's; cleared by each step */
    uint32_t rng;
    uint32_t awake;               /* diagnostics after a step */
    uint32_t tri_tests;
} hta_rigid_world;

typedef struct {
    hta_rigid_shape shape;
    hta_rigid_material material;
    float half[3];                /* sphere: half[0] only */
    float pos[3], vel[3], ang[3];
    float rot[4];                 /* 0,0,0,0 means identity */
    float density;                /* 0 = the material's */
    float life, fade;
    uint32_t user;
} hta_rigid_desc;

bool hta_rigid_init(hta_rigid_world *w, uint32_t capacity, const hta_collision *world);
void hta_rigid_free(hta_rigid_world *w);
/* Change the pool size (a video setting). Keeps the newest bodies. */
bool hta_rigid_resize(hta_rigid_world *w, uint32_t capacity);
void hta_rigid_clear(hta_rigid_world *w);

/* Index of the new body, recycling the oldest when full. */
uint32_t hta_rigid_spawn(hta_rigid_world *w, const hta_rigid_desc *d);
void     hta_rigid_remove(hta_rigid_world *w, uint32_t i);

/* Advance by dt seconds (substepped internally; call once per frame). */
void hta_rigid_step(hta_rigid_world *w, float dt);

/* A blast: bodies inside `radius` are thrown away from `centre` with up to
 * `speed` wu/s (falling off linearly), and set spinning. */
void hta_rigid_blast(hta_rigid_world *w, const float centre[3], float radius, float speed);
/* A push at one point of one body (a bullet): dv = impulse * inv_mass. */
void hta_rigid_impulse(hta_rigid_world *w, uint32_t i, const float point[3], const float impulse[3]);

/* Column-major rigid model matrix (the renderer's instance format). */
void  hta_rigid_matrix(const hta_rigid_body *b, float out[16]);
/* 1 while alive, falling to 0 over its fade. */
float hta_rigid_alpha(const hta_rigid_body *b);
uint32_t hta_rigid_active(const hta_rigid_world *w);

/* Per-material defaults: density (per cubic wu), restitution, friction. */
void hta_rigid_material_props(hta_rigid_material m, float *density, float *restitution, float *friction);

/* Contacts of a sphere against a collision grid (and its extra grid and
 * instances): up to `max` deepest, each a point on the surface, the normal
 * out of it, and the penetration. Exposed for tests and other movers. */
typedef struct { float point[3], normal[3], depth; } hta_contact;
uint32_t hta_collision_sphere(const hta_collision *c, const float centre[3], float radius,
                              hta_contact *out, uint32_t max);

#endif
