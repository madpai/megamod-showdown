/* Shared fallback lighting. Blood Gulch's BSP carries zeroed "default" ambient
 * and distant lights (real lighting lives in the lightmap textures, which we
 * do not sample yet), so without this the map renders almost black. */
#ifndef HTA_SCENE_LIGHT_H
#define HTA_SCENE_LIGHT_H

#include "../asset/bsp.h"

/* Fills light_dir/light_color/ambient from the BSP, substituting a neutral
 * outdoor key light wherever the BSP provides nothing usable. */
static inline void hta_scene_light_from_bsp(const hta_bsp_mesh *m,
                                            float light_dir[3], float light_color[3],
                                            float ambient[3])
{
    float lsum = 0.0f, asum = 0.0f, dsum = 0.0f;
    for (int i = 0; i < 3; i++) {
        light_dir[i]   = m->light0_dir[i];
        light_color[i] = m->light0_color[i];
        ambient[i]     = m->ambient[i];
        lsum += light_color[i] < 0 ? -light_color[i] : light_color[i];
        asum += ambient[i]     < 0 ? -ambient[i]     : ambient[i];
        dsum += light_dir[i]   < 0 ? -light_dir[i]   : light_dir[i];
    }
    if (dsum < 1e-4f) { light_dir[0] = 0.35f; light_dir[1] = 0.25f; light_dir[2] = -0.90f; }
    if (lsum < 1e-3f) { light_color[0] = 0.95f; light_color[1] = 0.93f; light_color[2] = 0.85f; }
    if (asum < 1e-3f) { ambient[0] = 0.38f; ambient[1] = 0.42f; ambient[2] = 0.50f; }
}

#endif
