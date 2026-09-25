/* Destructible props: crates, barrels, glass, walls that break.
 * Portable and game-agnostic.
 *
 * A prop is an oriented box with health and a material. While whole it
 * blocks: it owns a collision grid placed as an hta_collision_instance, the
 * same mechanism vehicles use, so players, bullets, grenades and debris
 * all respect it with no special cases. Its look is the caller's -- the
 * submeshes of an imported map, a model, or a plain textured box.
 *
 * Damage comes from blasts (with falloff) and from hits. At zero health it
 * BREAKS: its instance goes inactive, the caller hides its look (an event
 * says which), and it bursts into rigid chunks filling its volume, thrown
 * by whatever broke it, with dust, splinters, sparks or glass to match.
 * A prop can respawn after a delay, whole again.
 *
 * Breaking is decided by the HOST and replicated as a prop index; the
 * chunks are cosmetic and local, like gibs. */
#ifndef HTA_PROPS_H
#define HTA_PROPS_H

#include <stdbool.h>
#include <stdint.h>
#include "fx.h"
#include "player.h"
#include "rigid.h"

typedef struct {
    float    centre[3];
    float    half[3];
    float    yaw;               /* radians about +Z */
    uint8_t  material;          /* hta_rigid_material */
    float    health, max_health;
    float    respawn_time;      /* 0 = stays broken */
    float    respawn_in;
    bool     broken;
    bool     explosive;         /* a red barrel: breaking it is a blast */
    float    blast_damage, blast_radius;
    uint32_t user;              /* the caller's: which submeshes it owns */
    /* its own collision, placed in the world */
    hta_bsp_mesh     coll_mesh;
    hta_collision    coll;
    hta_collision_instance *inst;   /* into the props' instance array */
} hta_prop;

typedef enum { HTA_PROP_EV_BROKE = 1, HTA_PROP_EV_RESPAWNED, HTA_PROP_EV_EXPLODED } hta_prop_ev_kind;
typedef struct { hta_prop_ev_kind kind; uint32_t prop; float pos[3]; float damage, radius; } hta_prop_event;
#define HTA_PROP_MAX_EVENTS 32u

typedef struct {
    hta_prop *props;
    uint32_t  count, cap;
    /* Instances, one per prop, for the world grid's `instances` list. A
     * caller with vehicles merges both lists (see hta_props_instances). */
    hta_collision_instance *instances;
    hta_prop_event events[HTA_PROP_MAX_EVENTS];
    uint32_t event_count;
    uint32_t chunks_per_break;      /* scaled by the debris budget */
    /* A LAN client: the host decides what breaks and respawns. Damage
     * still chips and blasts still throw, but nothing breaks or comes
     * back here except through hta_props_set_broken. */
    bool remote;
    /* Changes whenever a prop is added, breaks or comes back; unique
     * across every hta_props (a reload never repeats an old value). */
    uint32_t version;
} hta_props;

bool hta_props_init(hta_props *p, uint32_t cap);
void hta_props_free(hta_props *p);

/* Adds a prop; returns its index or UINT32_MAX. Health 0 = the material's
 * default. */
uint32_t hta_props_add(hta_props *p, const float centre[3], const float half[3], float yaw,
                       hta_rigid_material material, float health, uint32_t user);

/* Damage one prop. `from` is where the damage came from (for the throw).
 * Returns true if this broke it. */
bool hta_props_damage(hta_props *p, uint32_t i, float amount, const float point[3],
                      const float from[3], hta_rigid_world *w, hta_fx *fx);
/* A blast: every prop inside `radius` takes falloff damage. Chains: an
 * explosive prop that breaks raises its own blast (as an event the caller
 * feeds back into the game -- and into this function). */
void hta_props_blast(hta_props *p, const float centre[3], float damage, float radius,
                     hta_rigid_world *w, hta_fx *fx);
/* First prop a ray hits within max_t: its index and t, or false. */
bool hta_props_ray(const hta_props *p, const float orig[3], const float dir[3], float max_t,
                   uint32_t *out_prop, float *out_t, float nrm[3]);

void hta_props_update(hta_props *p, float dt);
bool hta_props_pop(hta_props *p, hta_prop_event *out);

/* Replication. The host writes one bit per prop (bit i of byte i/8) into
 * `mask` and returns how many it wrote (<= max). A client applies the
 * host's mask: a prop the host broke shatters here with the same events
 * (BROKE, EXPLODED) a local break raises, one it restored respawns;
 * props past `count` are left alone. With no `w` and no `fx` it is a
 * quiet sync (joining a match in progress): BROKE and RESPAWNED only, no
 * chunks and no explosions. */
uint32_t hta_props_broken_mask(const hta_props *p, uint8_t *mask, uint32_t max);
void hta_props_apply_mask(hta_props *p, const uint8_t *mask, uint32_t count,
                          hta_rigid_world *w, hta_fx *fx);

/* A caller-owned merged instance list: vehicles first, then props.
 * Returns the count written (<= cap). */
uint32_t hta_props_instances(const hta_props *p, const hta_collision_instance *others, uint32_t n_others,
                             hta_collision_instance *out, uint32_t cap);

/* Default health per material (ours). */
float hta_props_default_health(hta_rigid_material m);

#endif
