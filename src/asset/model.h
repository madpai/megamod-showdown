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
#define HTA_MOD2_GEOMETRIES        0x0D0u
#define HTA_MOD2_SHADERS           0x0DCu
#define HTA_GEOM_PARTS             0x24u  /* +36 inside GBXModelGeometry */
#define HTA_GEOM_SIZE              0x30u
#define HTA_PART_SIZE              132u
#define HTA_PART_SHADER            4u
#define HTA_PART_TRI_COUNT         72u
#define HTA_PART_TRI_OFFSET        76u
#define HTA_PART_VTYPE             86u
#define HTA_PART_VCOUNT            88u
#define HTA_PART_VOFFSET           100u
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

/* Sky dome in model space (drawn camera-relative). */
bool hta_sky_load(hta_bsp_mesh *out, const hta_cache *c,
                  const hta_resource_map *bitmaps, char *err, size_t errlen);

#endif
