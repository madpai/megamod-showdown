#include "gfx_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every number in the presets below is OURS, chosen by eye and by budget,
 * not taken from any tag. They are in HANDOFF.md's ledger. */

static const char *const kQualityNames[HTA_QUALITY_COUNT] = {
    "potato", "low", "medium", "high", "ultra", "custom"
};

const char *hta_quality_name(hta_quality q)
{
    return (unsigned)q < HTA_QUALITY_COUNT ? kQualityNames[q] : "custom";
}

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

bool hta_quality_from_name(const char *name, hta_quality *out)
{
    if (!name) return false;
    for (int i = 0; i < HTA_QUALITY_COUNT; i++)
        if (ieq(name, kQualityNames[i])) { if (out) *out = (hta_quality)i; return true; }
    return false;
}

void hta_gfx_settings_preset(hta_gfx_settings *s, hta_quality q)
{
    if (!s) return;
    if ((unsigned)q >= HTA_QUALITY_CUSTOM) q = HTA_QUALITY_MEDIUM;
    memset(s, 0, sizeof(*s));
    s->preset = q;

    /* Shared defaults: the original look, nothing surprising. */
    s->render_scale = 1.0f;
    s->dynamic_res_min = 0.5f;
    s->target_fps = 60.0f;
    s->msaa = 1;
    s->anisotropy = 4;
    s->vsync = true;
    s->tonemap = HTA_TONEMAP_NONE;
    s->exposure = 1.0f;
    s->contrast = 1.0f;
    s->saturation = 1.0f;
    s->bloom_strength = 0.35f;
    s->bloom_threshold = 1.0f;
    s->fog_density = 0.0009f;
    s->fog_start = 30.0f;
    s->fog_color[0] = 0.62f; s->fog_color[1] = 0.68f; s->fog_color[2] = 0.76f;
    s->fog_from_scene = true;
    s->draw_distance = 1.0f;
    s->particle_density = 1.0f;
    s->max_debris = 64;
    s->gib_level = 2;
    s->weather_density = 1.0f;
    s->max_decals = 128;
    s->window_mode = HTA_WINDOW_WINDOWED;
    s->window_width = 1600;
    s->window_height = 900;
    s->fov_degrees = 70.0f;

    switch (q) {
    case HTA_QUALITY_POTATO:
        s->render_scale = 0.5f;  s->anisotropy = 1;
        s->target_fps = 30.0f;
        s->particle_density = 0.35f; s->max_debris = 16; s->gib_level = 1;
        s->weather_density = 0.25f; s->max_decals = 32;
        s->draw_distance = 0.75f;
        break;
    case HTA_QUALITY_LOW:
        s->render_scale = 0.75f; s->dynamic_res = true; s->dynamic_res_min = 0.5f;
        s->anisotropy = 2; s->target_fps = 30.0f;
        s->fog = true;
        s->particle_density = 0.6f; s->max_debris = 32; s->gib_level = 1;
        s->weather_density = 0.5f; s->max_decals = 64;
        break;
    case HTA_QUALITY_MEDIUM:
        s->dynamic_res = true; s->dynamic_res_min = 0.6f;
        s->post = true; s->bloom = true; s->fxaa = true; s->vignette = 0.15f;
        s->fog = true;
        s->particle_density = 0.85f; s->weather_density = 0.8f;
        break;
    case HTA_QUALITY_HIGH:
        s->msaa = 2; s->anisotropy = 8;
        s->post = true; s->tonemap = HTA_TONEMAP_ACES; s->exposure = 1.05f;
        s->saturation = 1.08f; s->contrast = 1.04f;
        s->bloom = true; s->bloom_strength = 0.45f; s->vignette = 0.2f;
        s->fog = true;
        s->max_debris = 128; s->max_decals = 256;
        break;
    case HTA_QUALITY_ULTRA:
        s->render_scale = 1.5f; s->msaa = 4; s->anisotropy = 16;
        s->post = true; s->tonemap = HTA_TONEMAP_ACES; s->exposure = 1.05f;
        s->saturation = 1.1f; s->contrast = 1.05f;
        s->bloom = true; s->bloom_strength = 0.5f; s->vignette = 0.2f;
        s->sharpen = 0.2f;
        s->fog = true; s->draw_distance = 1.5f;
        s->max_debris = 256; s->max_decals = 512;
        break;
    default: break;
    }
}

