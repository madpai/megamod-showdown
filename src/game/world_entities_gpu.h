/* The renderer half of the world's entities (engine/world_entities.h):
 * each mover's triangles -- the package's groups tagged with its index --
 * become a small mesh of their own, drawn as a rigid instance offset by how
 * far it has moved. The world mesh stops drawing those groups. X5: each
 * prop draws its model (a package-backed asset resource) where it stands.
 * Used by the phone and by megamod-join alike. */
#ifndef HTA_WORLD_ENTITIES_GPU_H
#define HTA_WORLD_ENTITIES_GPU_H

#include "../asset/asset_res.h"
#include "../engine/world_entities.h"
#include "../gfx/gfx.h"

typedef struct {
    hta_gfx_mesh *mesh[HTA_WDEF_MAX_ENTITIES];
    uint32_t count;
    /* X5: one GPU mesh per model a prop places (by asset table index),
     * uploaded once and shared by every prop that names it. */
    hta_gfx_mesh **model;
    uint32_t model_count, models_uploaded;
} hta_went_gpu;

/* `world`/`world_gpu`: the uploaded world mesh; `submesh_entity`: per
 * submesh, the entity it draws plus one (external_map.h); `assets`: the
 * world's asset table (props' models), may be NULL. Returns the number of
 * movers given a mesh. */
uint32_t hta_went_gpu_upload(hta_went_gpu *g, hta_gfx *gfx, const hta_bsp_mesh *world,
                             hta_gfx_mesh *world_gpu, const uint16_t *submesh_entity,
                             const hta_world_entities *w, const hta_asset_table *assets);
/* This frame's instances, appended to `out` (room for `cap`). */
uint32_t hta_went_gpu_instances(const hta_went_gpu *g, const hta_world_entities *w,
                                hta_gfx_instance *out, uint32_t cap);
void hta_went_gpu_free(hta_went_gpu *g, hta_gfx *gfx);

#endif
