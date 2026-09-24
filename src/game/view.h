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
#include "../asset/oal_asset.h"

typedef struct {
    hta_actor actor[HTA_GAME_MAX_UNITS];
    bool      dying[HTA_GAME_MAX_UNITS];
    bool      shown[HTA_GAME_MAX_UNITS];    /* draw this unit's body */
    char      base_clip[HTA_GAME_MAX_UNITS][48];
    float     action_left[HTA_GAME_MAX_UNITS];
    int32_t   hand_node;
    float     hand_offset[3];

    /* One mesh per roster weapon, in the weapon's own space. An imported
     * weapon's is its package's world model, borrowed, not owned. */
    hta_bsp_mesh weapon_mesh[HTA_GAME_MAX_WEAPONS];
    bool      have_weapon[HTA_GAME_MAX_WEAPONS];
    bool      borrowed[HTA_GAME_MAX_WEAPONS];

    /* Imported bodies (a unit whose character >= 0): this frame's skinned
     * vertices, bones and placement, and the clip it is in. */
    hta_vertex *oal_posed[HTA_GAME_MAX_UNITS];
    uint32_t  oal_capacity[HTA_GAME_MAX_UNITS];
    float     oal_world[HTA_GAME_MAX_UNITS][HTA_OAL_MAX_BONES][12];
    float     oal_root[HTA_GAME_MAX_UNITS][12];
    int32_t   oal_clip[HTA_GAME_MAX_UNITS];
    float     oal_time[HTA_GAME_MAX_UNITS], oal_attack[HTA_GAME_MAX_UNITS];
    int8_t    oal_char[HTA_GAME_MAX_UNITS];
    /* The flag's mesh is its pole, then a red cloth and a blue one: the
     * pole's submesh count, and each cloth's submesh (0 when there is none). */
    uint32_t  flag_pole_submeshes;
    uint32_t  flag_cloth[2];
    uint32_t  rng;
    bool      loaded;
} hta_game_view;

typedef struct {
    int32_t weapon;       /* roster index: which weapon_mesh */
    float   model[16];    /* column-major, weapon space to world */
    uint32_t first_submesh, submesh_count;   /* part of it, or 0/0 for all */
} hta_game_held_weapon;

/* The parts of the flag's mesh to draw for `team`'s flag: the pole, and its
 * cloth. Writes up to two ranges; returns how many. */
uint32_t hta_game_view_flag_parts(const hta_game_view *v, int team,
                                  uint32_t first[2], uint32_t count[2]);

/* Loads a body for each of the first `units` slots and every roster
 * weapon's third-person model. */
bool hta_game_view_load(hta_game_view *v, const hta_game *g,
                        const hta_resource_map *bitmaps, uint32_t units,
                        char *err, size_t errlen);
void hta_game_view_free(hta_game_view *v);

/* Animate and pose every unit that is not `skip` (the local player, whose
 * body is the camera). Call after hta_game_update. */
void hta_game_view_update(hta_game_view *v, const hta_game *g, int32_t skip, float dt);

/* What to draw for a unit's body: its mesh (for the upload) and this
 * frame's posed vertices. An imported character when the unit wears one,
 * the cyborg otherwise. */
const hta_bsp_mesh *hta_game_view_body_mesh(const hta_game_view *v, const hta_game *g, uint32_t unit);
const hta_vertex *hta_game_view_body_vertices(const hta_game_view *v, const hta_game *g, uint32_t unit);

/* A weapon model drawn by a Halo weapon-space matrix (a drop on the
 * ground, a Halo hand): for an imported weapon, right-multiply by its
 * Source-to-Halo convention. `model` is column-major 4x4, in place. */
void hta_game_view_weapon_space(const hta_game *g, int32_t weapon, float model[16]);

/* The weapons in hands this frame. Returns how many were written. */
uint32_t hta_game_view_weapons(const hta_game_view *v, const hta_game *g, int32_t skip,
                               hta_game_held_weapon *out, uint32_t max);

#endif