static bool clampf(float *v, float lo, float hi, float fallback)
{
    float o = *v;
    if (!isfinite(*v)) *v = fallback;
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
    return o != *v;
}

static bool clampu(uint32_t *v, uint32_t lo, uint32_t hi)
{
    uint32_t o = *v;
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
    return o != *v;
}

bool hta_gfx_settings_clamp(hta_gfx_settings *s)
{
    if (!s) return false;
    bool c = false;
    if ((unsigned)s->preset >= HTA_QUALITY_COUNT) { s->preset = HTA_QUALITY_CUSTOM; c = true; }
    c |= clampf(&s->render_scale, 0.25f, 2.0f, 1.0f);
    c |= clampf(&s->dynamic_res_min, 0.25f, 1.0f, 0.5f);
    c |= clampf(&s->target_fps, 20.0f, 360.0f, 60.0f);
    /* MSAA is a power of two; round down to one. */
    {
        uint32_t m = s->msaa, p = 1;
        while (p * 2 <= m && p < 8) p *= 2;
        if (m == 0) p = 1;
        if (p != s->msaa) { s->msaa = p; c = true; }
    }
    c |= clampu(&s->anisotropy, 1, 16);
    c |= clampu(&s->fps_cap, 0, 1000);
    if ((unsigned)s->tonemap >= HTA_TONEMAP_COUNT) { s->tonemap = HTA_TONEMAP_NONE; c = true; }
    c |= clampf(&s->exposure, 0.1f, 8.0f, 1.0f);
    c |= clampf(&s->contrast, 0.5f, 2.0f, 1.0f);
    c |= clampf(&s->saturation, 0.0f, 2.0f, 1.0f);
    c |= clampf(&s->warmth, -1.0f, 1.0f, 0.0f);
    c |= clampf(&s->bloom_strength, 0.0f, 2.0f, 0.35f);
    c |= clampf(&s->bloom_threshold, 0.1f, 8.0f, 1.0f);
    c |= clampf(&s->sharpen, 0.0f, 1.0f, 0.0f);
    c |= clampf(&s->vignette, 0.0f, 1.0f, 0.0f);
    c |= clampf(&s->fog_density, 0.0f, 0.05f, 0.0009f);
    c |= clampf(&s->fog_start, 0.0f, 10000.0f, 30.0f);
    for (int k = 0; k < 3; k++) c |= clampf(&s->fog_color[k], 0.0f, 1.0f, 0.6f);
    c |= clampf(&s->draw_distance, 0.5f, 2.0f, 1.0f);
    c |= clampf(&s->particle_density, 0.0f, 1.0f, 1.0f);
    c |= clampu(&s->max_debris, 0, 1024);
    c |= clampu(&s->gib_level, 0, 2);
    c |= clampf(&s->weather_density, 0.0f, 1.0f, 1.0f);
    c |= clampu(&s->max_decals, 0, 4096);
    if ((unsigned)s->window_mode >= HTA_WINDOW_COUNT) { s->window_mode = HTA_WINDOW_WINDOWED; c = true; }
    c |= clampu(&s->window_width, 320, 16384);
    c |= clampu(&s->window_height, 200, 16384);
    c |= clampf(&s->fov_degrees, 50.0f, 120.0f, 70.0f);
    /* The adaptive floor cannot be above where the scale starts. */
    if (s->dynamic_res_min > s->render_scale) { s->dynamic_res_min = s->render_scale; c = true; }
    return c;
}

bool hta_gfx_settings_direct(const hta_gfx_settings *s)
{
    if (!s) return true;
    return !s->post && s->msaa <= 1 && fabsf(s->render_scale - 1.0f) < 1e-3f;
}

static bool has(const char *hay, const char *needle)
{
    if (!hay) return false;
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

/* The model number after `prefix` in a GPU name ("Adreno (TM) 740" -> 740). */
static int model_after(const char *name, const char *prefix)
{
    if (!name) return -1;
    size_t n = strlen(prefix);
    for (const char *p = name; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)prefix[i])) i++;
        if (i != n) continue;
        const char *q = p + n;
        while (*q && !isdigit((unsigned char)*q)) q++;
        if (!*q) return -1;
        return atoi(q);
    }
    return -1;
}

