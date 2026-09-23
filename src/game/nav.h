/* Where a bot can walk, and how to get there.
 *
 * Halo CE's multiplayer maps ship no AI pathfinding data: the campaign's
 * `ai` firing positions and the structure BSP's pathfinding surfaces exist
 * for the Covenant, and the Trial's Blood Gulch leaves both empty. So the
 * walkable surface is rebuilt here from the same collision mesh the player
 * already walks on.
 *
 * A regular grid over XY, and in each column every floor the ground query
 * finds on the way down -- so the room under a base roof and the roof itself
 * are separate nodes in the same column. A node exists where the biped fits:
 * its cylinder, from the tag, stands there without the wall push moving it
 * and without a ceiling in its head. Two neighbouring nodes are linked when
 * the rise between them is a step or a walkable slope (the biped's own
 * `maximum slope angle`) and nothing at knee height is in the way. A drop is
 * linked one way, as far as a fall that does not hurt.
 *
 * Portable: collision mesh in, grid out. No renderer, no platform.
 */
#ifndef HTA_NAV_H
#define HTA_NAV_H

#include <stdbool.h>
#include <stdint.h>
#include "../engine/player.h"

/* Grid spacing, in world units. Ours: one biped radius (0.175 wu) would
 * give two cells a body and a million nodes; a whole body width gives a
 * base doorway three cells and the whole of Blood Gulch about 150,000. */
#define HTA_NAV_CELL 0.35f
#define HTA_NAV_MAX_LAYERS 6u
#define HTA_NAV_NONE 0xFFFFFFFFu

/* Clearance is counted this far and no further. */
#define HTA_NAV_CLEAR_MAX 8u

/* Node flags. */
#define HTA_NAV_NEAR_WALL 0x01u   /* the wall push touched it; costs more */

typedef struct {
    float    z;
    uint32_t link[8];    /* neighbour node per direction, or HTA_NAV_NONE */
    uint16_t cx, cy;
    uint8_t  flags;
    uint8_t  region;     /* connected component, 0 for unreachable islands */
    uint8_t  clear;      /* cells to the nearest place a car cannot go, up to
                          * HTA_NAV_CLEAR_MAX: see hta_nav_car_clear */
} hta_nav_node;

typedef struct {
    float     min[2];
    float     cell;
    uint32_t  nx, ny;
    uint32_t *col_start;     /* nx*ny + 1 */
    hta_nav_node *nodes;
    uint32_t  node_count;
    uint32_t  link_count;
    uint8_t   main_region;   /* the largest component: "the map" */

    /* A* scratch, reused between searches. */
    float    *g, *f;
    uint32_t *parent;
    uint32_t *heap;
    uint32_t *stamp;         /* search generation a node was touched in */
    uint32_t  generation;
    bool      built;
} hta_nav;

typedef struct {
    float    radius;        /* the biped's cylinder */
    float    height;        /* standing collision height */
    float    max_slope;     /* radians */
    float    max_drop;      /* world units; a fall further is not a path */
} hta_nav_params;

/* Build over the collision mesh's bounds. `bmin`/`bmax` are the mesh
 * bounds (the render BSP's are fine). */
bool hta_nav_build(hta_nav *n, const hta_collision *col,
                   const float bmin[3], const float bmax[3],
                   const hta_nav_params *p, char *err, size_t errlen);
void hta_nav_free(hta_nav *n);

/* The node a biped standing at `feet` is on, or the nearest one within
 * `reach` world units horizontally. HTA_NAV_NONE if there is none. */
uint32_t hta_nav_nearest(const hta_nav *n, const float feet[3], float reach);

/* World position of a node's floor. */
void hta_nav_pos(const hta_nav *n, uint32_t node, float out[3]);

/* A* from one node to another. Writes up to `max` node indices, start
 * first, into `out` and returns how many; 0 if unreachable or if the search
 * ran past `budget` expansions. */
uint32_t hta_nav_path(hta_nav *n, uint32_t from, uint32_t to,
                      uint32_t *out, uint32_t max, uint32_t budget);

/* Every node's way to one goal, worked out once: `next[i]` is the neighbour
 * to step to from node i, HTA_NAV_NONE where the goal cannot be reached (or
 * at the goal itself). For a goal that never moves -- a flag's stand -- this
 * replaces a search per walker, which from the back of a base can outrun
 * any budget small enough to replan with. `next` holds node_count entries.
 * Returns how many nodes can reach the goal. */
uint32_t hta_nav_field(hta_nav *n, uint32_t goal, uint32_t *next);

/* Read a path to the field's goal out of it: up to `max` nodes from `from`,
 * start first. A single node means `from` is the goal -- or cannot reach
 * it, which the caller tells apart by knowing the goal. */
uint32_t hta_nav_field_path(const hta_nav *n, const uint32_t *next, uint32_t from,
                            uint32_t *out, uint32_t max);

/* Can a biped walk straight from node a to node b over nav nodes? Used to
 * skip the staircase a grid path makes across open ground. */
bool hta_nav_straight(const hta_nav *n, uint32_t a, uint32_t b);

/* Pull a grid path taut: keep only the corners. Returns the new length. */
uint32_t hta_nav_smooth(const hta_nav *n, uint32_t *path, uint32_t len);

/* ---- For cars. ----
 * The grid is a biped's: it runs a body width from walls, up the steps of
 * the bases and over hillsides a vehicle's physics refuses. Every node also
 * carries `clear`, how many cells lie between it and the nearest node that
 * is not open ground for a car (hugging a wall, or at the edge of the grid,
 * or with a neighbour steeper than HTA_VEHICLE_MAX_SLOPE). A car of a given
 * radius keeps to nodes with at least hta_nav_car_clear(radius). */
uint8_t hta_nav_car_clear(float radius);

/* The walkers' calls again, restricted to nodes with `clear` >= `clear`
 * (0 is the walkers' own). A path also pays for running near its limit, so
 * it keeps to the middle of the open ground it has, and may cross
 * narrower ground only near its two ends -- dearly -- to get a car out
 * from between the posts it was parked among. */
uint32_t hta_nav_nearest_wide(const hta_nav *n, const float at[3], float reach, uint8_t clear);
uint32_t hta_nav_path_wide(hta_nav *n, uint32_t from, uint32_t to,
                           uint32_t *out, uint32_t max, uint32_t budget, uint8_t clear);
bool     hta_nav_straight_wide(const hta_nav *n, uint32_t a, uint32_t b, uint8_t clear);
uint32_t hta_nav_smooth_wide(const hta_nav *n, uint32_t *path, uint32_t len, uint8_t clear);
uint32_t hta_nav_random_wide(const hta_nav *n, uint32_t *rng, uint8_t clear);

/* Keep a built grid on disk: building it is seconds on a phone, reading it
 * back is milliseconds. `key` should change whenever the map or the
 * parameters do (the map's CRC and the biped's numbers). A file with any
 * other key, version or size is refused and the caller builds afresh. */
bool hta_nav_save(const hta_nav *n, const char *path, uint32_t key);
bool hta_nav_load(hta_nav *n, const char *path, uint32_t key);

/* A random node in the main region, for somewhere to wander to. */
uint32_t hta_nav_random(const hta_nav *n, uint32_t *rng);

#endif
