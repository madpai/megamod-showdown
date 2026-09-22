/* What the other players look like: a skinned cyborg per unit, playing the
 * clip its state calls for, with the weapon it is holding in its hand.
 *
 * Bodies are CPU-skinned `hta_actor`s, one per unit, because each is posed
 * differently. Weapons are rigid, so each weapon model is built ONCE, in its
 * own space, and drawn per holder at the `right hand` marker through the
 * renderer's instanced path -- one matrix a frame instead of a thousand
 * vertices. The dead keep their body where they fell, playing one of the
 * cyborg's kill clips, until they come back.
 *
 * Portable: meshes and matrices out, the platform uploads and draws them.
 */
#ifndef HTA_GAME_VIEW_H
#define HTA_GAME_VIEW_H

#include "game.h"
#include "../engine/actor.h"

typedef struct {
    hta_actor actor[HTA_GAME_MAX_UNITS];
    bool      dying[HTA_GAME_MAX_UNITS];
    bool      shown[HTA_GAME_MAX_UNITS];    /* draw this unit's body */
    char      base_clip[HTA_GAME_MAX_UNITS][48];
    float     action_left[HTA_GAME_MAX_UNITS];
    int32_t   hand_node;
    float     hand_offset[3];

    /* One mesh per roster weapon, in the weapon's own space. */
    hta_bsp_mesh weapon_mesh[HTA_GAME_MAX_WEAPONS];
    bool      have_weapon[HTA_GAME_MAX_WEAPONS];
    uint32_t  rng;
    bool      loaded;
} hta_game_view;

typedef struct {
    int32_t weapon;       /* roster index: which weapon_mesh */
    float   model[16];    /* column-major, weapon space to world */
} hta_game_held_weapon;

/* Loads a body for each of the first `units` slots and every roster
 * weapon's third-person model. */
bool hta_game_view_load(hta_game_view *v, const hta_game *g,
                        const hta_resource_map *bitmaps, uint32_t units,
                        char *err, size_t errlen);
void hta_game_view_free(hta_game_view *v);

/* Animate and pose every unit that is not `skip` (the local player, whose
 * body is the camera). Call after hta_game_update. */
void hta_game_view_update(hta_game_view *v, const hta_game *g, int32_t skip, float dt);

/* The weapons in hands this frame. Returns how many were written. */
uint32_t hta_game_view_weapons(const hta_game_view *v, const hta_game *g, int32_t skip,
                               hta_game_held_weapon *out, uint32_t max);

#endif
