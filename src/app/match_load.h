/* Loading a match without a GPU (docs/DESKTOP_AGENT.md step 3; the
 * dedicated server's S2): the data half of what the phone's load_map and
 * start_game did -- the Trial cache, the world, collision, the walkable
 * grid, items, vehicles, the game with its players and bots -- for any
 * platform. The platform finds and maps the Trial files, then draws, plays
 * sound and runs its local player's weapon and body around these calls.
 *
 * Portable: no platform header here. */
#ifndef HTA_APP_MATCH_LOAD_H
#define HTA_APP_MATCH_LOAD_H

#include <stdbool.h>
#include "app/fs.h"

typedef struct hta_session hta_session;

/* The world from the mapped Trial files: s->map_data (required),
 * s->sounds_data and s->bitmaps_data (optional), and s->world's package
 * through `fs` when s->world is set. Walkable grids are kept in
 * `writable_dir` ("" or NULL: built every time). False, with s->status
 * saying why, when there is no playable world. */
bool hta_match_load_world(hta_session *s, const hta_fs *fs, const char *writable_dir);

/* The game on that world, from the session's match setup (game_mode,
 * bot_count, bot_skill, limits, vehicle_roster, classes, net_enabled /
 * net_hosting): imported weapons and bodies, starts and flags, vehicles,
 * the walkable grid for bots, items, the local player when
 * `local_player` (s->me; -1 without one: a server) and the bots. False
 * when the map has no playable game. */
bool hta_match_start(hta_session *s, const char *writable_dir, bool local_player);

/* After the platform has dressed its player: the match is on, LAN's map
 * check set, the props' nodes blocked, the first round begun. */
void hta_match_begin(hta_session *s);

/* Props block the walkable grid where they stand, once the props exist. */
void hta_match_nav_props(hta_session *s);

#endif