hta_quality hta_gfx_settings_suggest(const char *name, bool mobile, uint64_t vram_mib, uint32_t cores)
{
    /* Software rasterisers draw the game, slowly. */
    if (has(name, "llvmpipe") || has(name, "lavapipe") || has(name, "swiftshader"))
        return HTA_QUALITY_POTATO;
    if (mobile) {
        if (cores && cores <= 4) return HTA_QUALITY_POTATO;
        int adreno = model_after(name, "adreno");
        if (adreno >= 0) {
            if (adreno >= 730) return HTA_QUALITY_HIGH;
            if (adreno >= 640) return HTA_QUALITY_MEDIUM;
            if (adreno >= 600) return HTA_QUALITY_LOW;
            return HTA_QUALITY_POTATO;
        }
        if (has(name, "immortalis")) return HTA_QUALITY_HIGH;
        int mali = model_after(name, "mali-g");
        if (mali >= 0) {
            if (mali >= 710) return HTA_QUALITY_MEDIUM;   /* G710, G715, G720 */
            if (mali >= 76 && mali < 100) return HTA_QUALITY_MEDIUM;   /* G76..G78 */
            if (mali >= 610) return HTA_QUALITY_LOW;
            return HTA_QUALITY_LOW;
        }
        if (has(name, "xclipse")) return HTA_QUALITY_MEDIUM;
        return HTA_QUALITY_LOW;
    }
    /* Desktop: device-local memory is the best single hint we get. */
    bool integrated = has(name, "intel") && !has(name, "arc");
    if (integrated) return vram_mib >= 2048 ? HTA_QUALITY_MEDIUM : HTA_QUALITY_LOW;
    if (vram_mib >= 6144) return HTA_QUALITY_ULTRA;
    if (vram_mib >= 3072) return HTA_QUALITY_HIGH;
    if (vram_mib >= 1024) return HTA_QUALITY_MEDIUM;
    return vram_mib ? HTA_QUALITY_LOW : HTA_QUALITY_MEDIUM;
}

bool hta_gfx_settings_adapt(hta_gfx_settings *s, float frame_ms, float ceiling)
{
    if (!s || !s->dynamic_res || !(frame_ms > 0.0f)) return false;
    if (ceiling < s->dynamic_res_min) ceiling = s->dynamic_res_min;
    float budget = 1000.0f / (s->target_fps > 1.0f ? s->target_fps : 60.0f);
    float scale = s->render_scale;
    /* Asymmetric: drop fast when over budget, climb slowly when well
     * under, and hold anywhere between -- the hysteresis band. */
    if (frame_ms > budget * 1.10f)      scale -= 0.05f;
    else if (frame_ms < budget * 0.80f) scale += 0.025f;
    else return false;
    if (scale < s->dynamic_res_min) scale = s->dynamic_res_min;
    if (scale > ceiling) scale = ceiling;
    /* Quantise so the scene target is rebuilt on a real change only. */
    scale = floorf(scale * 40.0f + 0.5f) / 40.0f;
    if (fabsf(scale - s->render_scale) < 1e-4f) return false;
    s->render_scale = scale;
    return true;
}

/* ------------------------------ key = value ------------------------------ */

