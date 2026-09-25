/* A LAN joiner against a scripted host, in one process over localhost:
 * the path a PC joins a phone's match by, with no phone. The host lists
 * the joiner's own unit and a bot walking a line; kills it (gibbed), sets
 * off a grenade, breaks a prop and scores. The joiner must steer its unit
 * by CONTROL, see the bot move smoothly, follow the host's corrections,
 * and replay the kill, the gibs, the blast and the prop. */
#include "game/net_view.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static hta_vertex V[6];
static uint32_t I[6];

static void pump(hta_net_server *s, hta_net_client *c, double now)
{
    hta_net_client_pump(c, now);
    hta_net_server_pump(s, now);
    hta_net_client_pump(c, now + 0.0001);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    /* The joiner's world: a floor, one breakable crate, effects on. */
    const float q[6][3] = { {-40,-40,0}, {40,-40,0}, {40,40,0}, {-40,-40,0}, {40,40,0}, {-40,40,0} };
    for (int i = 0; i < 6; i++) { memset(&V[i], 0, sizeof(V[i])); memcpy(V[i].pos, q[i], 12); I[i] = (uint32_t)i; }
    hta_bsp_mesh m;
    memset(&m, 0, sizeof(m));
    m.vertices = V; m.vertex_count = 6; m.indices = I; m.index_count = 6;
    m.bounds_min[0] = m.bounds_min[1] = -40; m.bounds_max[0] = m.bounds_max[1] = 40; m.bounds_max[2] = 1;
    hta_collision col;
    assert(hta_collision_build(&col, &m));
    hta_gfx_settings gs;
    hta_gfx_settings_preset(&gs, HTA_QUALITY_HIGH);
    static hta_world_fx wfx;
    assert(hta_wfx_init(&wfx, &col, &gs));
    hta_external_breakable br;
    memset(&br, 0, sizeof(br));
    br.min[0] = 6; br.max[0] = 6.6f; br.min[1] = -0.3f; br.max[1] = 0.3f; br.max[2] = 0.6f; br.health = 30;
    hta_external_map em;
    memset(&em, 0, sizeof(em));
    em.breakables = &br; em.breakable_count = 1;
    hta_wfx_load_map(&wfx, &em, 0);

    /* The host and the joiner; both think they are on the same map. */
    static hta_net_server host;
    assert(hta_net_server_open(&host, 0));
    host.map_crc = 0xB100D;
    uint16_t port = hta_udp_port(&host.udp);
    static hta_net_client net;
    assert(hta_net_client_open(&net, "127.0.0.1", port));
    net.map_crc = 0xB100D;
    hta_player me;
    hta_player_init(&me);
    hta_camera cam;
    hta_camera_init(&cam);
    static hta_net_view v;
    hta_net_view_init(&v);

    double t = 1.0;
    for (int i = 0; i < 6 && !net.connected; i++, t += 0.05) pump(&host, &net, t);
    assert(net.connected);
    printf("joined as %u\n", net.id);

    static hta_net_world w;
    memset(&w, 0, sizeof(w));
    w.count = 2;
    w.entities[0].id = 0; w.entities[0].kind = HTA_NET_ENTITY_PLAYER; w.entities[0].peer_id = net.id;
    w.entities[0].flags = HTA_NET_ENTITY_ALIVE | HTA_NET_ENTITY_GROUNDED;
    w.entities[0].pos[0] = 3; w.entities[0].pos[1] = 4;
    snprintf(w.entities[0].name, sizeof(w.entities[0].name), "PC");
    w.entities[1].id = 1; w.entities[1].kind = HTA_NET_ENTITY_BOT;
    w.entities[1].flags = HTA_NET_ENTITY_ALIVE | HTA_NET_ENTITY_GROUNDED;
    snprintf(w.entities[1].name, sizeof(w.entities[1].name), "Bot");
    static hta_net_game g;
    memset(&g, 0, sizeof(g));
    g.mode = 1; g.score_limit = 25; g.winner_team = 255; g.flag[0].carrier = g.flag[1].carrier = 255;
    g.prop_count = 1;

    hta_net_view_input in;
    memset(&in, 0, sizeof(in));
    in.forward = 1.0f;
    bool controlled = false, smooth = true, grenade_seen = false;
    float last_bot_x = -1;
    uint16_t host_grenades = 0;
    for (int f = 0; f < 120; f++, t += 1.0 / 60.0) {
        /* The host: its bot walks +x at 3 wu/s; every 50 ms, WORLD. */
        w.entities[1].pos[0] = (float)((t - 1.0) * 3.0);
        if (f == 30) in.grenade = true;
        if (f == 60) {                                  /* the host corrects us: a teleporter */
            w.entities[0].pos[0] = 20; w.entities[0].pos[1] = -5;
        } else if (f > 60) {
            w.entities[0].pos[0] = me.pos[0]; w.entities[0].pos[1] = me.pos[1];
        }
        if (f == 90) {                                  /* the bot dies in a rocket blast */
            w.entities[1].flags &= (uint8_t)~HTA_NET_ENTITY_ALIVE;
            hta_net_kill k;
            memset(&k, 0, sizeof(k));
            k.victim = 1; k.killer = 0; k.flags = HTA_NET_KILL_GIBBED; k.amount = 1.5f;
            k.pos[0] = w.entities[1].pos[0]; k.pos[2] = 0.4f; k.from[0] = w.entities[1].pos[0] - 1;
            snprintf(k.text, sizeof(k.text), "Bot was blown up by PC");
            assert(hta_net_server_kill(&host, &k, t));
            hta_net_fx fx = { .kind = HTA_NET_FX_DETONATE, .entity = 0, .weapon = 1 };
            fx.pos[0] = k.pos[0]; fx.dir[2] = 1;
            assert(hta_net_server_fx(&host, &fx));
            g.team_score[0] = 1;
            g.prop_broken[0] = 1;                       /* the blast took the crate */
        }
        hta_net_server_world(&host, &w);
        hta_net_server_game(&host, &g);
        pump(&host, &net, t);
        hta_net_view_update(&v, &net, t, &me, &cam, &wfx);
        hta_player_update(&me, &cam, &col, &(hta_player_input){ .move_forward = in.forward }, 1.0f / 60.0f);
        hta_net_view_send(&v, &net, t, &in, &cam, &me, true);
        in.grenade = false;
        hta_wfx_update(&wfx, 1.0f / 60.0f, &cam);
        /* The host sees our controls. */
        for (unsigned p = 0; p < HTA_NET_MAX_PLAYERS; p++)
            if (host.peers[p].active && host.peers[p].has_control) {
                controlled |= host.peers[p].control.forward == 1.0f && (host.peers[p].control.flags & HTA_NET_READY);
                host_grenades = host.peers[p].control.grenade_count;
            }
        grenade_seen |= host_grenades == 1;
        /* The bot is drawn smoothly: never a step bigger than it walks. */
        hta_net_pose pose;
        if (f < 90 && hta_net_view_entity(&v, 1, t, &pose)) {
            if (last_bot_x >= 0 && fabsf(pose.pos[0] - last_bot_x) > 3.0f * 0.06f) smooth = false;
            last_bot_x = pose.pos[0];
        }
        if (f == 62) {
            printf("after the host's correction: at %.2f %.2f\n", me.pos[0], me.pos[1]);
            assert(fabsf(me.pos[0] - 20) < 0.5f && fabsf(me.pos[1] + 5) < 0.5f);
        }
    }
    hta_net_pose pose;
    assert(!hta_net_view_entity(&v, 0, t, &pose));              /* ourselves: not drawn */
    assert(hta_net_view_entity(&v, 1, t, &pose) && !pose.alive && pose.gibbed && !strcmp(pose.name, "Bot"));
    const char *lines[4];
    uint32_t nl = hta_net_view_feed(&v, t, lines, 4);
    printf("me=%d controlled=%d grenade=%d smooth=%d kills=%u gibs=%u fx=%u score %d-%d feed: %s\n",
           v.me, controlled, grenade_seen, smooth, v.kills, v.gibs, v.fx, v.team_score[0], v.team_score[1],
           nl ? lines[0] : "(none)");
    assert(v.me == 0 && controlled && grenade_seen && smooth);
    assert(v.kills == 1 && v.gibs == 1 && v.fx == 1 && v.team_score[0] == 1 && v.mode == 1);
    assert(nl == 1 && strstr(lines[0], "blown up"));
    assert(v.corrections >= 1);
    /* The effects happened here: gibs and clods flying, the crate gone. */
    assert(hta_rigid_active(&wfx.rigid) > 10 && wfx.props.props[0].broken && wfx.props.remote);
    /* And nothing the joiner did locally could have broken it. */
    hta_net_client_close(&net);
    hta_net_server_close(&host);
    hta_wfx_free(&wfx);
    hta_collision_free(&col);
    puts("net view OK");
    return 0;
}
