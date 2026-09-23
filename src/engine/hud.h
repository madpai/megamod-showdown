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
#include "../asset/font.h"

#define HTA_HUD_CANVAS_W 640.0f
#define HTA_HUD_CANVAS_H 480.0f

/* Halo's canvas assumes a monitor at desk distance. A phone is a much
 * smaller slab of glass held at arm's length, and scaling the HUD straight
 * off screen height leaves it unreadably small -- more so on a 21:9 handset,
 * where scaling by height gives the HUD an even smaller share of the width
 * than Halo's 4:3 ever did. This is a deliberate departure from the tag, and
 * the one number here that Halo does not supply. */
#define HTA_HUD_PHONE_SCALE 1.75f

/* The weapon ammo block draws at half the size its sprite implies, and I do
 * not know which field says so. Measured, not derived: across three
 * screenshots of the real game the shield bar comes out at 0.99, 0.97 and
 * 0.86 of its sprite scaled to the canvas -- i.e. 1.0, which is what
 * everything else here already does -- while the ammo pip grid comes out at
 * 0.51. Same canvas (480p), same anchor, both scales 1.0 in the tag.
 *
 * So this is an empirical correction, not a tag value, and it is the second
 * and last invented number in the HUD. Whatever really produces it is
 * probably in hud_globals, which does not reconcile yet. */
#define HTA_HUD_WEAPON_SCALE 0.5f

/* HUDInterfaceAnchor */
#define HTA_HUD_ANCHOR_TOP_LEFT      0u
#define HTA_HUD_ANCHOR_TOP_RIGHT     1u
#define HTA_HUD_ANCHOR_BOTTOM_LEFT   2u
#define HTA_HUD_ANCHOR_BOTTOM_RIGHT  3u
#define HTA_HUD_ANCHOR_CENTER        4u

#define HTA_HUD_MAX_ELEMENTS 80
#define HTA_HUD_MAX_BLIPS 16
/* Where the motion tracker sits: Halo's bottom-left corner, this far in.
 * On a wide phone that is left of the thumb stick. Ours. */
#define HTA_HUD_SENSOR_X 4.0f
#define HTA_HUD_SENSOR_Y 4.0f

typedef struct {
    uint32_t vertex;        /* first of its four */
    uint32_t submesh;
    float    w_px, h_px;    /* native size on the 640x480 canvas */
    float    offset[2];     /* the tag's anchor offset, canvas px */
    uint8_t  anchor;        /* HTA_HUD_ANCHOR_* */
    float    extra_scale;   /* 1.0, except the weapon block's 0.5 */
    float    uv[4];         /* u0, v0, u1, v1 */
    /* 0 draws always; N draws only while scoped at that zoom level. The
     * sniper's scope brackets and reticle ticks are flagged "show only when
     * zoomed" in its tag, and are grouped per level. */
    int8_t   zoom_level;
    /* Ignores anchor, offset and scale and covers the whole screen. The
     * death fade is the only one. */
    bool     fullscreen;
} hta_hud_elem;

