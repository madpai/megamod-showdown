/* The screen HUD, built from Halo's own `wphi` weapon HUD interface tag.
 *
 * Halo's HUD art is white with an alpha mask and is tinted by a colour the
 * tag carries, so nothing here invents a look: the reticle bitmap, which
 * sprite of it to use, and what colour to draw it are all read from the
 * weapon's own HUD tag.
 *
 * Quads come out in CLIP space (x and y in -1..1) because the HUD needs no
 * camera and no matrix; the renderer draws them last with no depth.
 *
 * Halo lays the HUD out on a fixed canvas (640x480) and scales it to the
 * screen, so a reticle is the same apparent size on any device.
 */
#ifndef HTA_HUD_H
#define HTA_HUD_H

#include "../asset/cache.h"
#include "../asset/bsp.h"
#include "../asset/bitmap.h"
#include "../asset/weapon.h"

#define HTA_HUD_CANVAS_W 640.0f
#define HTA_HUD_CANVAS_H 480.0f

/* HUDInterfaceAnchor */
#define HTA_HUD_ANCHOR_TOP_LEFT      0u
#define HTA_HUD_ANCHOR_TOP_RIGHT     1u
#define HTA_HUD_ANCHOR_BOTTOM_LEFT   2u
#define HTA_HUD_ANCHOR_BOTTOM_RIGHT  3u
#define HTA_HUD_ANCHOR_CENTER        4u

#define HTA_HUD_MAX_ELEMENTS 8

typedef struct {
    uint32_t vertex;        /* first of its four */
    uint32_t submesh;
    float    w_px, h_px;    /* native size on the 640x480 canvas */
    float    offset[2];     /* the tag's anchor offset, canvas px */
    uint8_t  anchor;        /* HTA_HUD_ANCHOR_* */
    float    uv[4];         /* u0, v0, u1, v1 */
} hta_hud_elem;

typedef struct {
    hta_bsp_mesh mesh;          /* clip-space quads; owns its textures */
    bool         loaded;

    hta_hud_elem elem[HTA_HUD_MAX_ELEMENTS];
    uint32_t     elem_count;

    /* crosshair */
    bool     have_cross;
    uint32_t cross_elem;
    float    cross_px;          /* native size on the 640x480 canvas */

    /* unit HUD: shield and health, from `unhi`. */
    bool     have_unit;
    int32_t  shield_meter;      /* element index, -1 if absent */
    int32_t  health_meter;
    float    shield_min[3], shield_max[3];   /* the tag's empty/full colours */
    float    health_min[3], health_max[3];
} hta_hud;

/* Reads the weapon's `wphi`, decodes its reticle, and reserves geometry.
 * Returns false only on a hard error; a weapon with no crosshair simply
 * leaves have_cross false. */
bool hta_hud_load(hta_hud *h, const hta_cache *c, const hta_resource_map *bitmaps,
                  const hta_weapon_def *weap, char *err, size_t errlen);
void hta_hud_free(hta_hud *h);

/* Positions everything for a screen of this size. Call when it changes;
 * it is cheap enough to call every frame. */
void hta_hud_layout(hta_hud *h, uint32_t screen_w, uint32_t screen_h);

/* 0..1. The bar fills accordingly and takes the colour the tag gives for
 * that level, lerped between its empty and full colours. */
void hta_hud_set_shield(hta_hud *h, float fraction);
void hta_hud_set_health(hta_hud *h, float fraction);

#endif
