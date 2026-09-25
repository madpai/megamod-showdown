/* Open Asset Lab .oalmap v1/v2 runtime adapter. V2 adds a lightmap texture
 * index to each material group; vertices retain the same lightmap UV fields. */
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
    /* Where each team's flag stands, when the map says (TF2's
     * item_teamflag, from the manifest's flag_points). */
    bool  has_flag[2];
    float flag[2][3];
    /* Props that break (Source prop_physics), from the manifest's
     * "breakables", in index order. Their triangles are in groups flagged
     * HTA_EXTERNAL_GROUP_BREAKABLE and are not in solid_indices: a
     * breakable owns its collision while whole (props.h). */
    struct hta_external_breakable *breakables;
    uint32_t breakable_count;
    /* Per submesh: the breakable it belongs to plus one, 0 for none. */
    uint16_t *submesh_breakable;
    /* The map's own weather ("weather": {"kind", "intensity"}): -1 none,
     * else an hta_weather_kind (1 rain, 2 storm, 3 snow, 4 ash, 5 sand). */
    int   weather;
    float weather_intensity;
} hta_external_map;

typedef struct hta_external_breakable {
    float    min[3], max[3];   /* runtime units */
    uint8_t  material;         /* hta_rigid_material: 0 wood 1 metal 2 concrete 3 glass */
    float    health;           /* 0: the material's default */
    bool     explosive;
    float    blast_damage, blast_radius;
} hta_external_breakable;

#define HTA_EXTERNAL_GROUP_NO_COLLISION 1u
/* Drawn blended by its texture's alpha, after the solid world: Source's
 * $alphatest and $translucent materials (chain-link, hay, cobwebs, glass). */
#define HTA_EXTERNAL_GROUP_ALPHA 2u
/* One breakable prop's triangles; its index + 1 in bits 8..23. */
#define HTA_EXTERNAL_GROUP_BREAKABLE 4u

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
