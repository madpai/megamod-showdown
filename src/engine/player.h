/* First-person pawn driven by Trial tag physics (globals + cyborg_mp).
 * Ground is a height query on the collision mesh (structure BSP plus
 * scenery/vehicle coll tags; render mesh fallback). Walls use a
 * standing-cylinder depenetration pass.
 */
#ifndef HTA_PLAYER_H
#define HTA_PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include "camera.h"
#include "../asset/bsp.h"
#include "../asset/biped.h"

struct hta_collision;
/* A rigid collision grid placed in the world: a vehicle's own `coll`
 * geometry, built ONCE in model space and queried through its pose. Moving
 * one costs a matrix, not a grid rebuild. `rot` is row-major local->world,
 * orthonormal. `radius` bounds the grid around `pos` for cheap culling. */
typedef struct hta_collision_instance {
    const struct hta_collision *grid;
    float rot[9];
    float pos[3];
    float radius;
    bool  active;
} hta_collision_instance;

typedef struct hta_collision {
    /* uniform grid over XY; each cell lists triangle indices */
    float    min[2], cell;
    uint32_t nx, ny;
    uint32_t *cell_start;   /* nx*ny + 1 */
    uint32_t *tri_index;    /* flattened */
    const hta_vertex *verts;
    const uint32_t   *indices;
    const uint8_t    *tri_material;   /* may be NULL */
    uint32_t          tri_count;
    float             walkable_nz; /* cos(max slope); 0.5 ≈ 60° */
    bool built;
    /* Optional independently rebuilt moving-object grid; borrowed, acyclic.
     * hta_collision_build memsets this struct, so re-point `extra` AFTER any
     * rebuild of the grid that carries it, or the link silently vanishes. */
    const struct hta_collision *extra;
    /* Optional placed rigid grids (vehicles); borrowed. Same caveat as
     * `extra`: re-point after any hta_collision_build of this grid. */
    const hta_collision_instance *instances;
    uint32_t instance_count;
} hta_collision;

bool hta_collision_build(hta_collision *c, const hta_bsp_mesh *mesh);
/* The same with `across` cells along the longer side (at most 256). An
 * imported map packs dense prop models into a small area: the default 64
 * leaves hundreds of triangles in a cell there. */
bool hta_collision_build_cells(hta_collision *c, const hta_bsp_mesh *mesh, uint32_t across);
#define HTA_COLLISION_CELLS_IMPORTED 256u

/* Point the grid at the biped tag's own slope limit. Without this the grid
 * keeps its 60-degree default, so a host test simulates different physics
 * than the device -- which is how the base-doorway headroom bug hid from the
 * suite. Call it right after building, wherever real tag physics exist. */
void hta_collision_set_slope(hta_collision *c, float max_slope_radians);
void hta_collision_free(hta_collision *c);
/* Call after the mesh vertex/index arrays have been realloc'd (e.g. scenery
 * append). The grid still refers to the original triangle range. */
void hta_collision_rebind(hta_collision *c, const hta_vertex *verts,
                          const uint32_t *indices);
void hta_collision_rebind_material(hta_collision *c, const uint8_t *tri_material);

/* Highest triangle surface at or below (x,y,z_from). Returns false if none. */
bool hta_collision_ground(const hta_collision *c, float x, float y, float z_from,
                          float *out_z);

/* Halo's MaterialType of the ground there, or HTA_MATERIAL_NONE. What you
 * are standing on is what decides which footstep plays. */
uint8_t hta_collision_ground_material(const hta_collision *c,
                                      float x, float y, float z_from);

/* Push a standing pill (radius, [z_feet, z_feet+height]) out of steep faces.
 * Floors are ignored — those stay a ground snap. */
void hta_collision_depenetrate(const hta_collision *c,
                               float *x, float *y, float z_feet,
                               float height, float radius);

/* First-hit ray vs the collision mesh. `dir` need not be unit length.
 * Writes hit point, surface normal, and t along dir. */
bool hta_collision_ray(const hta_collision *c,
                       const float orig[3], const float dir[3], float max_t,
                       float *out_t, float hit[3], float nrm[3]);

/* As above, and also what the surface is made of. A bullet sounds different
 * hitting sand and hitting a base wall, and the projectile tag says how. */
bool hta_collision_ray_material(const hta_collision *c,
                                const float orig[3], const float dir[3], float max_t,
                                float *out_t, float hit[3], float nrm[3],
                                uint8_t *out_material);

typedef struct {
    float pos[3];        /* feet position */
    float velocity[3];
    bool  on_ground;
    float eye_height;    /* current, lerped stand/crouch */
    float crouch_t;      /* 0 stand .. 1 crouch */
    float radius;
    float walk_speed;    /* alias of phys.run_forward for older call sites */
    float jump_speed;
    float gravity;
    bool  noclip;
    hta_player_physics phys;

    /* Zoom magnification, 1.0 unzoomed. The player owns the field of view
     * -- hta_player_update writes it every frame -- so a scope has to go
     * through here or it is clobbered on the next update. */
    float zoom;

    /* Footsteps are paced by distance, not by a timer, so they slow down
     * when you do and stop when you stop. `footstep` is true for the one
     * update in which a foot lands. */
    float step_distance;
    bool  footstep;
    bool  landed;        /* true for the update a fall ends */
    float land_speed;    /* how fast you were falling when it did, wu/s */
} hta_player;

/* Sets the scope magnification. 1.0 (or anything below it) is unzoomed. */
void hta_player_set_zoom(hta_player *p, float magnification);

/* World units between footfalls. The Trial's cyborg runs at 2.25 wu/s, so
 * this is a little under three steps a second at a full run. */
#define HTA_STEP_LENGTH 0.80f

typedef struct {
    float move_forward;  /* -1 .. 1 */
    float move_right;    /* -1 .. 1 */
    float look_yaw;      /* radians this frame */
    float look_pitch;
    bool  jump;
    bool  fire;
    bool  crouch;
} hta_player_input;

void hta_player_init(hta_player *p);
void hta_player_apply_physics(hta_player *p, const hta_player_physics *phys);
void hta_player_spawn(hta_player *p, const hta_spawn_point *sp);
/* Advances the player and writes the resulting eye position into `cam`. */
void hta_player_update(hta_player *p, hta_camera *cam, const hta_collision *col,
                       const hta_player_input *in, float dt);

#endif
