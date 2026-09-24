/* A match on a map that is not Halo's.
 *
 * An imported map (Open Asset Lab's .oalmap) brings only a world: triangles,
 * textures and player starts. Everything else a match needs -- the weapons
 * lying about, the flag stands -- still comes from Blood Gulch's scenario,
 * and here it is moved onto the new ground.
 *
 * Where things go is ours, not any tag's: the scenario's equipment is spread
 * over the walkable map as evenly as the nav grid allows, each flag stands
 * among its own team's starts, and a start whose team the map does not say
 * is shared by both teams.
 *
 * Portable: nav grid and game in, positions out. No renderer, no platform.
 */
#ifndef HTA_EXTERNAL_WORLD_H
#define HTA_EXTERNAL_WORLD_H

#include "game.h"
#include "nav.h"
#include "../engine/pickup.h"

/* An imported map's starts are already on the floor (Asset Lab grounds
 * them); snapping from higher can land on a ceiling's top (cs_office) or a
 * roof (de_dust2). A step's height is enough. Ours. */
#define HTA_EXTERNAL_SPAWN_LIFT 0.1f

/* The nodes a body can walk to from a start in the main region AND walk
 * back from, following links as they go (a drop is one-way). Regions are undirected, so a
 * region can hold ledges you drop into and never leave; farthest-point
 * placement finds exactly those. One byte per node, malloc'd; free it.
 * NULL on failure. `out_count` (may be NULL) gets how many are playable. */
uint8_t *hta_nav_playable(const hta_nav *n, const hta_spawn_point *spawns, uint32_t count,
                          uint32_t *out_count);

/* `count` points on the playable nodes (`mask`; NULL means the main
 * region), each as far as it can be from the ones before it and from
 * `seed` (may be NULL). Deterministic, so every phone in a match puts
 * things in the same places. Returns how many were found. */
uint32_t hta_nav_spread(const hta_nav *n, const uint8_t *mask, const float (*seed)[3], uint32_t seeds,
                        float (*out)[3], uint32_t count);

/* The largest connected piece of an imported map can be its rooftops
 * (de_dust2's are), which nobody can reach. Make the piece holding most of
 * the starts the map instead. Returns false when no start is on the grid. */
bool hta_nav_main_from_spawns(hta_nav *n, const hta_spawn_point *spawns, uint32_t count);

/* Move the scenario's equipment onto the nav grid. Call before
 * hta_pickups_build: the geometry is built where the items stand. */
void hta_pickups_relocate(hta_pickups *p, const hta_nav *n, const uint8_t *mask);

/* The imported map's starts replace the scenario's, and each team's flag
 * stands at the walkable spot nearest its starts' middle. CTF stays
 * unavailable when the map names no team starts. */
void hta_game_use_external(hta_game *g, const hta_spawn_point *spawns, uint32_t count,
                           const hta_nav *n, const uint8_t *mask);

/* A team's flag where the map itself puts it (the walkable spot nearest
 * `at`), in place of its starts' middle. */
void hta_game_external_flag(hta_game *g, int team, const float at[3],
                            const hta_nav *n, const uint8_t *mask);

#endif