typedef enum { K_F, K_U, K_B, K_TONEMAP, K_WINDOW, K_RGB } kind;
typedef struct { const char *key; kind k; size_t off; } field;
#define F(name, k, member) { name, k, offsetof(hta_gfx_settings, member) }
static const field kFields[] = {
    F("render_scale", K_F, render_scale),
    F("dynamic_resolution", K_B, dynamic_res),
    F("dynamic_resolution_min", K_F, dynamic_res_min),
    F("target_fps", K_F, target_fps),
    F("msaa", K_U, msaa),
    F("anisotropy", K_U, anisotropy),
    F("vsync", K_B, vsync),
    F("fps_cap", K_U, fps_cap),
    F("post", K_B, post),
    F("tonemap", K_TONEMAP, tonemap),
    F("exposure", K_F, exposure),
    F("contrast", K_F, contrast),
    F("saturation", K_F, saturation),
    F("warmth", K_F, warmth),
    F("bloom", K_B, bloom),
    F("bloom_strength", K_F, bloom_strength),
    F("bloom_threshold", K_F, bloom_threshold),
    F("fxaa", K_B, fxaa),
    F("sharpen", K_F, sharpen),
    F("vignette", K_F, vignette),
    F("fog", K_B, fog),
    F("fog_density", K_F, fog_density),
    F("fog_start", K_F, fog_start),
    F("fog_color", K_RGB, fog_color),
    F("fog_from_scene", K_B, fog_from_scene),
    F("draw_distance", K_F, draw_distance),
    F("particle_density", K_F, particle_density),
    F("max_debris", K_U, max_debris),
    F("gib_level", K_U, gib_level),
    F("weather_density", K_F, weather_density),
    F("max_decals", K_U, max_decals),
    F("window_mode", K_WINDOW, window_mode),
    F("window_width", K_U, window_width),
    F("window_height", K_U, window_height),
    F("fov", K_F, fov_degrees),
};
#undef F
static const char *const kTonemapNames[HTA_TONEMAP_COUNT] = { "none", "aces" };
static const char *const kWindowNames[HTA_WINDOW_COUNT] = { "windowed", "borderless", "fullscreen" };

static bool parse_bool(const char *v, bool *out)
{
    if (ieq(v, "1") || ieq(v, "true") || ieq(v, "on") || ieq(v, "yes")) { *out = true; return true; }
    if (ieq(v, "0") || ieq(v, "false") || ieq(v, "off") || ieq(v, "no")) { *out = false; return true; }
    return false;
}

static bool apply(hta_gfx_settings *s, const char *key, const char *val)
{
    for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); i++) {
        const field *f = &kFields[i];
        if (!ieq(key, f->key)) continue;
        void *p = (uint8_t *)s + f->off;
        char *end = NULL;
        switch (f->k) {
        case K_F: { float x = strtof(val, &end); if (end == val) return false; *(float *)p = x; return true; }
        case K_U: { long x = strtol(val, &end, 10); if (end == val || x < 0) return false; *(uint32_t *)p = (uint32_t)x; return true; }
        case K_B: return parse_bool(val, (bool *)p);
        case K_TONEMAP:
            for (int t = 0; t < HTA_TONEMAP_COUNT; t++)
                if (ieq(val, kTonemapNames[t])) { *(hta_tonemap *)p = (hta_tonemap)t; return true; }
            return false;
        case K_WINDOW:
            for (int t = 0; t < HTA_WINDOW_COUNT; t++)
                if (ieq(val, kWindowNames[t])) { *(hta_window_mode *)p = (hta_window_mode)t; return true; }
            return false;
        case K_RGB: {
            float c[3]; const char *q = val;
            for (int k = 0; k < 3; k++) {
                while (*q == ' ' || *q == ',') q++;
                c[k] = strtof(q, &end);
                if (end == q) return false;
                q = end;
            }
            memcpy(p, c, sizeof(c));
            return true;
        }
        }
    }
    return false;
}

/* Splits the next line into trimmed key and value. */
static bool next_kv(const char **cur, const char *end, char *key, size_t kn, char *val, size_t vn)
{
    while (*cur < end) {
        const char *ls = *cur, *le = ls;
        while (le < end && *le != '\n') le++;
        *cur = le < end ? le + 1 : end;
        while (ls < le && isspace((unsigned char)*ls)) ls++;
        const char *te = le;
        while (te > ls && isspace((unsigned char)te[-1])) te--;
        if (ls == te || *ls == '#' || *ls == ';') continue;
        const char *eq = ls;
        while (eq < te && *eq != '=') eq++;
        if (eq == te) continue;
        const char *ke = eq;
        while (ke > ls && isspace((unsigned char)ke[-1])) ke--;
        const char *vs = eq + 1;
        while (vs < te && isspace((unsigned char)*vs)) vs++;
        size_t kl = (size_t)(ke - ls), vl = (size_t)(te - vs);
        if (kl == 0 || kl >= kn || vl >= vn) continue;
        memcpy(key, ls, kl); key[kl] = 0;
        memcpy(val, vs, vl); val[vl] = 0;
        return true;
    }
    return false;
}

