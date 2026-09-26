/* The match's world, one step (docs/DESKTOP_AGENT.md step 4, "the loop
 * out"; the dedicated server's S3). Everything a match does whoever is
 * watching: the game (bots, rounds, vehicles, pools, drops, flags), its
 * events' consequences (props hit, LAN effects and kills sent), the next
 * round, the breakable props and their debris' physics, and a host's
 * networking. The platform runs its own player's controls and weapon
 * before it, and shows what happened after it from the outbox.
 *
 * Portable: no platform header here. */
#ifndef HTA_APP_SESSION_TICK_H
#define HTA_APP_SESSION_TICK_H

typedef struct hta_session hta_session;

#define HTA_POSTGAME 10.0f   /* seconds the score stays up before the next game */

/* One step of `dt` seconds at time `now` (seconds, any epoch). Fills
 * s->outbox with the game's events, s->prop_outbox with props that broke
 * or came back, and sets s->round_restarted -- all three cleared at the
 * start of every tick, so drain them before the next. `unit_added` is
 * called when a LAN joiner gets a body (a platform uploads it); may be NULL. */
void hta_session_tick(hta_session *s, float dt, double now, void (*unit_added)(hta_session *));

#endif
