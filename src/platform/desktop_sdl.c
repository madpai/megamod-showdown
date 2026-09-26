#include "desktop_sdl.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct hta_desktop {
    SDL_Window *win;
    hta_gfx    *gfx;
    hta_gfx_settings settings;
    uint64_t    t0, last;
    bool        captured;
};

static bool make_surface(void *instance, void *user, void *surface_out)
{
    SDL_Window *w = (SDL_Window *)user;
    /* VkInstance and VkSurfaceKHR travel as void*: the renderer's header
     * stays free of any window-system include, and this file of Vulkan's. */
    return SDL_Vulkan_CreateSurface(w, (VkInstance)instance, (VkSurfaceKHR *)surface_out) == SDL_TRUE;
}

static Uint32 window_flags(hta_window_mode m)
{
    Uint32 f = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (m == HTA_WINDOW_BORDERLESS) f |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else if (m == HTA_WINDOW_FULLSCREEN) f |= SDL_WINDOW_FULLSCREEN;
    return f;
}

hta_desktop *hta_desktop_open(const char *title, const hta_gfx_settings *s, char *err, size_t errlen)
{
    hta_desktop *d = calloc(1, sizeof(*d));
    if (!d) { snprintf(err, errlen, "out of memory"); return NULL; }
    if (s) d->settings = *s; else hta_gfx_settings_preset(&d->settings, HTA_QUALITY_MEDIUM);
    hta_gfx_settings_clamp(&d->settings);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        snprintf(err, errlen, "SDL: %s", SDL_GetError());
        free(d);
        return NULL;
    }
    d->win = SDL_CreateWindow(title ? title : "Megamod", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              (int)d->settings.window_width, (int)d->settings.window_height,
                              window_flags(d->settings.window_mode));
    if (!d->win) { snprintf(err, errlen, "window: %s", SDL_GetError()); hta_desktop_close(d); return NULL; }
    unsigned n = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(d->win, &n, NULL) || n == 0 || n > 16) {
        snprintf(err, errlen, "no Vulkan surface extensions: %s", SDL_GetError());
        hta_desktop_close(d);
        return NULL;
    }
    const char *exts[16];
    SDL_Vulkan_GetInstanceExtensions(d->win, &n, exts);
    int pw = 0, ph = 0;
    SDL_Vulkan_GetDrawableSize(d->win, &pw, &ph);
    d->gfx = hta_gfx_create_desktop(exts, n, make_surface, d->win, (uint32_t)pw, (uint32_t)ph,
                                    &d->settings, err, errlen);
    if (!d->gfx) { hta_desktop_close(d); return NULL; }
    d->t0 = d->last = SDL_GetPerformanceCounter();
    return d;
}

void hta_desktop_close(hta_desktop *d)
{
    if (!d) return;
    if (d->gfx) hta_gfx_destroy(d->gfx);
    if (d->win) SDL_DestroyWindow(d->win);
    SDL_Quit();
    free(d);
}

hta_gfx *hta_desktop_gfx(hta_desktop *d) { return d ? d->gfx : NULL; }
void *hta_desktop_window(hta_desktop *d) { return d ? d->win : NULL; }

void hta_desktop_size(const hta_desktop *d, uint32_t *w, uint32_t *h)
{
    int pw = 0, ph = 0;
    if (d && d->win) SDL_Vulkan_GetDrawableSize(d->win, &pw, &ph);
    if (w) *w = (uint32_t)pw;
    if (h) *h = (uint32_t)ph;
}

void hta_desktop_set_title(hta_desktop *d, const char *title)
{
    if (d && d->win && title) SDL_SetWindowTitle(d->win, title);
}

