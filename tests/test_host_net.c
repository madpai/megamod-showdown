/* The host's per-tick networking (app/host_net), off the phone: a joiner
 * over localhost gets a unit in the host's game, its controls drive that
 * unit, WORLD names it as theirs, and leaving removes it. Exactly what
 * a dedicated server will run -- with no local player (me = -1). */
#include "app/host_net.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int uploads;
static void unit_added(hta_session *s) { (void)s; uploads++; }

static void pump(hta_net_server *s, hta_net_client *c, double now)
{
    hta_net_client_pump(c, now);
    hta_net_server_pump(s, now);
    hta_net_client_pump(c, now + 0.0001);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    static hta_session s;
    memset(&s, 0, sizeof(s));
    s.me = -1;                                   /* a server: nobody plays here */
    for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++) s.peer_unit[i] = -1;
    s.game_on = true;
    s.net.connected = true;                      /* the host's own session is up */
    assert(hta_net_server_open(&s.host_server, 0));
    uint16_t port = hta_udp_port(&s.host_server.udp);

    static hta_net_client joiner;
    assert(hta_net_client_open(&joiner, "127.0.0.1", port));
    double t = 1.0;
    for (int i = 0; i < 6 && !joiner.connected; i++, t += 0.05) pump(&s.host_server, &joiner, t);
    assert(joiner.connected);

    /* The joiner runs forward. */
    hta_net_control c;
    memset(&c, 0, sizeof(c));
    c.id = joiner.id; c.forward = 1.0f; c.yaw = 0.5f;
    c.flags = HTA_NET_TRIGGER;
    c.loadout[0] = c.loadout[1] = 255;
    for (int i = 0; i < 4; i++, t += 0.05) {
        assert(hta_net_client_control(&joiner, &c));
        pump(&s.host_server, &joiner, t);
    }
    hta_host_peers(&s, t, unit_added);
    int slot = -1;
    for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++)
        if (s.host_server.peers[i].active && s.host_server.peers[i].player.id == joiner.id) slot = (int)i;
    assert(slot >= 0);
    int unit = s.peer_unit[slot];
    printf("joiner %u -> unit %d, %d upload(s), class reject %d\n", joiner.id, unit, uploads, s.peer_class_reject[slot]);
    assert(unit >= 0 && uploads == 1);
    assert(s.game.units[unit].kind == HTA_UNIT_REMOTE);
    hta_host_peers(&s, t, unit_added);
    assert(uploads == 1);                        /* once per joiner, not per tick */
    if (!s.peer_class_reject[slot]) {
        assert(s.game.units[unit].in.move.move_forward == 1.0f);
        assert(s.game.units[unit].in.move.fire);
        assert(s.game.units[unit].eye.yaw == 0.5f);
    }

    /* WORLD names the unit as the joiner's; no local player is listed. */
    hta_host_world(&s);
    for (int i = 0; i < 4 && !joiner.have_world; i++, t += 0.05) pump(&s.host_server, &joiner, t);
    assert(joiner.have_world && joiner.have_game);
    const hta_net_world *w = &joiner.world;
    printf("WORLD: %u entities\n", w->count);
    bool found = false;
    for (unsigned i = 0; i < w->count; i++) {
        assert(w->entities[i].peer_id != 0 || w->entities[i].kind == HTA_NET_ENTITY_BOT);
        if (w->entities[i].id == (uint8_t)unit) {
            found = true;
            assert(w->entities[i].peer_id == joiner.id && w->entities[i].kind == HTA_NET_ENTITY_PLAYER);
        }
    }
    assert(found);

    /* Silence: its controls go stale and the unit stops. */
    hta_host_peers(&s, t + 1.0, unit_added);
    assert(s.game.units[unit].in.move.move_forward == 0.0f && !s.game.units[unit].in.move.fire);

    /* Gone: the unit goes with it. */
    s.host_server.peers[slot].active = false;
    hta_host_peers(&s, t + 1.0, unit_added);
    assert(s.peer_unit[slot] == -1);
    assert(s.game.units[unit].kind == HTA_UNIT_NONE);

    hta_net_client_close(&joiner);
    hta_net_server_close(&s.host_server);
    printf("host_net: ok\n");
    return 0;
}
