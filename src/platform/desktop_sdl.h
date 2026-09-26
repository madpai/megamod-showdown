/* Desktop platform layer: a window, a Vulkan renderer presenting to it,
 * input, and frame pacing, on SDL2. Linux, Windows and macOS (MoltenVK)
 * alike; the engine and renderer see none of SDL.
 *
 * This is the desktop half of the platform interface the Android loop will
 * be moved onto (docs/ENGINE_ARCHITECTURE.md): the game asks for a window,
 * a renderer, this frame's input and the time, and never for anything
 * platform-specific. */
#ifndef HTA_DESKTOP_SDL_H
#define HTA_DESKTOP_SDL_H

#include <stdbool.h>
#include <stdint.h>
#include "../gfx/gfx.h"
#include "../gfx/gfx_settings.h"
#include "../app/input.h"

/* hta_input, the device-neutral frame input, is src/app/input.h. */

typedef struct hta_desktop hta_desktop;

/* A window sized and moded by `s` (window_mode, window_width/height) and a
 * renderer built with `s`. NULL, with the reason in `err`, on failure. */
hta_desktop *hta_desktop_open(const char *title, const hta_gfx_settings *s, char *err, size_t errlen);
void         hta_desktop_close(hta_desktop *d);
hta_gfx     *hta_desktop_gfx(hta_desktop *d);
void         hta_desktop_size(const hta_desktop *d, uint32_t *w, uint32_t *h);
void         hta_desktop_set_title(hta_desktop *d, const char *title);
/* The SDL_Window, for a caller that runs its own SDL event loop. */
void        *hta_desktop_window(hta_desktop *d);

/* Pumps events into `in`. Handles a resize itself (the renderer's
 * swapchain follows the window). Returns false once the window is closed. */
bool hta_desktop_poll(hta_desktop *d, hta_input *in, float mouse_sensitivity);
/* Grab or release the mouse for mouselook. */
void hta_desktop_capture_mouse(hta_desktop *d, bool on);
/* Applies new settings: window mode and size, vsync, then the renderer's. */
bool hta_desktop_apply(hta_desktop *d, const hta_gfx_settings *s, char *err, size_t errlen);

/* Seconds since open, and sleeps out the rest of the frame when the
 * settings cap the rate (fps_cap; vsync already paces the swapchain).
 * Returns the frame's dt, clamped to 0.1 s. */
double hta_desktop_time(const hta_desktop *d);
float  hta_desktop_pace(hta_desktop *d, uint32_t fps_cap);

#endif
