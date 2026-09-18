/* Renderer interface. Vulkan lives behind this; the engine never sees it. */
#ifndef HTA_GFX_H
#define HTA_GFX_H

#include <stdbool.h>
#include "../engine/engine.h"

typedef struct hta_gfx hta_gfx;

/* native_window is an ANativeWindow* on Android. */
hta_gfx *hta_gfx_create(void *native_window);
void     hta_gfx_destroy(hta_gfx *g);
bool     hta_gfx_draw(hta_gfx *g, const hta_engine *e);
const char *hta_gfx_device_name(const hta_gfx *g);

#endif