typedef struct {
    hta_bsp_mesh mesh;          /* clip-space quads; owns its textures */
    bool         loaded;

    hta_hud_elem elem[HTA_HUD_MAX_ELEMENTS];
    uint32_t     elem_count;

    /* crosshair */
    bool     have_cross;
    uint32_t cross_elem;
    float    cross_tint[4];     /* the tag's own colour, to go back to */
    bool     cross_on_target;
    float    cross_px;          /* native size on the 640x480 canvas */

    /* unit HUD: shield and health, from `unhi`. */
    bool     have_unit;
    int32_t  shield_meter;      /* element index, -1 if absent */
    int32_t  health_meter;
    float    shield_min[3], shield_max[3];   /* the tag's empty/full colours */
    float    health_min[3], health_max[3];

    /* The weapon's own ammo block: Halo draws the assault rifle's magazine
     * as a grid of pips, which is a meter like any other, on a plate from
     * the `child hud` chain. */
    /* Which zoom level is up, 0 unzoomed. Hides and shows scope furniture. */
    int      zoom_level;

    bool     have_ammo;
    int32_t  ammo_meter;                     /* element index, -1 if absent */
    float    ammo_min[3], ammo_max[3];

    /* The rounds counter. Halo draws HUD numbers with the `hud_globals`
     * fullscreen font, one glyph a digit -- there is no digit bitmap in
     * the weapon's own tag, only where to put the number and how many
     * digits it gets. */
    hta_font_digits digits;
    int32_t  number_elem[8];     /* most significant first */
    uint32_t number_count;
    float    number_cell;        /* canvas px per digit */
    float    number_base[2];     /* the tag's anchor offset */
    bool     number_leading_zeros;

    /* A black sheet over everything, for dying behind. Added last so it
     * draws last; -1 when there was no room for it. */
    int32_t  fade_elem;

    /* The motion tracker: the unit HUD's disc and view cone, the globals'
     * sweep ring and blip, and a pool of blip elements placed per frame. */
    bool     have_sensor;
    int32_t  sensor_elem[3];
    int32_t  blip_elem[HTA_HUD_MAX_BLIPS];
    float    blip_px;           /* the blip art's size, canvas px */
    float    sensor_origin[2];  /* the disc's top-left corner, canvas px */
    float    sensor_center[2];  /* offset of its centre within that, px */
    float    sensor_radius;     /* hud_globals `motion sensor scale`, px */
    float    sensor_scale;      /* hud_globals `hud scale in multiplayer` */
} hta_hud;

/* One contact on the motion tracker: x right and y forward, -1..1 of the
 * range; `size` scales the dot; `friendly` paints it yellow, not red. */
typedef struct {
    float x, y, size;
    bool  friendly;
} hta_hud_blip;
/* Place this frame's contacts. Beyond the edge they are not drawn. */
void hta_hud_set_blips(hta_hud *h, const hta_hud_blip *b, uint32_t n);

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
/* Rounds in the magazine over its capacity. */
void hta_hud_set_ammo(hta_hud *h, float fraction);

/* The rounds the counter shows. Harmless on a weapon with no number
 * element, and on a HUD whose font would not load. */
void hta_hud_set_number(hta_hud *h, int value);

/* Which zoom level the weapon is at, 0 for unzoomed. Shows that level's
 * scope furniture and hides every other level's. */
void hta_hud_set_zoom(hta_hud *h, int level);
/* Halo CE's crosshair goes red while it is on an enemy within the weapon's
 * autoaim range. No tag carries that red (the reticle overlay has a
 * default, a flashing and a disabled colour, none of them for a target),
 * so HTA_HUD_TARGET_COLOR is ours. */
#define HTA_HUD_TARGET_R 1.0f
#define HTA_HUD_TARGET_G 0.12f
#define HTA_HUD_TARGET_B 0.08f
void hta_hud_set_on_target(hta_hud *h, bool on);

/* Black over the whole screen at `alpha`, 0 for none. Halo fades out as you
 * die and back in as you respawn. */
void hta_hud_set_fade(hta_hud *h, float alpha);

/* The unit HUD's own sounds: the shield charging back up, the hit, the
 * warning tones.
 *
 * Halo keeps these on the `unhi` rather than on the biped, as a reflexive of
 * UnitHUDInterfaceHUDSound (56 bytes, which reconciles) at +960 -- a
 * definition walk puts it at 928 and drifts, so that offset is probed. Each
 * entry is a sound and a `latched to` bitfield naming the condition it plays
 * under. Blood Gulch's cyborg fills all five.
 *
 * Note the classes differ: the hit is a one-shot `snd!`, everything else is
 * an `lsnd` meant to run for as long as the condition lasts. */
#define HTA_HUDSND_SHIELD_RECHARGING  0x01u
#define HTA_HUDSND_SHIELD_DAMAGED     0x02u
#define HTA_HUDSND_SHIELD_LOW         0x04u
#define HTA_HUDSND_SHIELD_EMPTY       0x08u
#define HTA_HUDSND_HEALTH_LOW         0x10u

/* The tag a condition plays, or 0. `out_looping` says whether it is an
 * `lsnd` (hold it while the condition lasts) or a `snd!` (fire once). */
uint32_t hta_unit_hud_sound(const hta_cache *c, uint32_t latched_to,
                            bool *out_looping);

#endif
