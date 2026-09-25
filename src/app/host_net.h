/* The host's side of a networked match, per tick: portable, on hta_session,
 * shared by the phone host and the dedicated server (docs/DEDICATED_SERVER.md
 * stage S1). */
#ifndef HTA_APP_HOST_NET_H
#define HTA_APP_HOST_NET_H

#include "session.h"

/* Joiners into the game: a unit for each new peer (then `unit_added`, for
 * the platform's presentation; NULL on a server), their controls into its
 * input, and their unit removed when they leave. */
void hta_host_peers(hta_session *s, double now, void (*unit_added)(hta_session *s));
/* The local player's carried weapons, ammo, grenades and power-up into the
 * game's copy of them (a server has no local player: s->me < 0 is a no-op). */
void hta_host_mirror_local(hta_session *s);
/* The match out to every joiner: WORLD, projectiles, vehicles, drops, GAME. */
void hta_host_world(hta_session *s);

#endif
