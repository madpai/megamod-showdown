/* Open Asset Lab .oalmap v1 runtime adapter. Caller owns mesh and spawn array. */
#ifndef HTA_EXTERNAL_MAP_H
#define HTA_EXTERNAL_MAP_H
#include "bsp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    hta_bsp_mesh mesh;
    hta_spawn_point *spawns;
    uint32_t spawn_count;
    uint32_t key;          /* FNV-1a of the manifest: tells two packages apart */
    /* The triangles that collide: every group but those flagged
     * HTA_EXTERNAL_GROUP_NO_COLLISION (Source's non-solid props). */
    uint32_t *solid_indices;
    uint32_t solid_index_count;
} hta_external_map;

#define HTA_EXTERNAL_GROUP_NO_COLLISION 1u

/* A mesh to build collision from: `render`'s vertices (which may have been
 * moved out of the package) with only the solid triangles. Borrows both;
 * never free it. */
void hta_external_map_collision_view(const hta_bsp_mesh *render, const hta_external_map *m,
                                     hta_bsp_mesh *view);

/* Spawn team_index from the manifest's "team": 0 red, 1 blue; a start
 * with no team is shared by both teams. */
#define HTA_EXTERNAL_TEAM_ANY 0xFFFFu

bool hta_external_map_load(const char *path, hta_external_map *out, char *err, size_t errlen);
/* The same from bytes already in memory (an mmapped APK asset). Copies
 * what it keeps; `data` may be unmapped afterwards. */
bool hta_external_map_load_memory(const uint8_t *data, size_t size, hta_external_map *out,
                                  char *err, size_t errlen);
void hta_external_map_free(hta_external_map *m);
#endif
