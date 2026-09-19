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

typedef struct {
    /* uniform grid over XY; each cell lists triangle indices */
    float    min[2], cell;
    uint32_t nx, ny;
    uint32_t *cell_start;   /* nx*ny + 1 */
    uint32_t *tri_index;    /* flattened */
    const hta_vertex *verts;
    const uint32_t   *indices;
    uint32_t          tri_count;
    float             walkable_nz; /* cos(max slope); 0.5 ≈ 60° */
    bool built;
} hta_collision;

bool hta_collision_build(hta_collision *c, const hta_bsp_mesh *mesh);
void hta_collision_free(hta_collision *c);
/* Call after the mesh vertex/index arrays have been realloc'd (e.g. scenery
 * append). The grid still refers to the original triangle range. */
void hta_collision_rebind(hta_collision *c, const hta_vertex *verts,
                          const uint32_t *indices);

/* Highest triangle surface at or below (x,y,z_from). Returns false if none. */
bool hta_collision_ground(const hta_collision *c, float x, float y, float z_from,
                          float *out_z);

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
} hta_player;

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
