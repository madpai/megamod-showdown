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
    /* How many Halo spawns and how fast it throws them. A muzzle flash is
     * one sprite sitting still; an explosion is a dozen thrown outwards. */
    int16_t  count_min, count_max;
    float    speed_min, speed_max;      /* world units a second */
    float    spread;                    /* velocity cone half-angle, radians */
    uint16_t create_in, create;         /* HTA_FX_IN_* / HTA_FX_CAM_* */
    /* From the `pphy` the particle's `part` names (PointPhysics, 64, which
     * reconciles). Smoke floats and passes through walls; a spent casing
     * falls and bounces off them. */
    float    gravity;                   /* air gravity scale, 0 = floats */
    float    drag;                      /* air friction */
    float    elasticity;                /* bounce, 0 = sticks */
    bool     collides;                  /* "collides with structures" */
} hta_effect_particle;

/* Walk every particle an effect spawns, in order. `index` runs 0 .. the
 * return of hta_effect_particle_count. Unlike hta_effect_fp_flash this
 * filters nothing: a caller that is spawning the whole burst wants the
 * smoke and the sparks as well as the flash. */
uint32_t hta_effect_particle_count(const hta_cache *c, uint32_t effect_tag_id);
bool hta_effect_particle_at(const hta_cache *c, uint32_t effect_tag_id,
                            uint32_t index, hta_effect_particle *out);

/* One track of a looping sound (`lsnd`). */
typedef struct {
    uint32_t start, loop, end;   /* `snd!` ids, 0 where the track has none */
    float    gain;
} hta_loop_sound;

/* The looping sound attached to an object's marker, if it has one.
 *
 * Halo hangs continuous sounds off the OBJECT, not the firing effect: the
 * flamethrower's firing effect has no sound at all, and its roar is an
 * `lsnd` attached to `primary trigger`, scaled by the trigger's function.
 * Object attachments are at object+320, 72 bytes each; `lsnd` keeps its
 * tracks at +60, 160 bytes each. Both structs reconcile.
 *
 * The first `lsnd` on that marker wins. That is the firing sound on the
 * flamethrower; on the plasma pistol the same marker carries the overcharge
 * whine, which is why callers should only reach for this when the weapon's
 * own firing effect has no sound of its own.
 */
bool hta_object_loop_sound(const hta_cache *c, uint32_t object_tag_id,
                           const char *marker, hta_loop_sound *out);

/* Any attachment of a given class on a marker. The flamethrower's jet is
 * two `pctl` particle systems on `spawn fire`, attached the same way its
 * roar is an `lsnd` on `primary trigger`. Returns 0 when there is none. */
uint32_t hta_object_attachment(const hta_cache *c, uint32_t object_tag_id,
                               const char *marker, uint32_t want_class);

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

/* The blast of a detonation: the widest particle this effect actually
 * spawns that adds light to the frame, whatever camera it is meant for.
 *
 * Same scoring as hta_effect_fp_flash and for the same reason -- one quad
 * standing in for a burst -- but without the first-person filter, because
 * an explosion is a thing in the world. Returns false when the effect has
 * no additive particle, which is most of them. */
bool hta_effect_blast(const hta_cache *c, uint32_t effect_tag_id,
                      hta_effect_particle *out);

/* What a detonation leaves behind. Halo hangs the bang and the scorch off
 * the effect's PARTS, beside the damage, the light and the particle system:
 * a rocket's explosion names `frag grenade\expl` and the `grenade char`
 * decal. Either output may come back 0 -- a bullet impact has no decal of
 * its own -- and the radius is the decal tag's own, so an explosion marks
 * the wall the size Halo says rather than the size of a bullet hole. */
bool hta_effect_detonation(const hta_cache *c, uint32_t effect_tag_id,
                           uint32_t *out_sound, float *out_decal_radius,
                           uint32_t *out_decal);

/* The bitmap a `deca` draws with (decal+216). */
uint32_t hta_decal_bitmap(const hta_cache *c, uint32_t decal_tag_id);

/* The area damage a detonation does: how far it reaches, the core inside
 * which it does full damage, and how much that is. From the first `jpt!`
 * among the effect's parts -- a rocket's explosion carries two, the blast
 * and the shock wave, and the blast comes first.
 *
 * DamageEffect reconciles at 672: radius bounds at +0, area-of-effect core
 * radius at +460, damage bounds at +464. */
bool hta_effect_damage(const hta_cache *c, uint32_t effect_tag_id,
                       float *out_radius, float *out_core, float *out_damage);

/* `foot` (material_effects): the sound this biped makes stepping on that
 * MaterialType. Group 0 is the walk set. Returns 0 when the material has no
 * sound of its own -- plenty do not, and silence is the right answer. */
uint32_t hta_material_effect_sound(const hta_cache *c, uint32_t foot_tag_id,
                                   uint32_t group, uint8_t material);

/* What a projectile sounds like hitting that MaterialType. Halo keeps one
 * response per material on the projectile itself, each naming an `effe`,
 * and the sound is that effect's. Returns 0 when the material has no
 * response -- a shield or rubber makes no impact noise. */
uint32_t hta_projectile_impact_sound(const hta_cache *c, uint32_t projectile_id,
                                     uint8_t material);

/* The `effe` this projectile plays on that MaterialType, or 0. The sound
 * above is that effect's; the decal it leaves is in the same place. */
uint32_t hta_projectile_response_effect(const hta_cache *c, uint32_t projectile_id,
                                        uint8_t material);

#endif
