#include "net_view.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The host's snapshot period (session.c sends WORLD at 20 Hz). */
#define VIEW_PERIOD 0.05
/* Ours, as the Android joiner: snap past this, else ease. */
#define VIEW_SNAP 0.5f
#define VIEW_EASE 0.12f
/* A detonation's reach when the joiner does not know the pool (a PC
 * client loads no weapons). Ours: a Halo frag grenade's order. */
#define VIEW_BLAST_RADIUS 1.5f

void hta_net_view_init(hta_net_view *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->me = -1;
    v->winner_team = 255;
}

static void feed_push(hta_net_view *v, const char *text, double now)
{
    memmove(v->feed[1], v->feed[0], sizeof(v->feed[0]) * (HTA_NET_VIEW_FEED - 1));
    memmove(v->feed_at + 1, v->feed_at, sizeof(v->feed_at[0]) * (HTA_NET_VIEW_FEED - 1));
    snprintf(v->feed[0], sizeof(v->feed[0]), "%s", text);
    v->feed_at[0] = now;
}

static float wrap(float a)
{
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

static void apply_world(hta_net_view *v, const hta_net_world *w, double now,
                        hta_player *local, hta_camera *cam, uint8_t my_peer)
{
    bool seen[HTA_NET_MAX_ENTITIES] = { 0 };
    if (w->round != v->round) {
        v->round = w->round;
        memset(v->feed, 0, sizeof(v->feed));
    }
    v->time = w->time;
    v->over = w->over;
    for (uint8_t i = 0; i < w->count && i < HTA_NET_MAX_ENTITIES; i++) {
        const hta_net_entity *e = &w->entities[i];
        if (e->id >= HTA_NET_MAX_ENTITIES) continue;
        hta_net_view_entity_state *s = &v->ent[e->id];
        seen[e->id] = true;
        bool alive = (e->flags & HTA_NET_ENTITY_ALIVE) != 0;
        /* Ease from where it was drawn; a fresh entity or a respawn jumps. */
        s->from = s->live && s->alive == alive ? s->to : *e;
        s->to = *e;
        s->to_at = now;
        if (s->alive && !alive) s->died_at = now;
        if (!s->alive && alive) s->gibbed = false;      /* back in, whole */
        s->alive = alive;
        s->live = true;
        if (e->peer_id && e->peer_id == my_peer) {
            v->me = e->id;
            if (alive && local) {
                bool respawn = !v->me_alive;
                float d[3] = { e->pos[0] - local->pos[0], e->pos[1] - local->pos[1], e->pos[2] - local->pos[2] };
                float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                float f = respawn || dist > VIEW_SNAP ? 1.0f : VIEW_EASE;
                for (int k = 0; k < 3; k++) {
                    local->pos[k] += d[k] * f;
                    if (cam) cam->pos[k] += d[k] * f;
                }
                if (respawn) {
                    local->velocity[0] = local->velocity[1] = local->velocity[2] = 0.0f;
                    if (cam) { cam->yaw = e->yaw; cam->pitch = e->pitch; }
                    v->respawned = true;
                }
                if (f == 1.0f) { v->snapped = true; v->corrections++; }
            }
            v->me_alive = alive;
        }
    }
    for (uint32_t i = 0; i < HTA_NET_MAX_ENTITIES; i++)
        if (!seen[i]) v->ent[i].live = false;
}

void hta_net_view_update(hta_net_view *v, hta_net_client *net, double now,
                         hta_player *local, hta_camera *cam, hta_world_fx *wfx)
{
    if (!v || !net) return;
    v->respawned = v->snapped = false;
    if (!net->connected) {
        v->me = -1; v->me_alive = false; v->props_synced = false;
        for (uint32_t i = 0; i < HTA_NET_MAX_ENTITIES; i++) v->ent[i].live = false;
        return;
    }
    if (net->have_world && net->last_world_tick != v->world_tick) {
        v->world_tick = net->last_world_tick;
        v->last_world_at = now;
        apply_world(v, &net->world, now, local, cam, net->id);
    }
    if (net->have_game) {
        const hta_net_game *g = &net->game;
        v->mode = g->mode; v->score_limit = g->score_limit; v->winner_team = g->winner_team;
        v->team_score[0] = g->team_score[0]; v->team_score[1] = g->team_score[1];
        if (wfx && wfx->ready) {
            /* The host's props: quiet the first time (we were not there). */
            wfx->props.remote = true;
            hta_props_apply_mask(&wfx->props, g->prop_broken, g->prop_count,
                                 v->props_synced ? &wfx->rigid : NULL, v->props_synced ? &wfx->fx : NULL);
            v->props_synced = true;
        }
    }
    hta_net_kill k;
    while (hta_net_client_pop_kill(net, &k)) {
        v->kills++;
        if (k.killer == net->id && v->me >= 0 && k.victim != (uint8_t)v->me && k.victim < HTA_NET_MAX_ENTITIES) {
            char line[96];
            snprintf(line, sizeof(line), "You killed %s", v->ent[k.victim].to.name);
            feed_push(v, line, now);
        } else feed_push(v, k.text, now);
        if ((k.flags & HTA_NET_KILL_GIBBED) && k.victim < HTA_NET_MAX_ENTITIES) {
            v->ent[k.victim].gibbed = true;
            v->gibs++;
            if (wfx && wfx->ready && wfx->gib_level) {
                hta_wfx_net_fx(wfx, HTA_WFX_NET_GIB, k.pos, k.from, k.amount);
                hta_wfx_push_cue(wfx, HTA_WFX_CUE_GIB, HTA_RMAT_FLESH, k.pos, k.amount, false);
            }
        }
    }
    hta_net_fx fx;
    while (hta_net_client_pop_fx(net, &fx)) {
        v->fx++;
        if (!wfx || !wfx->ready) continue;
        if (fx.kind == HTA_NET_FX_DETONATE)
            hta_wfx_net_fx(wfx, HTA_WFX_NET_DETONATE, fx.pos, fx.dir, VIEW_BLAST_RADIUS);
        else if (fx.kind == HTA_NET_FX_WRECK)
            hta_wfx_net_fx(wfx, HTA_WFX_NET_WRECK, fx.pos, fx.dir, 0.0f);
        else if (fx.kind == HTA_NET_FX_IMPACT) {
            /* Chips off whatever it struck; a remote prop only chips. */
            hta_game_event e;
            memset(&e, 0, sizeof(e));
            e.kind = HTA_EV_HIT_WORLD;
            memcpy(e.pos, fx.pos, sizeof(e.pos));
            memcpy(e.dir, fx.dir, sizeof(e.dir));
            hta_wfx_game_event(wfx, &e, NULL);
        }
    }
    /* Nobody to play them with on a PC: events are only counted. */
    hta_net_event ev;
    while (hta_net_client_pop_event(net, &ev)) {}
}

bool hta_net_view_send(hta_net_view *v, hta_net_client *net, double now,
                       const hta_net_view_input *in, const hta_camera *cam,
                       const hta_player *local, bool ready)
{
    if (!v || !net || !net->connected || !in || !cam) return false;
    /* One-shot presses count up even between sends: none is lost. */
    v->melee += in->melee; v->grenade += in->grenade; v->reload += in->reload;
    v->pickup += in->pickup; v->action += in->action; v->ability += in->ability;
    if (now - v->last_send < VIEW_PERIOD) return false;
    v->last_send = now;
    hta_net_control c;
    memset(&c, 0, sizeof(c));
    c.id = net->id;
    c.weapon_slot = in->weapon_slot & 1u;
    c.forward = fmaxf(-1.0f, fminf(1.0f, in->forward));
    c.right = fmaxf(-1.0f, fminf(1.0f, in->right));
    c.yaw = wrap(cam->yaw);
    c.pitch = fmaxf(-1.55f, fminf(1.55f, cam->pitch));
    if (in->jump) c.flags |= HTA_NET_JUMP;
    if (in->fire) c.flags |= HTA_NET_TRIGGER;
    if (in->crouch) c.flags |= HTA_NET_DUCK;
    if (in->alt) c.flags |= HTA_NET_ALT;
    if (ready && net->have_game) c.flags |= HTA_NET_READY;
    c.melee_count = v->melee; c.grenade_count = v->grenade; c.reload_count = v->reload;
    c.pickup_count = v->pickup; c.action_count = v->action; c.ability_count = v->ability;
    c.loadout[0] = c.loadout[1] = 255;
    bool ok = hta_net_client_control(net, &c);
    if (local) {
        hta_net_player p;
        memset(&p, 0, sizeof(p));
        p.id = net->id; p.weapon = in->weapon_slot;
        for (int k = 0; k < 3; k++) { p.pos[k] = local->pos[k]; p.velocity[k] = local->velocity[k]; }
        p.yaw = c.yaw; p.pitch = c.pitch;
        if (local->on_ground) p.flags |= HTA_NET_GROUNDED;
        if (local->crouch_t > 0.5f) p.flags |= HTA_NET_CROUCH;
        hta_net_client_state(net, &p);
    }
    return ok;
}

bool hta_net_view_entity(const hta_net_view *v, uint32_t i, double now, hta_net_pose *out)
{
    if (!v || !out || i >= HTA_NET_MAX_ENTITIES || (int32_t)i == v->me) return false;
    const hta_net_view_entity_state *s = &v->ent[i];
    if (!s->live) return false;
    float t = (float)((now - s->to_at) / VIEW_PERIOD);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    for (int k = 0; k < 3; k++) out->pos[k] = s->from.pos[k] + (s->to.pos[k] - s->from.pos[k]) * t;
    out->yaw = s->from.yaw + wrap(s->to.yaw - s->from.yaw) * t;
    out->pitch = s->from.pitch + (s->to.pitch - s->from.pitch) * t;
    out->flags = s->to.flags; out->weapon = s->to.weapon; out->kind = s->to.kind;
    out->character = s->to.character;
    out->health = s->to.health; out->shield = s->to.shield;
    out->alive = s->alive; out->gibbed = s->gibbed;
    out->name = s->to.name;
    return true;
}

uint32_t hta_net_view_feed(const hta_net_view *v, double now, const char **lines, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; v && i < HTA_NET_VIEW_FEED && n < max; i++)
        if (v->feed[i][0] && now - v->feed_at[i] < HTA_NET_VIEW_FEED_SECONDS) lines[n++] = v->feed[i];
    return n;
}
