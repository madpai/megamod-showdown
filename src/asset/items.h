/* What the map puts on the ground, and when it comes back.
 *
 * Blood Gulch places 37 of these: four shotguns, the rifles and plasma
 * rifles by each base, pistols, two sniper rifles, a rocket launcher in the
 * middle, the flamethrower, the plasma cannon, grenades by the doors, health
 * packs, and the overshield and active camouflage.
 *
 * Halo keeps them in the SCENARIO as `netgame equipment`, not as scenery:
 * a position, a facing, a respawn time and an `itmc` item collection. The
 * collection is a weighted list, which is how one pedestal in the middle of
 * the map gives you either the overshield or active camouflage, fifty-fifty.
 *
 * Scenario reconciles at 1456, ScenarioNetgameEquipment at 144,
 * ItemCollection at 92 and ItemCollectionPermutation at 84 -- all four,
 * which is the check that these offsets are right.
 */
#ifndef HTA_ITEMS_H
#define HTA_ITEMS_H

#include "cache.h"

#define HTA_ITEM_MAX_PLACEMENTS 64u
#define HTA_ITEM_MAX_CHOICES     8u

/* What picking one up does.
 *
 * A `weap` is a weapon and there is nothing to decide. An `eqip` says what
 * it is in its own `powerup type` field -- NOT in its path, which lies: the
 * overshield's model is `powerups\active camoflage\active camoflage` and
 * the camouflage's is the overshield's, swapped in Bungie's own tags. */
typedef enum {
    HTA_ITEM_NONE = 0,
    HTA_ITEM_WEAPON,
    HTA_ITEM_GRENADE,
    HTA_ITEM_HEALTH,
    HTA_ITEM_OVERSHIELD,
    HTA_ITEM_CAMOUFLAGE,
    HTA_ITEM_SPEED,        /* double speed; the Trial places none */
    HTA_ITEM_VISION        /* full-spectrum vision; likewise */
} hta_item_kind;

/* Equipment `grenade type`. */
#define HTA_GRENADE_FRAG   0u
#define HTA_GRENADE_PLASMA 1u

typedef struct {
    uint32_t      tag_id;      /* the `weap` or `eqip` */
    hta_item_kind kind;
    float         weight;      /* the collection's, for the weighted draw */
    /* Equipment only, and all the tag's own: how long a powerup lasts, which
     * grenade it is, the sound picking it up makes, and what it looks like
     * lying on the ground. */
    float         powerup_time;
    uint32_t      grenade_type;
    uint32_t      pickup_snd;
    uint32_t      model_id;
    char          path[96];
} hta_item_choice;

typedef struct {
    float    position[3];
    float    facing;           /* radians */
    float    respawn;          /* seconds; what the tags say, resolved */
    uint32_t collection_id;

    hta_item_choice choice[HTA_ITEM_MAX_CHOICES];
    uint32_t        choice_count;
} hta_item_spawn;

/* Reads the scenario's netgame equipment. Returns how many were read. */
uint32_t hta_scenario_items(const hta_cache *c, hta_item_spawn *out,
                            uint32_t max);

/* Draw one of a spawn's choices, by the collection's own weights. `rng` is
 * advanced. Returns an index into `choice`, or 0 when there is nothing. */
uint32_t hta_item_pick(const hta_item_spawn *s, uint32_t *rng);

/* You carry TWO. That is not a number of ours: the scenario's player
 * starting profile has a `primary weapon` and a `secondary weapon` and
 * nowhere to put a third, and the map's starting equipment hands out
 * exactly two collections. */
#define HTA_CARRY_MAX 2u

/* What the map starts you holding.
 *
 * Blood Gulch has NO player starting profile -- in multiplayer the loadout
 * belongs to the gametype, which does not ship inside a map -- but it does
 * carry `starting equipment` (Scenario +912, 204 bytes), and its first block
 * names two item collections: the assault rifle and the pistol. The campaign
 * map's profile agrees, down to the magazines: AR 60/240, pistol 12/72.
 *
 * Fills `out` with weapon tag ids in order and returns how many. */
uint32_t hta_scenario_starting_weapons(const hta_cache *c, uint32_t *out,
                                       uint32_t max);

/* What an item tag is, from its class and path. Exposed because the
 * platform decides what a pickup DOES from this. */
hta_item_kind hta_item_kind_of(const hta_cache *c, uint32_t tag_id,
                               char *out_path, size_t pathlen);

/* The same, filling in the equipment detail as well. `out` may be NULL. */
hta_item_kind hta_item_describe(const hta_cache *c, uint32_t tag_id,
                                char *out_path, size_t pathlen,
                                hta_item_choice *out);

#endif
