/* GBXModel (mod2) extraction, scenery/vehicle instancing, and the sky model.
 *
 * PC/Trial stores model verts/indices in the cache's model_data blob
 * (tag-data-header model_data_file_offset / vertex_size).
 */
#ifndef HTA_MODEL_H
#define HTA_MODEL_H

#include "bitmap.h"

#define HTA_SCENARIO_SKIES_OFF     0x030u
#define HTA_SCENARIO_SCENERY_OFF   0x210u
#define HTA_SCENARIO_SCENERY_PAL   0x21Cu
#define HTA_SCENARIO_VEHICLES_OFF  0x240u
#define HTA_SCENARIO_VEHICLE_PAL   0x24Cu

#define HTA_OBJECT_MODEL_ID        0x34u  /* Object.model TagDependency.tag_id */
#define HTA_OBJECT_COLLISION_ID    0x7Cu  /* Object.collision_model tag_id */
#define HTA_MOD2_FLAGS             0x000u
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
#define HTA_VTYPE_MODEL_UNCOMP     4u
#define HTA_MODEL_VTX_SIZE         68u
#define HTA_SCENERY_ENTRY_SIZE     72u
#define HTA_VEHICLE_ENTRY_SIZE     120u
#define HTA_PALETTE_ENTRY_SIZE     48u

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