int hta_gfx_settings_parse(hta_gfx_settings *s, const char *text, size_t len)
{
    if (!s || !text) return 0;
    const char *end = text + len;
    char key[64], val[128];
    int applied = 0;
    bool custom_fields = false;
    /* The preset first, so the lines after it can override it. */
    for (const char *cur = text; next_kv(&cur, end, key, sizeof key, val, sizeof val);) {
        hta_quality q;
        if (ieq(key, "preset") && hta_quality_from_name(val, &q)) {
            if (q != HTA_QUALITY_CUSTOM) hta_gfx_settings_preset(s, q);
            else s->preset = HTA_QUALITY_CUSTOM;
            applied++;
        }
    }
    hta_quality saved = s->preset;
    for (const char *cur = text; next_kv(&cur, end, key, sizeof key, val, sizeof val);) {
        if (ieq(key, "preset")) continue;
        hta_gfx_settings before = *s;
        if (apply(s, key, val)) {
            applied++;
            if (memcmp(&before, s, sizeof(before)) != 0) custom_fields = true;
        }
    }
    /* A value that differs from the preset makes it custom; a saved file
     * repeating the preset's own values stays on the preset. */
    if (custom_fields && saved != HTA_QUALITY_CUSTOM) {
        hta_gfx_settings ref;
        hta_gfx_settings_preset(&ref, saved);
        ref.window_mode = s->window_mode;      /* display fields are not quality */
        ref.window_width = s->window_width;
        ref.window_height = s->window_height;
        ref.fov_degrees = s->fov_degrees;
        ref.vsync = s->vsync;
        ref.fps_cap = s->fps_cap;
        ref.render_scale = s->dynamic_res ? s->render_scale : ref.render_scale;
        s->preset = memcmp(&ref, s, sizeof(ref)) == 0 ? saved : HTA_QUALITY_CUSTOM;
    }
    hta_gfx_settings_clamp(s);
    return applied;
}

size_t hta_gfx_settings_format(const hta_gfx_settings *s, char *out, size_t cap)
{
    if (!s || !out || cap == 0) return 0;
    size_t n = 0;
    int w = snprintf(out, cap, "# Megamod engine video settings. Edit freely; bad values are clamped.\n"
                               "preset = %s\n", hta_quality_name(s->preset));
    if (w < 0 || (size_t)w >= cap) return 0;
    n = (size_t)w;
    for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); i++) {
        const field *f = &kFields[i];
        const void *p = (const uint8_t *)s + f->off;
        switch (f->k) {
        case K_F: w = snprintf(out + n, cap - n, "%s = %g\n", f->key, (double)*(const float *)p); break;
        case K_U: w = snprintf(out + n, cap - n, "%s = %u\n", f->key, *(const uint32_t *)p); break;
        case K_B: w = snprintf(out + n, cap - n, "%s = %s\n", f->key, *(const bool *)p ? "on" : "off"); break;
        case K_TONEMAP: {
            unsigned t = (unsigned)*(const hta_tonemap *)p;
            w = snprintf(out + n, cap - n, "%s = %s\n", f->key, kTonemapNames[t < HTA_TONEMAP_COUNT ? t : 0]);
            break;
        }
        case K_WINDOW: {
            unsigned t = (unsigned)*(const hta_window_mode *)p;
            w = snprintf(out + n, cap - n, "%s = %s\n", f->key, kWindowNames[t < HTA_WINDOW_COUNT ? t : 0]);
            break;
        }
        case K_RGB: {
            const float *c = (const float *)p;
            w = snprintf(out + n, cap - n, "%s = %g, %g, %g\n", f->key, (double)c[0], (double)c[1], (double)c[2]);
            break;
        }
        }
        if (w < 0 || (size_t)w >= cap - n) return 0;
        n += (size_t)w;
    }
    return n;
}

bool hta_gfx_settings_load(hta_gfx_settings *s, const char *path)
{
    if (!s || !path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return hta_gfx_settings_parse(s, buf, n) > 0;
}

bool hta_gfx_settings_save(const hta_gfx_settings *s, const char *path)
{
    if (!s || !path) return false;
    char buf[8192];
    size_t n = hta_gfx_settings_format(s, buf, sizeof(buf));
    if (!n) return false;
    /* Write beside, then rename: a crash mid-write keeps the old file. */
    char tmp[1024];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    return rename(tmp, path) == 0;
}
