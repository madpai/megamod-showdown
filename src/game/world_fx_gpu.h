/* The renderer half of the world-effects director (world_fx.h): links
 * where the renderer does. */
#ifndef HTA_WORLD_FX_GPU_H
#define HTA_WORLD_FX_GPU_H

#include "../gfx/gfx.h"
#include "world_fx.h"

bool     hta_wfx_gpu_upload(hta_world_fx *w, hta_gfx *g);
void     hta_wfx_gpu_free(hta_world_fx *w, hta_gfx *g);
/* Builds this frame's vertices and appends up to three dynamic draws to
 * `dyn` (which holds `count` already, room for `max`). Returns the new count. */
uint32_t hta_wfx_gpu_draw(hta_world_fx *w, const hta_camera *cam, hta_gfx_dynamic *dyn,
                          uint32_t count, uint32_t max);
/* Dynamic resolution: feed the frame time; moves the renderer's scale
 * within its ceiling when the settings allow. */
void     hta_wfx_gpu_frame(hta_world_fx *w, hta_gfx *g, const hta_gfx_settings *s, float dt);

#endif
