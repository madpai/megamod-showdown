/* First-person weapon from Trial `weap` tags.
 *
 * Offsets were checked against Invader's weapon.json with `bounds` fields
 * counted as two values -- "zoom magnification range" is one, which is why
 * everything from 0x3DC on sits 4 bytes later than a naive field walk says.
 * The AR reading 15 shots/s and the pistol 3.5 is the confirmation.
 */
#ifndef HTA_WEAPON_H
#define HTA_WEAPON_H

#include "cache.h"
#include "bsp.h"
#include "bitmap.h"

#define HTA_TAG_EFFE HTA_FOURCC('e','f','f','e')
#define HTA_TAG_PROJ HTA_FOURCC('p','r','o','j')
#define HTA_TAG_SND  HTA_FOURCC('s','n','d','!')

/* ---- Weapon ---- */
/* Weapon inherits Item inherits Object, so its own fields start at 776 and
 * the HUD interface sits 376 in. Do NOT find a weapon's `wphi` by matching
 * tag paths: the rocket launcher's is "rocket_launcher" and the
 * flamethrower's is "flame thrower". */
#define HTA_WEAP_HUD_INTERFACE 1152u /* TagDependency -> wphi */
#define HTA_WEAP_ZOOM_LEVELS   986u  /* int16: 0 means this weapon cannot zoom */
#define HTA_WEAP_ZOOM_MAG      988u  /* two floats: magnification at the first
                                      * level and at the last */
#define HTA_WEAP_FP_MODEL     0x45Cu  /* TagDependency -> mod2 */
#define HTA_WEAP_FP_ANIM      0x46Cu  /* TagDependency -> antr */
#define HTA_WEAP_HUD          0x480u  /* TagDependency -> wphi */
#define HTA_WEAP_PICKUP_SND   0x490u  /* TagDependency -> snd! */
#define HTA_WEAP_ZOOM_IN_SND  0x4A0u
#define HTA_WEAP_ZOOM_OUT_SND 0x4B0u
#define HTA_WEAP_MAGAZINES    0x4F0u  /* TagReflexive */
#define HTA_WEAP_TRIGGERS     0x4FCu  /* TagReflexive */

/* ---- WeaponMagazine ---- */
#define HTA_MAG_SIZE             112u
#define HTA_MAG_ROUNDS_INITIAL   6u    /* int16 */
#define HTA_MAG_ROUNDS_RESERVE   8u    /* int16 */
#define HTA_MAG_ROUNDS_LOADED    10u   /* int16 */
#define HTA_MAG_RELOAD_TIME      20u   /* float, seconds */
#define HTA_MAG_ROUNDS_RELOADED  24u   /* int16 */
#define HTA_MAG_CHAMBER_TIME     28u   /* float */
#define HTA_MAG_RELOADING_FX     56u   /* TagDependency -> effe */
#define HTA_MAG_CHAMBERING_FX    72u   /* TagDependency -> effe */

/* ---- WeaponTrigger ---- */
#define HTA_TRIG_SIZE         276u
#define HTA_TRIG_ROF          4u    /* two floats: initial, final shots/sec */
#define HTA_TRIG_ROUNDS_SHOT  34u   /* int16 */
#define HTA_TRIG_PROJ_SHOT    110u  /* int16 */
#define HTA_TRIG_BETWEEN_CONT 38u   /* int16: projectiles between contrails */
#define HTA_TRIG_ERROR_ACCEL  56u   /* float, seconds to bloom to the max cone */
#define HTA_TRIG_ERROR_DECEL  60u   /* float, seconds to settle back */
#define HTA_TRIG_MIN_ERROR    120u  /* Angle */
#define HTA_TRIG_ERROR_ANGLE  124u  /* two Angles: initial, final (radians) */
#define HTA_TRIG_FP_OFFSET    136u  /* Point3D: projectile spawn, NOT the hold */
#define HTA_TRIG_PROJECTILE   148u  /* TagDependency -> proj */
#define HTA_TRIG_FIRING_FX    264u  /* TagReflexive */

/* ---- WeaponTriggerFiringEffect ---- */
#define HTA_FIREFX_SIZE    132u
#define HTA_FIREFX_FIRING  36u   /* TagDependency -> effe */
#define HTA_FIREFX_EMPTY   68u   /* TagDependency -> effe */
#define HTA_FIREFX_DAMAGE  84u   /* TagDependency -> jpt! */

/* ---- Globals -> first person interface ---- */
#define HTA_MATG_FP_INTERFACE 0x17Cu  /* TagReflexive */
#define HTA_FPI_SIZE          192u
#define HTA_FPI_HANDS         0u      /* TagDependency -> mod2 */

