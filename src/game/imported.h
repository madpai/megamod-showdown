/* Imported characters and weapons in a match: which clip a body plays for
 * the state the game is in, and where it holds a weapon.
 *
 * Holding follows Source's own rule, bone-merge: a weapon model shares a
 * bone name with the body's hand (ValveBiped.weapon_bone on CS:S and HL2
 * models), and the two are made to coincide. A body without that bone holds
 * it at its right-hand attachment instead. Nothing here names a model.
 *
 * Portable: matrices and names, no renderer.
 */
#ifndef HTA_IMPORTED_H
#define HTA_IMPORTED_H

#include "../asset/oal_asset.h"
#include <stdbool.h>

/* The weapon model's space to the world, for `weapon` held by `body` posed
 * as `world` and placed by `root` (3x4 row-major). False when the body has
 * nothing to hold with. */
bool hta_imported_hold_matrix(const hta_oal_model *body, const float (*world)[12],
                              const float root[12], const hta_oal_model *weapon, float out[12]);

/* The body's hand frame in the world (3x4), for holding a Halo weapon. */
bool hta_imported_hand_frame(const hta_oal_model *body, const float (*world)[12],
                             const float root[12], float out[12]);

/* Crossing between the two weapon conventions.
 *
 * A Source world model points its barrel along -Y with +Z up, and its
 * ValveBiped.weapon_bone marks the grip (axes rows 1 0 0 / 0 0 -1 / 0 1 0
 * in model space; measured on CS:S's w_ models). A Halo weapon model is
 * held at the biped's "right hand" marker and points along +X, +Z up.
 *
 * hta_imported_source_in_halo_hand: an imported weapon's world model into
 * the space of a Halo weapon (multiply the Halo hand matrix by it).
 * hta_imported_halo_in_source_hand: a Halo weapon model held by an
 * imported body, world matrix out. */
void hta_imported_source_in_halo_hand(const hta_oal_model *weapon, float out[12]);
bool hta_imported_halo_in_source_hand(const hta_oal_model *body, const float (*world)[12],
                                      const float root[12], float out[12]);

/* The role an imported body plays for a Halo animation base name such as
 * "crouch rifle move-left" or "stand pistol airborne". */
const char *hta_imported_role(const char *halo_base);

#endif
