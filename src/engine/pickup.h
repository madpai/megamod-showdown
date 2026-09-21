/* What is lying on the ground, and when it comes back.
 *
 * The map's own placements, run as a little state machine: each one holds an
 * item drawn from its collection's weights, hands it over when you walk onto
 * it, then counts down and draws again. That last part matters on the
 * pedestal in the middle of Blood Gulch, which is overshield or active
 * camouflage fifty-fifty and rolls afresh every time it comes back.
 *
 * Portable: no renderer, no audio, no platform. The caller asks what it has
 * walked onto, decides whether it wants it, and says so.
 */
#ifndef HTA_PICKUP_H
#define HTA_PICKUP_H

#include "../asset/items.h"
#include "../asset/bsp.h"
#include "../asset/bitmap.h"

/* How close you have to be, in world units. A world unit is 3.05 m, so this
 * is about a metre and a half -- Halo picks things up as you walk over them
 * rather than making you aim at them, and this is that reach. Ours: the tags
 * carry no pickup radius. */
#define HTA_PICKUP_REACH 0.5f

typedef struct {
    bool     present;      /* is there something to take right now */
    uint32_t choice;       /* which of the spawn's items it is */
    float    timer;        /* seconds until it comes back, when absent */
} hta_pickup_slot;

typedef struct {
    hta_item_spawn  spawn[HTA_ITEM_MAX_PLACEMENTS];
    hta_pickup_slot slot[HTA_ITEM_MAX_PLACEMENTS];
    uint32_t        count;
    uint32_t        rng;
    bool            loaded;

    /* One-shot, true for the update in which it happened. */
    int32_t         respawned;   /* slot index, -1 for none */

    /* What they look like lying there. Every placement's model is built
     * once at its own position; what changes is only which ones are
     * VISIBLE, so the geometry is fixed and the per-frame work is
     * collapsing the quads of whatever has been taken. */
    hta_bsp_mesh mesh;
    hta_vertex  *posed;          /* what the renderer reads */
    uint32_t     first_vertex[HTA_ITEM_MAX_PLACEMENTS];
    uint32_t     vertex_count[HTA_ITEM_MAX_PLACEMENTS];
    bool         have_mesh;
    bool         dirty;          /* something appeared or was taken */
} hta_pickups;

/* Reads the scenario's netgame equipment and puts everything out. */
bool hta_pickups_load(hta_pickups *p, const hta_cache *c);

/* Everything back on the ground, fresh draws. */
void hta_pickups_reset(hta_pickups *p);

/* Counts down whatever is missing. */
void hta_pickups_update(hta_pickups *p, float dt);

/* The nearest thing within reach of `pos`, or -1. Takes nothing: the caller
 * decides whether it wants it, because a full magazine or a full set of
 * grenades should leave the item where it is. */
int32_t hta_pickups_at(const hta_pickups *p, const float pos[3]);

/* The same, but only of one kind -- so "am I standing on a weapon" is one
 * call. */
int32_t hta_pickups_at_kind(const hta_pickups *p, const float pos[3],
                            hta_item_kind kind);

/* What is in that slot, or NULL. */
const hta_item_choice *hta_pickups_item(const hta_pickups *p, int32_t slot);

/* Take it. It goes away and starts counting down. */
void hta_pickups_take(hta_pickups *p, int32_t slot);

/* Build the geometry: every placement's model, at its own position and
 * facing. Only the item a placement STARTS with is built -- the weighted
 * pedestal shows whichever it first drew and keeps that shape, which is
 * wrong by a hair and cheap by a lot. */
bool hta_pickups_build(hta_pickups *p, const hta_cache *c,
                       const hta_resource_map *bitmaps,
                       char *err, size_t errlen);

/* Rewrite `posed`: present items where they belong, taken ones collapsed to
 * nothing.
 *
 * Only worth calling when something has actually been taken or come back.
 * Blood Gulch's items are 23,000 vertices; posing them every frame would be
 * 900 KB of upload a frame to make thirty-seven things that are not moving
 * carry on not moving. `dirty` says when it is needed. */
void hta_pickups_pose(hta_pickups *p);

/* True when the geometry has changed since the last pose. */
bool hta_pickups_dirty(const hta_pickups *p);

void hta_pickups_free(hta_pickups *p);

#endif
