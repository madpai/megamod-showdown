#include "scripted_match.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void hta_scripted_match_init(hta_scripted_match *m, const char *tag, unsigned bots, unsigned props, double now)
{
    memset(m, 0, sizeof(*m));
    m->tag = tag ? tag : "scripted";
    m->bots = bots > 8 ? 8 : bots;
    m->props = props > HTA_NET_MAX_PROPS ? HTA_NET_MAX_PROPS : props;
    m->g.mode = 1; m->g.score_limit = 50; m->g.winner_team = 255;
    m->g.flag[0].carrier = m->g.flag[1].carrier = 255;
    m->g.prop_count = (uint16_t)m->props;
    m->t0 = m->last = now;
    m->next_kill = now + 3.0;
    m->next_prop = now + 2.0;
    m->rng = 12345;
}

void hta_scripted_match_tick(hta_scripted_match *m, hta_net_server *s, double now)
{
    float dt = (float)(now - m->last);
    m->last = now;
    memset(&m->w, 0, sizeof(m->w));
    m->w.time = (float)(now - m->t0); m->w.score_limit = 50; m->w.respawn_time = 3;
    m->w.round = 1; m->w.winner = 255;
    /* Joiners. Bots walk at their mean height: the host has no floor. */
    float ground = 0.0f, cx = 0.0f, cy = 0.0f; unsigned grounders = 0;
    for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++) {
        hta_net_peer *p = &s->peers[i];
        if (!p->active) { m->peer_was[i] = false; continue; }
        if (!m->peer_was[i]) {
            m->peer_was[i] = true;
            m->peer_pos[i][0] = -4.0f + 2.0f * (float)i; m->peer_pos[i][1] = -6.0f; m->peer_pos[i][2] = 0.0f;
            printf("%s: player %u joined\n", m->tag, p->player.id);
            fflush(stdout);
        }
        float yaw = 0.0f;
        if (p->has_control && now - p->last_control_at < 0.3) {
            const hta_net_control *c = &p->control;
            yaw = c->yaw;
            m->controls++;
            if (p->has_state) {
                /* It has no world to walk them on: it takes the joiner's
                 * own word for where it stands (a phone host simulates). */
                memcpy(m->peer_pos[i], p->player.pos, sizeof(m->peer_pos[i]));
            } else {
                float sp = 2.25f;                           /* Halo's run, wu/s */
                m->peer_pos[i][0] += (cosf(yaw) * c->forward + sinf(yaw) * c->right) * sp * dt;
                m->peer_pos[i][1] += (sinf(yaw) * c->forward - cosf(yaw) * c->right) * sp * dt;
            }
        }
        if (p->has_state) { ground += m->peer_pos[i][2]; cx += m->peer_pos[i][0]; cy += m->peer_pos[i][1]; grounders++; }
        hta_net_entity *e = &m->w.entities[m->w.count++];
        e->id = (uint8_t)i; e->kind = HTA_NET_ENTITY_PLAYER; e->peer_id = p->player.id;
        e->flags = HTA_NET_ENTITY_ALIVE | HTA_NET_ENTITY_GROUNDED;
        memcpy(e->pos, m->peer_pos[i], sizeof(e->pos));
        e->yaw = yaw; e->health = 1; e->shield = 1; e->kills = m->kills[i];
        e->carry[0] = e->carry[1] = 255;
        snprintf(e->name, sizeof(e->name), "Player %u", p->player.id);
    }
    /* Bots on circles. */
    for (unsigned b = 0; b < m->bots; b++) {
        hta_net_entity *e = &m->w.entities[m->w.count++];
        float a = (float)(now - m->t0) * 0.5f + (float)b * 2.1f, r = 2.0f + 1.0f * (float)b;
        e->id = (uint8_t)(8 + b); e->kind = HTA_NET_ENTITY_BOT;
        /* ...around the joiners, where they can be seen. */
        if (grounders && !m->have_centre) { m->centre[0] = cx / (float)grounders; m->centre[1] = cy / (float)grounders; m->have_centre = true; }
        e->pos[0] = m->centre[0] + cosf(a) * r; e->pos[1] = m->centre[1] + sinf(a) * r;
        e->pos[2] = grounders ? ground / (float)grounders : 0.0f;
        e->yaw = a + 1.5708f; e->health = 1; e->shield = 1; e->carry[0] = e->carry[1] = 255;
        e->velocity[0] = -sinf(a) * r * 0.5f; e->velocity[1] = cosf(a) * r * 0.5f;
        if (m->bot_dead[b] > 0) m->bot_dead[b] -= dt;
        e->flags = m->bot_dead[b] > 0 ? HTA_NET_ENTITY_GROUNDED : HTA_NET_ENTITY_ALIVE | HTA_NET_ENTITY_GROUNDED;
        snprintf(e->name, sizeof(e->name), "Bot %u", b + 1);
    }
    m->w.bot_count = (uint8_t)m->bots;
    /* A kill: a grenade blows a living bot apart. */
    if (m->bots && now >= m->next_kill) {
        m->rng = m->rng * 1664525u + 1013904223u;
        unsigned b = (m->rng >> 16) % m->bots;
        if (m->bot_dead[b] <= 0) {
            const hta_net_entity *e = &m->w.entities[m->w.count - m->bots + b];
            hta_net_kill k;
            memset(&k, 0, sizeof(k));
            k.victim = e->id; k.killer = 255; k.flags = HTA_NET_KILL_GIBBED; k.amount = 1.6f;
            memcpy(k.pos, e->pos, sizeof(k.pos)); k.pos[2] += 0.4f;
            memcpy(k.from, e->pos, sizeof(k.from)); k.from[0] -= 0.8f;
            snprintf(k.text, sizeof(k.text), "%s was blown up", e->name);
            hta_net_server_kill(s, &k, now);
            hta_net_fx fx;
            memset(&fx, 0, sizeof(fx));
            fx.kind = HTA_NET_FX_DETONATE; fx.entity = 255; fx.weapon = 0;
            memcpy(fx.pos, k.from, sizeof(fx.pos)); fx.pos[2] = e->pos[2]; fx.dir[2] = 1.0f;
            hta_net_server_fx(s, &fx);
            m->bot_dead[b] = 3.0f;
            m->g.team_score[b & 1]++;
            printf("%s: %s\n", m->tag, k.text);
            fflush(stdout);
        }
        m->next_kill = now + 4.0;
    }
    /* Props break in turn and come back. */
    if (m->props && now >= m->next_prop) {
        unsigned i = m->prop_turn % m->props, j = (m->prop_turn + m->props / 2u + 1u) % m->props;
        m->g.prop_broken[i / 8] |= (uint8_t)(1u << (i % 8));
        if (j != i) m->g.prop_broken[j / 8] &= (uint8_t)~(1u << (j % 8));
        m->prop_turn++;
        m->next_prop = now + 2.5;
    }
    hta_net_server_world(s, &m->w);
    hta_net_server_game(s, &m->g);
    m->frames++;
}
