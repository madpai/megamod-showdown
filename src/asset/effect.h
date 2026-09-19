/* `effe` tags: what a weapon actually does when it goes off.
 *
 * Halo hangs almost nothing off the weapon itself. The gunshot, the muzzle
 * flash, the smoke and the ejected casing are all parts and particles of the
 * trigger's firing effect, attached to named markers on the model.
 *
 * Layouts from Invader: Effect 64, EffectEvent 68, EffectPart 104,
 * EffectParticle 232, EffectLocation 32 (a marker name and nothing else).
 * All five reconcile, which is the check that catches a mis-ordered field.
 *
 * Which particle is "the" first-person muzzle flash is not guesswork and
 * needs no matching on tag paths -- the tag says so four ways over. See
 * hta_effect_fp_flash.
 */
#ifndef HTA_EFFECT_H
#define HTA_EFFECT_H

#include "cache.h"

/* EffectParticle "create in": which environment it belongs to. */
#define HTA_FX_IN_ANY    0u
#define HTA_FX_IN_AIR    1u
#define HTA_FX_IN_WATER  2u
#define HTA_FX_IN_SPACE  3u

/* EffectParticle "create": which camera it belongs to. */
#define HTA_FX_CAM_ANY        0u
#define HTA_FX_CAM_FIRST      1u
#define HTA_FX_CAM_THIRD      2u
#define HTA_FX_CAM_FIRST_PREF 3u

/* Particle "framebuffer blend function" -- only the two we care about. */
#define HTA_FX_BLEND_ALPHA 0u
#define HTA_FX_BLEND_ADD   3u

typedef struct {
    uint32_t part_id;        /* the `part` tag */
    uint32_t bitmap_id;      /* its `bitm` */
    char     marker[32];     /* the model marker it is attached to */
    float    radius_min, radius_max;
    float    lifespan;       /* seconds */
    float    fade_in, fade_out;
    uint8_t  blend;          /* HTA_FX_BLEND_* */
    uint8_t  orientation;    /* 0 screen facing, 1 parallel to direction */
} hta_effect_particle;

/* The first `snd!` this effect plays, or 0. */
uint32_t hta_effect_first_sound(const hta_cache *c, uint32_t effect_tag_id);

/* The first-person muzzle flash, if this effect has one.
 *
 * Selected by what the tag declares, not by its name: the particle must be
 * created in air (or anywhere), for the first-person camera (or either),
 * attached to `marker_name`, and its `part` must blend ADDITIVELY. On the
 * Trial's assault rifle exactly one particle satisfies all four -- the one
 * whose bitmap is literally called "flash h ar fp".
 */
bool hta_effect_fp_flash(const hta_cache *c, uint32_t effect_tag_id,
                         const char *marker_name, hta_effect_particle *out);

#endif
