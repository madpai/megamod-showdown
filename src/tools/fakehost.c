/* MEGAMOD FAKEHOST: a scripted LAN host, for testing a joiner (the PC's
 * megamod-join, or a phone) with no phone and no game data. It speaks
 * protocol v9 as a phone host does: WORLD, GAME (scores, props), KILL
 * (gibbed), FX (detonations). Its match is a flat plain:
 *   - bots walk circles; every few seconds one is blown apart by a
 *     grenade and comes back three seconds later,
 *   - every joiner gets a unit it steers with its CONTROL packets
 *     (forward/right along its yaw at a run, on flat ground),
 *   - the first `--props` props of the joiner's map break in turn and
 *     come back (a joiner on an imported map sees its crates go).
 *
 *   megamod-fakehost [--port P] [--bots N] [--props N] [--seconds S]
 *
 * It accepts any map (map check 0): it has no world of its own. */
#define _POSIX_C_SOURCE 200809L
#include "net/session.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    unsigned port = 32270, bots = 3, props = 0;
    double seconds = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bots") && i + 1 < argc) bots = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--props") && i + 1 < argc) props = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else { fprintf(stderr, "usage: %s [--port P] [--bots N] [--props N] [--seconds S]\n", argv[0]); return 2; }
    }
    if (bots > 8) bots = 8;
    if (props > HTA_NET_MAX_PROPS) props = HTA_NET_MAX_PROPS;
    static hta_net_server s;
    if (!hta_net_server_open(&s, (uint16_t)port)) { fprintf(stderr, "cannot open UDP %u\n", port); return 1; }
    snprintf(s.info.name, sizeof(s.info.name), "Fakehost");
    printf("fakehost: v%u on UDP %u, %u bots, %u props\n", HTA_NET_VERSION, port, bots, props);
    fflush(stdout);

    /* Slots 0..7 joiners (by peer), 8.. bots. */
    static hta_net_world w;
    static hta_net_game g;
    memset(&g, 0, sizeof(g));
    g.mode = 1; g.score_limit = 50; g.winner_team = 255;
    g.flag[0].carrier = g.flag[1].carrier = 255;
    g.prop_count = (uint16_t)props;
    float peer_pos[HTA_NET_MAX_PLAYERS][3] = { { 0 } };
    bool peer_was[HTA_NET_MAX_PLAYERS] = { 0 };
    float bot_dead[8] = { 0 };
    float centre[2] = { 0, 0 };
    bool have_centre = false;
    int16_t kills[HTA_NET_MAX_ENTITIES] = { 0 };
    double t0 = now_s(), last = t0, next_kill = t0 + 3.0, next_prop = t0 + 2.0;
    uint32_t prop_turn = 0, rng = 12345, frames = 0, controls = 0;
    for (;;) {
        double now = now_s();
        float dt = (float)(now - last);
        last = now;
        if (seconds > 0 && now - t0 > seconds) break;
        hta_net_server_pump(&s, now);
        memset(&w, 0, sizeof(w));
        w.time = (float)(now - t0); w.score_limit = 50; w.respawn_time = 3;
        w.round = 1; w.winner = 255;
        /* Joiners. Bots walk at their mean height: the host has no floor. */
        float ground = 0.0f, cx = 0.0f, cy = 0.0f; unsigned grounders = 0;
        for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++) {
            hta_net_peer *p = &s.peers[i];
            if (!p->active) { peer_was[i] = false; continue; }
            if (!peer_was[i]) {
                peer_was[i] = true;
                peer_pos[i][0] = -4.0f + 2.0f * (float)i; peer_pos[i][1] = -6.0f; peer_pos[i][2] = 0.0f;
                printf("fakehost: player %u joined\n", p->player.id);
                fflush(stdout);
            }
            float yaw = 0.0f;
            if (p->has_control && now - p->last_control_at < 0.3) {
                const hta_net_control *c = &p->control;
                yaw = c->yaw;
                controls++;
                if (p->has_state) {
                    /* It has no world to walk them on: it takes the joiner's
                     * own word for where it stands (a phone host simulates). */
                    memcpy(peer_pos[i], p->player.pos, sizeof(peer_pos[i]));
                } else {
                    float sp = 2.25f;                           /* Halo's run, wu/s */
                    peer_pos[i][0] += (cosf(yaw) * c->forward + sinf(yaw) * c->right) * sp * dt;
                    peer_pos[i][1] += (sinf(yaw) * c->forward - cosf(yaw) * c->right) * sp * dt;
                }
            }
            if (p->has_state) { ground += peer_pos[i][2]; cx += peer_pos[i][0]; cy += peer_pos[i][1]; grounders++; }
            hta_net_entity *e = &w.entities[w.count++];
            e->id = (uint8_t)i; e->kind = HTA_NET_ENTITY_PLAYER; e->peer_id = p->player.id;
            e->flags = HTA_NET_ENTITY_ALIVE | HTA_NET_ENTITY_GROUNDED;
            memcpy(e->pos, peer_pos[i], sizeof(e->pos));
            e->yaw = yaw; e->health = 1; e->shield = 1; e->kills = kills[i];
            e->carry[0] = e->carry[1] = 255;
            snprintf(e->name, sizeof(e->name), "Player %u", p->player.id);
        }
        /* Bots on circles. */
        for (unsigned b = 0; b < bots; b++) {
            hta_net_entity *e = &w.entities[w.count++];
            float a = (float)(now - t0) * 0.5f + (float)b * 2.1f, r = 2.0f + 1.0f * (float)b;
            e->id = (uint8_t)(8 + b); e->kind = HTA_NET_ENTITY_BOT;
            /* ...around the joiners, where they can be seen. */
            if (grounders && !have_centre) { centre[0] = cx / (float)grounders; centre[1] = cy / (float)grounders; have_centre = true; }
            e->pos[0] = centre[0] + cosf(a) * r; e->pos[1] = centre[1] + sinf(a) * r;
            e->pos[2] = grounders ? ground / (float)grounders : 0.0f;
            e->yaw = a + 1.5708f; e->health = 1; e->shield = 1; e->carry[0] = e->carry[1] = 255;
            e->velocity[0] = -sinf(a) * r * 0.5f; e->velocity[1] = cosf(a) * r * 0.5f;
            if (bot_dead[b] > 0) bot_dead[b] -= dt;
            e->flags = bot_dead[b] > 0 ? HTA_NET_ENTITY_GROUNDED : HTA_NET_ENTITY_ALIVE | HTA_NET_ENTITY_GROUNDED;
            snprintf(e->name, sizeof(e->name), "Bot %u", b + 1);
        }
        w.bot_count = (uint8_t)bots;
        /* A kill: a grenade blows a living bot apart. */
        if (bots && now >= next_kill) {
            rng = rng * 1664525u + 1013904223u;
            unsigned b = (rng >> 16) % bots;
            if (bot_dead[b] <= 0) {
                const hta_net_entity *e = &w.entities[w.count - bots + b];
                hta_net_kill k;
                memset(&k, 0, sizeof(k));
                k.victim = e->id; k.killer = 255; k.flags = HTA_NET_KILL_GIBBED; k.amount = 1.6f;
                memcpy(k.pos, e->pos, sizeof(k.pos)); k.pos[2] += 0.4f;
                memcpy(k.from, e->pos, sizeof(k.from)); k.from[0] -= 0.8f;
                snprintf(k.text, sizeof(k.text), "%s was blown up", e->name);
                hta_net_server_kill(&s, &k, now);
                hta_net_fx fx;
                memset(&fx, 0, sizeof(fx));
                fx.kind = HTA_NET_FX_DETONATE; fx.entity = 255; fx.weapon = 0;
                memcpy(fx.pos, k.from, sizeof(fx.pos)); fx.pos[2] = e->pos[2]; fx.dir[2] = 1.0f;
                hta_net_server_fx(&s, &fx);
                bot_dead[b] = 3.0f;
                g.team_score[b & 1]++;
                printf("fakehost: %s\n", k.text);
                fflush(stdout);
            }
            next_kill = now + 4.0;
        }
        /* Props break in turn and come back. */
        if (props && now >= next_prop) {
            unsigned i = prop_turn % props, j = (prop_turn + props / 2u + 1u) % props;
            g.prop_broken[i / 8] |= (uint8_t)(1u << (i % 8));
            if (j != i) g.prop_broken[j / 8] &= (uint8_t)~(1u << (j % 8));
            prop_turn++;
            next_prop = now + 2.5;
        }
        hta_net_server_world(&s, &w);
        hta_net_server_game(&s, &g);
        frames++;
        struct timespec nap = { 0, 8 * 1000 * 1000 };
        nanosleep(&nap, NULL);
    }
    printf("fakehost: done, %u joiners seen steering for %u frames\n", hta_net_server_count(&s), controls);
    hta_net_server_close(&s);
    return 0;
}
