/* Projectiles you can watch fly, for the weapons whose rounds are objects.
 *
 * Halo fires everything as a projectile, bullets included, but only two of
 * the Trial's carried weapons give theirs a MODEL: the rocket launcher (117
 * vertices) and the needler (10). Everything else is a particle or a tracer
 * and stays hitscan here -- a bullet at 324 world units a second crosses
 * Blood Gulch in under a third of a second anyway.
 *
 * The numbers are the projectile tag's own. Note the units: `initial
 * velocity` (proj+484) and `final velocity` (+488) are world units per
 * TICK, so a rocket's 0.4 is 12 wu/s and the sniper's 33.3 is 1000. Range
 * (+456) is in world units and the detonation timer (+444) in seconds.
 *
 * Geometry: one dynamic mesh holding HTA_PROJ_MAX copies of the model, built
 * when the weapon is equipped and re-posed on the CPU each frame -- the same
 * trick the muzzle flash uses. An idle copy is collapsed to zero area rather
 * than removed, so the index buffer never changes.
 */
#ifndef HTA_PROJECTILE_H
#define HTA_PROJECTILE_H

#include "../asset/bsp.h"
#include "../asset/cache.h"
#include "../asset/weapon.h"
#include "player.h"

#define HTA_PROJ_MAX 8

/* Halo keeps projectile velocities per tick, at 30 ticks a second. */
#define HTA_TICKS_PER_SECOND 30.0f

typedef struct {
    float pos[3];
    float dir[3];        /* unit */
    float travelled;     /* world units, for the speed ramp and the range */
    float age;           /* seconds, for the detonation timer */
    float fall;          /* downward speed picked up from gravity, wu/s */
    bool  alive;
} hta_projectile;

typedef struct {
    hta_projectile live[HTA_PROJ_MAX];

    /* The equipped weapon's projectile, or 0 when it has no model to draw. */
    uint32_t proj_tag_id;
    float    speed_initial, speed_final;   /* wu/s */
    float    range;                        /* world units; 0 = unlimited */
    float    timer;                        /* seconds; 0 = none */
    float    gravity_scale;

    /* One mesh, HTA_PROJ_MAX copies of the model. `base` is the model in its
     * own space; `mesh.vertices` is what the renderer sees. */
    hta_bsp_mesh mesh;
    hta_vertex  *base;
    uint32_t     verts_each;
    bool         loaded;

    /* What its detonation sounds like and how wide a mark it leaves, both
     * from the projectile's own detonation effect. Either may be absent:
     * a needle pops, a bullet does neither. */
    uint32_t detonation_snd;
    float    blast_radius;
    uint32_t decal_id;       /* `deca` the blast leaves, 0 if none */
    uint32_t det_effect;     /* the `effe` itself, for its particles */
    /* What its blast does, from the detonation's own damage effect. */
    float    blast_damage;
    float    blast_damage_radius;
    float    blast_core;

    /* Where the last projectile went off, for the caller's impact sound and
     * scorch. Valid for the update that set `detonated`. */
    bool     detonated;
    float    hit[3], hit_normal[3];
    uint8_t  hit_material;
} hta_projectiles;

void hta_projectiles_init(hta_projectiles *p);
void hta_projectiles_free(hta_projectiles *p);

/* Load the weapon's projectile. False (and everything cleared) when the
 * weapon has no projectile or its projectile has no model -- that is not an
 * error, it is most of the roster. */
bool hta_projectiles_equip(hta_projectiles *p, const hta_cache *c,
                           const hta_resource_map *bitmaps,
                           const hta_weapon_def *weap, char *err, size_t errlen);

/* Launch one. Ignored when the weapon has no drawable projectile. */
void hta_projectiles_fire(hta_projectiles *p, const float origin[3],
                          const float dir[3]);

/* Fly, collide and expire. Sets `detonated` for the update in which one
 * went off. Re-poses the mesh, so the caller re-uploads the vertices. */
void hta_projectiles_update(hta_projectiles *p, const hta_collision *col, float dt);

/* How many are in the air. */
uint32_t hta_projectiles_count(const hta_projectiles *p);

#endif