void hta_desktop_capture_mouse(hta_desktop *d, bool on)
{
    if (!d) return;
    d->captured = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

static void resize(hta_desktop *d)
{
    int pw = 0, ph = 0;
    SDL_Vulkan_GetDrawableSize(d->win, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;           /* minimised: wait */
    char err[256];
    if (!hta_gfx_resize(d->gfx, (uint32_t)pw, (uint32_t)ph, err, sizeof(err)))
        fprintf(stderr, "[desktop] resize to %dx%d failed: %s\n", pw, ph, err);
}

bool hta_desktop_poll(hta_desktop *d, hta_input *in, float sens)
{
    if (!d || !in) return false;
    memset(in->key_pressed, 0, sizeof(in->key_pressed));
    in->fire_pressed = in->alt_pressed = in->use_pressed = in->jump_pressed = false;
    in->crouch_pressed = false;
    in->reload = in->melee = in->swap = in->zoom = in->grenade = in->fly = in->ability = false;
    in->debug = 0;
    in->look_yaw = in->look_pitch = 0.0f;
    in->resized = false;
    if (sens <= 0.0f) sens = 0.0025f;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: in->quit = true; break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { resize(d); in->resized = true; }
            break;
        case SDL_MOUSEMOTION:
            if (d->captured) { in->look_yaw -= (float)e.motion.xrel * sens; in->look_pitch -= (float)e.motion.yrel * sens; }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) in->fire_pressed = true;
            if (e.button.button == SDL_BUTTON_RIGHT) in->alt_pressed = true;
            break;
        case SDL_KEYDOWN:
            if (!e.key.repeat && e.key.keysym.scancode < 512) in->key_pressed[e.key.keysym.scancode] = true;
            if (!e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_E) in->use_pressed = true;
            if (!e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_SPACE) in->jump_pressed = true;
            if (!e.key.repeat) switch (e.key.keysym.scancode) {   /* the named actions */
            case SDL_SCANCODE_E: in->swap = true; break;
            case SDL_SCANCODE_R: in->reload = true; break;
            case SDL_SCANCODE_F: in->melee = true; break;
            case SDL_SCANCODE_G: in->grenade = true; break;
            case SDL_SCANCODE_Q: in->ability = true; break;
            case SDL_SCANCODE_Z: in->zoom = true; break;
            case SDL_SCANCODE_V: in->fly = true; break;
            case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_C: in->crouch_pressed = true; break;
            default: break;
            }
            break;
        default: break;
        }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    Uint32 mb = SDL_GetMouseState(NULL, NULL);
    in->move_forward = (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    in->move_right = (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    in->jump = k[SDL_SCANCODE_SPACE];
    in->crouch = k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_C];
    in->use = k[SDL_SCANCODE_E];
    in->fire = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    in->alt_fire = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    /* Arrow keys look too, so it can be driven without a mouse. */
    float kl = 1.8f / 60.0f;
    if (k[SDL_SCANCODE_LEFT]) in->look_yaw += kl;
    if (k[SDL_SCANCODE_RIGHT]) in->look_yaw -= kl;
    if (k[SDL_SCANCODE_UP]) in->look_pitch += kl;
    if (k[SDL_SCANCODE_DOWN]) in->look_pitch -= kl;
    return !in->quit;
}

bool hta_desktop_apply(hta_desktop *d, const hta_gfx_settings *s, char *err, size_t errlen)
{
    if (!d || !s) return false;
    hta_gfx_settings n = *s;
    hta_gfx_settings_clamp(&n);
    if (n.window_mode != d->settings.window_mode) {
        Uint32 f = n.window_mode == HTA_WINDOW_BORDERLESS ? SDL_WINDOW_FULLSCREEN_DESKTOP :
                   n.window_mode == HTA_WINDOW_FULLSCREEN ? SDL_WINDOW_FULLSCREEN : 0;
        SDL_SetWindowFullscreen(d->win, f);
    }
    if (n.window_mode == HTA_WINDOW_WINDOWED &&
        (n.window_width != d->settings.window_width || n.window_height != d->settings.window_height))
        SDL_SetWindowSize(d->win, (int)n.window_width, (int)n.window_height);
    d->settings = n;
    bool ok = hta_gfx_apply_settings(d->gfx, &n, err, errlen);
    resize(d);
    return ok;
}

double hta_desktop_time(const hta_desktop *d)
{
    if (!d) return 0.0;
    return (double)(SDL_GetPerformanceCounter() - d->t0) / (double)SDL_GetPerformanceFrequency();
}

float hta_desktop_pace(hta_desktop *d, uint32_t cap)
{
    if (!d) return 0.0f;
    double freq = (double)SDL_GetPerformanceFrequency();
    if (cap) {
        double want = 1.0 / (double)cap;
        for (;;) {
            double el = (double)(SDL_GetPerformanceCounter() - d->last) / freq;
            if (el >= want) break;
            double left = want - el;
            if (left > 0.002) SDL_Delay((Uint32)((left - 0.001) * 1000.0));
        }
    }
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)((double)(now - d->last) / freq);
    d->last = now;
    return dt > 0.1f ? 0.1f : dt;
}
