/* Video, display and effects settings. Portable: no Vulkan, no platform.
 *
 * One struct describes everything a player can tune, from a phone that
 * needs half-resolution and nothing extra to a desktop GPU running
 * supersampling, MSAA, bloom and fog. The renderer reads the video half;
 * the game reads the effects half (how much debris, gore and weather to
 * simulate) so a low preset saves CPU as well as GPU.
 *
 * Presets are starting points. Changing any field makes the settings
 * CUSTOM; picking a preset overwrites every field.
 *
 * Settings persist as plain `key = value` lines, so a player (or a
 * desktop config tool) can edit the file by hand. Unknown keys are kept
 * out of the struct and ignored; out-of-range values are clamped. */
#ifndef HTA_GFX_SETTINGS_H
#define HTA_GFX_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HTA_QUALITY_POTATO = 0,  /* oldest phones: 50% scale, nothing extra */
    HTA_QUALITY_LOW,
    HTA_QUALITY_MEDIUM,
    HTA_QUALITY_HIGH,
    HTA_QUALITY_ULTRA,       /* desktop GPUs: supersampled, MSAA x4 */
    HTA_QUALITY_CUSTOM,
    HTA_QUALITY_COUNT
} hta_quality;

typedef enum {
    HTA_TONEMAP_NONE = 0,    /* clamp: the game's original look */
    HTA_TONEMAP_ACES,        /* filmic; highlights roll off instead of clip */
    HTA_TONEMAP_COUNT
} hta_tonemap;

typedef enum {
    HTA_WINDOW_WINDOWED = 0, /* desktop only; Android is always fullscreen */
    HTA_WINDOW_BORDERLESS,
    HTA_WINDOW_FULLSCREEN,
    HTA_WINDOW_COUNT
} hta_window_mode;

typedef struct {
    hta_quality preset;

    /* ---- video: read by the renderer ---- */
    float render_scale;      /* 0.5 .. 2.0 of the output resolution */
    bool  dynamic_res;       /* let hta_gfx_settings_adapt move render_scale */
    float dynamic_res_min;   /* floor for the adaptive scale */
    float target_fps;        /* what dynamic resolution aims for */
    uint32_t msaa;           /* 1, 2, 4 or 8; clamped to what the GPU has */
    uint32_t anisotropy;     /* 1 .. 16; applies when the renderer is (re)built */
    bool  vsync;
    uint32_t fps_cap;        /* 0 = uncapped */

    /* post-processing; all of it is skipped when `post` is off */
    bool  post;
    hta_tonemap tonemap;
    float exposure;          /* linear multiplier before tonemapping */
    float contrast;          /* 1 = unchanged */
    float saturation;        /* 1 = unchanged, 0 = greyscale */
    float warmth;            /* -1 cool .. +1 warm, 0 = unchanged */
    bool  bloom;
    float bloom_strength;
    float bloom_threshold;   /* scene-linear; >1 means only true highlights */
    bool  fxaa;
    float sharpen;           /* 0 .. 1, contrast-adaptive; useful below 100% scale */
    float vignette;          /* 0 .. 1 */

    /* atmosphere: distance fog, drawn in the forward pass */
    bool  fog;
    float fog_density;       /* per world unit, exponential-squared */
    float fog_start;         /* world units of clear air before fog begins */
    float fog_color[3];      /* when fog_from_scene is off */
    bool  fog_from_scene;    /* take the colour from the map's clear/sky colour */
    float draw_distance;     /* multiplies the camera's far plane, 0.5 .. 2 */

    /* ---- effects: read by the game, not the renderer ---- */
    float particle_density;  /* 0 .. 1 of what an effect asks for */
    uint32_t max_debris;     /* live rigid-body chunks: props, gibs, casings */
    uint32_t gib_level;      /* 0 off, 1 chunks, 2 chunks + blood trails */
    float weather_density;   /* 0 .. 1 of a weather's drop count */
    uint32_t max_decals;

    /* ---- display: desktop only ---- */
    hta_window_mode window_mode;
    uint32_t window_width, window_height;
    float fov_degrees;       /* horizontal-ish; the game maps it to fov_y */
} hta_gfx_settings;

/* Every field of `q`'s preset. CUSTOM yields MEDIUM's values. */
void hta_gfx_settings_preset(hta_gfx_settings *s, hta_quality q);

/* Clamps every field into range; returns true if anything changed. */
bool hta_gfx_settings_clamp(hta_gfx_settings *s);

/* True when the renderer can draw straight to the screen: output
 * resolution, no MSAA, no post-processing. The cheapest path. */
bool hta_gfx_settings_direct(const hta_gfx_settings *s);

/* A preset from what the device says about itself. `mobile` is true on
 * phones; `vram_mib` is device-local memory (0 if unknown); `cores` is the
 * CPU count. Deliberately conservative: a player can always go up. */
hta_quality hta_gfx_settings_suggest(const char *device_name, bool mobile,
                                     uint64_t vram_mib, uint32_t cores);

/* Dynamic resolution: given the last frame's GPU+CPU time in ms, nudge
 * render_scale toward the target frame rate within
 * [dynamic_res_min, preset scale]. Returns true if it changed (the caller
 * rebuilds the scene target). Hysteresis keeps it from oscillating. */
bool hta_gfx_settings_adapt(hta_gfx_settings *s, float frame_ms, float ceiling_scale);

/* Parse `key = value` lines over the current values. Blank lines and lines
 * starting with '#' or ';' are skipped. Returns the number of keys applied.
 * A `preset = name` line is applied first, whatever its position, so the
 * other lines can override it. */
int  hta_gfx_settings_parse(hta_gfx_settings *s, const char *text, size_t len);

/* Writes every key. Returns bytes written (excluding NUL), or 0 if `cap`
 * is too small. */
size_t hta_gfx_settings_format(const hta_gfx_settings *s, char *out, size_t cap);

bool hta_gfx_settings_load(hta_gfx_settings *s, const char *path);
bool hta_gfx_settings_save(const hta_gfx_settings *s, const char *path);

const char *hta_quality_name(hta_quality q);
bool        hta_quality_from_name(const char *name, hta_quality *out);

#endif
