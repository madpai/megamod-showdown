/* Simplest useful first-person controller: move, look, jump, gravity, and
 * collision against the world mesh.
 *
 * This is NOT Halo's movement system. It is the minimum that lets you walk
 * around Blood Gulch and judge whether the geometry is right. Halo's actual
 * physics (acceleration curves, crouch, vehicle interaction) is out of scope
 * for Phase 2.
 *
 * Collision uses a downward ray against the rendered triangle mesh, accelerated
 * by a uniform XY grid. The real collision BSP is deferred.
 */
#ifndef HTA_PLAYER_H
#define HTA_PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include "camera.h"
#include "../asset/bsp.h"

typedef struct {
    /* uniform grid over XY; each cell lists triangle indices */
    float    min[2], cell;
    uint32_t nx, ny;
    uint32_t *cell_start;   /* nx*ny + 1 */
    uint32_t *tri_index;    /* flattened */
    const hta_vertex *verts;
    const uint32_t   *indices;
    uint32_t          tri_count;
    bool built;
} hta_collision;

bool hta_collision_build(hta_collision *c, const hta_bsp_mesh *mesh);
void hta_collision_free(hta_collision *c);

/* Highest triangle surface at or below (x,y,z_from). Returns false if none. */
bool hta_collision_ground(const hta_collision *c, float x, float y, float z_from,
                          float *out_z);

typedef struct {
    float pos[3];        /* feet position */
    float velocity[3];
    bool  on_ground;
    float eye_height;
    float radius;
    float walk_speed;
    float jump_speed;
    float gravity;
    bool  noclip;        /* free-fly, for inspecting geometry */
} hta_player;

typedef struct {
    float move_forward;  /* -1 .. 1 */
    float move_right;    /* -1 .. 1 */
    float look_yaw;      /* radians this frame */
    float look_pitch;
    bool  jump;
} hta_player_input;

void hta_player_init(hta_player *p);
void hta_player_spawn(hta_player *p, const hta_spawn_point *sp);
/* Advances the player and writes the resulting eye position into `cam`. */
void hta_player_update(hta_player *p, hta_camera *cam, const hta_collision *col,
                       const hta_player_input *in, float dt);

#endif