typedef struct {
    uint32_t hud_interface_id; /* `wphi`, 0 if none (the ball and the flag) */
    uint32_t fp_model_id;    /* the weapon mesh: gun only, no arms */
    uint32_t fp_anim_id;     /* antr: the merged hands+gun skeleton */
    uint32_t pickup_snd_id, zoom_in_snd_id, zoom_out_snd_id;
    /* Only the pistol, the rocket launcher and the sniper zoom in the
     * Trial, and only the sniper has two levels. Everything else is 0. */
    int      zoom_levels;
    float    zoom_mag[2];    /* magnification at the first and last level */

    float    rof;            /* shots per second (final) */
    float    cooldown;       /* 1/rof */
    /* Halo does not bloom the reticle; it widens the SHOT cone while the
     * trigger is held. `error` runs 0..1, reaching 1 after error_accel
     * seconds of fire and returning to 0 over error_decel, and the cone
     * half-angle lerps between these two. */
    float    error_angle[2]; /* radians, initial -> final */
    float    error_accel, error_decel;   /* seconds */
    float    min_error;      /* radians */
    int      rounds_per_shot;
    int      projectiles_per_shot;
    /* Trigger +38 `projectiles between contrails`: the rifle's 3 means one
     * round in four draws its tracer. 0 means every round does. */
    int      between_contrails;
    uint32_t projectile_id;
    uint32_t firing_fx_id, empty_fx_id, firing_damage_id;

    /* magazine 0 */
    int      rounds_loaded_max, rounds_reserve_max, rounds_initial, rounds_reloaded;
    float    reload_time, chamber_time;
    uint32_t reloading_fx_id;

    /* Projectile spawn offset from the trigger (+X fwd, +Y left, +Z up).
     * The Trial leaves this (0,0,0) for the AR and the pistol: it is NOT
     * where the viewmodel is held. The hold comes from the animation, which
     * parents `frame gun` to `frame r wriste`. */
    float    fp_offset[3];
    char     path[96];

    /* Which trigger of the weapon this describes, and how many it has.
     * Vehicle guns have two: the Scorpion's cannon and machine gun, the
     * Banshee's bolts and fuel rod. */
    uint32_t trigger, trigger_count;
    uint32_t trigger_flags;  /* WeaponTriggerFlags */
    int      magazine;       /* the trigger's magazine index, -1 for none */
    uint32_t mag_flags;      /* WeaponMagazineFlags of that magazine */
    float    rof_initial;    /* shots per second when the trigger is first held */
    float    rof_accel;      /* seconds to spin up to `rof` */
    bool     single_shot;    /* rate of fire 0: one round per pull */
} hta_weapon_def;

/* WeaponTriggerFlags bit 3, WeaponMagazineFlags bit 1. */
#define HTA_TRIGGER_NO_REPEAT       (1u << 3)
#define HTA_MAG_CHAMBER_EACH_ROUND  (1u << 1)

/* The same, for trigger `trigger` and ITS magazine rather than the first. */
bool hta_weapon_load_trigger(const hta_cache *c, uint32_t weap_tag_id, uint32_t trigger,
                             hta_weapon_def *def);

/* One specific weapon by tag id. */
bool hta_weapon_load_id(const hta_cache *c, const hta_resource_map *bitmaps,
                        uint32_t weap_tag_id,
                        hta_weapon_def *def, hta_bsp_mesh *fp,
                        char *err, size_t errlen);

/* Tag ids of every weapon a player could FIGHT with: one with a
 * first-person model, first-person animations and a HUD interface. The
 * last of those is what rules out the ball and the flag, which are held in
 * first person but carried rather than fired.
 *
 * Also skipped: weapons whose first-person model carries its own arms
 * rather than wearing the globals hands (the unfinished fuel rod gun), and
 * a weapon that shares a first-person model and animation graph with one
 * already listed (`mp_needler` behind `needler`). Returns how many were
 * written. */
uint32_t hta_weapon_list_playable(const hta_cache *c, uint32_t *out, uint32_t max);

/* Prefers assault rifle, then pistol. Loads the FP mesh into `fp` if given --
 * bind pose only; the animated viewmodel goes through hta_viewmodel_load. */
bool hta_weapon_load_default(const hta_cache *c, const hta_resource_map *bitmaps,
                             hta_weapon_def *def, hta_bsp_mesh *fp,
                             char *err, size_t errlen);

/* `matg` -> first person interface -> first person hands (the cyborg arms).
 * Returns 0 if absent. */
uint32_t hta_globals_fp_hands(const hta_cache *c);

#endif
