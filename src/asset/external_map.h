/* Open Asset Lab .oalmap v1/v2/v3 runtime adapter. V2 adds a lightmap
 * texture index to each material group; vertices retain the same lightmap UV
 * fields. V3 is v2's layout plus a manifest "world_entities" section the
 * runtime must implement (world_def.h): an engine without it refuses v3
 * rather than load a world without its behaviour. */
#ifndef HTA_EXTERNAL_MAP_H
#define HTA_EXTERNAL_MAP_H
#include "bsp.h"
#include "package.h"
#include "world_def.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the world's package declared and loaded with it (X4): kept for
 * reports (megamod-content, megamod-resources); play never reads it. */
typedef struct {
    bool     declared;
    char     id[HTA_PKG_ID_MAX + 1];
    uint32_t provides, requires, imports;
    uint32_t dep_count;                              /* the closure, canonical order */
    char     dep[HTA_PKG_MAX_SET - 1][HTA_PKG_ID_MAX + 1];
    uint64_t dep_digest[HTA_PKG_MAX_SET - 1];
    bool     dep_direct[HTA_PKG_MAX_SET - 1];
} hta_external_package;

typedef struct {
    hta_bsp_mesh mesh;
    hta_spawn_point *spawns;
    uint32_t spawn_count;
    /* The world key: what two peers must agree on to share this world --
     * geometry, collision flags, spawns and every manifest member the
     * runtime plays by, not provenance (external_map.c). `digest` is the
     * 64-bit value, `key` its fold into the v10 map check's 32 bits. */
    uint64_t digest;
    uint32_t key;
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
    /* v3: the world's generic entities (buttons, doors, triggers...) and,
     * per submesh, the entity it draws plus one (0 none): a mover's
     * triangles, which are not in solid_indices -- it owns its collision. */
    uint32_t version;
    hta_world_defs world_defs;
    hta_external_package package;
    uint16_t *submesh_entity;
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
/* v3: one world entity's triangles (a mover); its index + 1 in bits 8..23. */
#define HTA_EXTERNAL_GROUP_ENTITY 8u

/* A mesh to build collision from: `render`'s vertices (which may have been
 * moved out of the package) with only the solid triangles. Borrows both;
 * never free it. */
void hta_external_map_collision_view(const hta_bsp_mesh *render, const hta_external_map *m,
                                     hta_bsp_mesh *view);

/* Spawn team_index from the manifest's "team": 0 red, 1 blue; a start
 * with no team is shared by both teams. */
#define HTA_EXTERNAL_TEAM_ANY 0xFFFFu

/* The world key's encoding; a change to what it covers is a new number. */
#define HTA_WORLD_KEY_SCHEMA 1u
static inline uint32_t hta_world_key_fold(uint64_t d) { return (uint32_t)d ^ (uint32_t)(d >> 32); }

bool hta_external_map_load(const char *path, hta_external_map *out, char *err, size_t errlen);
/* The same from bytes already in memory (an mmapped APK asset). Copies
 * what it keeps; `data` may be unmapped afterwards. */
bool hta_external_map_load_memory(const uint8_t *data, size_t size, hta_external_map *out,
                                  char *err, size_t errlen);
/* The same, loading every package the world requires (X4) through `src`
 * (NULL: a world that requires any is refused, naming what it needs). The
 * dependencies' content joins the world key. */
bool hta_external_map_load_with(const uint8_t *data, size_t size, const hta_pkg_source *src,
                                hta_external_map *out, char *err, size_t errlen);
void hta_external_map_free(hta_external_map *m);
/* The manifest members the world key covers, by index (NULL past the end). */
const char *hta_world_key_played(uint32_t i);
#endif
