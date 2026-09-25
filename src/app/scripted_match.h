/* A scripted LAN match: what megamod-fakehost and `megamod-server` with
 * `sim = scripted` serve to test joiners and networking without the real
 * simulation (or any game data). Bots walk circles around the joiners; a
 * grenade blows one apart every four seconds; props break in turn; every
 * joiner is a unit it steers (its reported position is taken as is). */
#ifndef HTA_APP_SCRIPTED_MATCH_H
#define HTA_APP_SCRIPTED_MATCH_H

#include "../net/session.h"

typedef struct {
    const char *tag;            /* log prefix */
    unsigned bots, props;
    hta_net_world w;
    hta_net_game g;
    float peer_pos[HTA_NET_MAX_PLAYERS][3];
    bool peer_was[HTA_NET_MAX_PLAYERS];
    float bot_dead[8];
    float centre[2];
    bool have_centre;
    int16_t kills[HTA_NET_MAX_ENTITIES];
    double t0, last, next_kill, next_prop;
    uint32_t prop_turn, rng, frames, controls;
} hta_scripted_match;

void hta_scripted_match_init(hta_scripted_match *m, const char *tag, unsigned bots, unsigned props, double now);
/* One step: reads the server's peers, sends WORLD and GAME (and a KILL/FX
 * when a bot goes). Call after hta_net_server_pump. */
void hta_scripted_match_tick(hta_scripted_match *m, hta_net_server *s, double now);

#endif
