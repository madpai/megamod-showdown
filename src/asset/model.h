/* GBXModel (mod2) extraction, scenery/vehicle instancing, and the sky model.
 *
 * PC/Trial stores model verts/indices in the cache's model_data blob
 * (tag-data-header model_data_file_offset / vertex_size).
 */
#ifndef HTA_MODEL_H
#define HTA_MODEL_H

#include "bitmap.h"
#include "anim.h"

#define HTA_SCENARIO_SKIES_OFF     0x030u
#define HTA_SCENARIO_SCENERY_OFF   0x210u
#define HTA_SCENARIO_SCENERY_PAL   0x21Cu
#define HTA_SCENARIO_VEHICLES_OFF  0x240u
#define HTA_SCENARIO_VEHICLE_PAL   0x24Cu

#define HTA_OBJECT_MODEL_ID        0x34u  /* Object.model TagDependency.tag_id */
#define HTA_OBJECT_COLLISION_ID    0x7Cu  /* Object.collision_model tag_id */
#define HTA_MOD2_FLAGS             0x000u
#define HTA_MOD2_FLAG_LOCAL_NODES  0x2u   /* "parts have local nodes" */
#define HTA_MOD2_U_SCALE           48u
#define HTA_MOD2_V_SCALE           52u
#define HTA_MOD2_NODES             0x0B8u
#define HTA_NODE_SIZE              156u
#define HTA_NODE_PARENT            36u
#define HTA_NODE_DEF_T             40u
#define HTA_NODE_DEF_Q             52u
#define HTA_COLL_NODES             0x28Cu
#define HTA_COLL_NODE_SIZE         64u
#define HTA_COLL_NODE_BSPS         52u
#define HTA_MOD2_REGIONS           0x0C4u
#define HTA_MOD2_GEOMETRIES        0x0D0u
#define HTA_MOD2_SHADERS           0x0DCu
#define HTA_GEOM_PARTS             0x24u  /* +36 inside GBXModelGeometry */
#define HTA_GEOM_SIZE              0x30u
#define HTA_REGION_SIZE            76u
#define HTA_REGION_PERMS           64u
#define HTA_PERM_SIZE              88u
#define HTA_PERM_SUPER_HIGH        72u
#define HTA_PART_SIZE              132u
#define HTA_PART_SHADER            4u
#define HTA_PART_TRI_BUF           68u  /* TriangleBufferType: 0 list, 1 strip */
#define HTA_PART_TRI_COUNT         72u  /* list: triangles; strip: index count */
#define HTA_PART_TRI_OFFSET        76u
#define HTA_PART_VTYPE             84u  /* 4 = model uncompressed */
#define HTA_PART_VCOUNT            88u
#define HTA_PART_VOFFSET           100u
#define HTA_PART_LOCAL_NODE_COUNT  107u  /* u8 */
#define HTA_PART_LOCAL_NODES       108u  /* u8[22] */
#define HTA_PART_MAX_LOCAL_NODES   22u
#define HTA_VTYPE_MODEL_UNCOMP     4u
#define HTA_MODEL_VTX_SIZE         68u
#define HTA_MODEL_VTX_NODE0        56u  /* u16 */
#define HTA_MODEL_VTX_NODE1        58u  /* u16 */
#define HTA_MODEL_VTX_WEIGHT0      60u  /* float */
#define HTA_MODEL_VTX_WEIGHT1      64u  /* float */
#define HTA_SCENERY_ENTRY_SIZE     72u
#define HTA_VEHICLE_ENTRY_SIZE     120u
#define HTA_PALETTE_ENTRY_SIZE     48u

/* One vertex's binding onto an animation graph's skeleton. Node indices are
 * already resolved: local-node tables and model-node names are both collapsed
 * into graph node indices at load time, so skinning is a flat lookup. */
typedef struct {
    uint16_t node[2];    /* graph node index; HTA_SKIN_NONE = unbound */
    float    weight[2];
} hta_skin_vertex;

#define HTA_SKIN_NONE 0xFFFFu

/* Append a first-person mod2 onto `dst`, bound to `g`'s skeleton by node name.
 * `*skin` grows in step with dst->vertices (realloc'd; free it yourself).
 * `rest_inv` and `have_rest` are indexed by GRAPH node and are filled in for
 * the nodes this model owns -- the hands and the weapon own disjoint nodes, so
 * two calls populate one shared table. */
bool hta_model_append_skinned(hta_bsp_mesh *dst, hta_skin_vertex **skin,
                              const hta_cache *c, const hta_resource_map *bitmaps,
                              uint32_t model_tag_id, const hta_anim_graph *g,
                              hta_transform *rest_inv, uint8_t *have_rest,
                              char *err, size_t errlen);

/* Does this model define a node by that name?
 *
 * Which tells us whether a weapon's first-person model carries its own arms:
 * almost every Trial weapon's fp model is the gun alone, 3 to 7 nodes, and
 * the arms come from the globals hands model. The fuel rod gun's is a
 * complete first-person model, 41 nodes, arms included. */
bool hta_model_has_node(const hta_cache *c, uint32_t model_tag_id,
                        const char *node_name);

/* A named marker on a mod2: which node it hangs from and where.
 *
 * The node comes back as a NAME, not an index, because a model's node list is
 * its own -- what maps it into an animation graph is the name
 * (hta_anim_node_index), the same way hta_model_append_skinned binds skins.
 *
 * GBXModel markers sit at +0xAC, immediately before the node list at +0xB8.
 * ModelMarker is 64 bytes (name, then instances at +52); ModelMarkerInstance
 * is 32 (region, permutation, node index, then translation at +4). */
bool hta_model_marker(const hta_cache *c, uint32_t model_tag_id,
                      const char *marker_name,
                      char out_node_name[32], float out_translation[3]);

/* Append a placed mod2 (scenery, vehicle, …) onto `world`, textures interned. */
bool hta_model_instance(hta_bsp_mesh *world, const hta_cache *c,
                        const hta_resource_map *bitmaps, uint32_t model_tag_id,
                        const float pos[3], const float rot[3],
                        char *err, size_t errlen);

/* Every scenery + vehicle placement from the scenario. */
bool hta_scenario_add_objects(hta_bsp_mesh *world, const hta_cache *c,
                              const hta_resource_map *bitmaps,
                              char *err, size_t errlen);

/* Append each scenery/vehicle collision BSP (rest pose) onto `col`.
 * Call after hta_bsp_load_collision and before hta_collision_build. */
bool hta_scenario_add_collision(hta_bsp_mesh *col, const hta_cache *c,
                                char *err, size_t errlen);

/* Sky dome in model space (drawn camera-relative). */
bool hta_sky_load(hta_bsp_mesh *out, const hta_cache *c,
                  const hta_resource_map *bitmaps, char *err, size_t errlen);

#endif
