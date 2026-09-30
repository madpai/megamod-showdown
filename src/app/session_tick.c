/* The match's world, one step (app/session_tick.h). Moved from the Android
 * frame: its game update and round restart, game_events' consequences, the
 * world effects it ran inside the draw branch, and net_frame's host half. */
#include "app/session_tick.h"
#include "app/host_net.h"
#include "app/match_load.h"
#include "app/session.h"
#include "app/racing.h"
#include "platform/platform.h"
#include "script/script.h"
#include <math.h>
#include <string.h>

/* What a host tells its joiners about an event: shots, impacts and
 * blasts as effects, wrecks, and kills (with the gibs' push). */
static void host_send(hta_session *s, const hta_game_event *e, double now)
{
    if (!s->net_hosting) return;
    if ((e->a == -1 || (e->a >= 0 && e->a < HTA_GAME_MAX_UNITS)) &&
        (e->kind == HTA_EV_FIRE || e->kind == HTA_EV_HIT_WORLD || e->kind == HTA_EV_DETONATE)) {
        hta_net_fx fx = {0};
        fx.kind = e->kind == HTA_EV_FIRE ? HTA_NET_FX_FIRE :
                  e->kind == HTA_EV_HIT_WORLD ? HTA_NET_FX_IMPACT : HTA_NET_FX_DETONATE;
        fx.entity = e->a < 0 ? 255 : (uint8_t)e->a;
        fx.weapon = (uint8_t)(e->kind == HTA_EV_DETONATE ? e->pool : e->weapon);
        fx.material = e->material;
        for (int k = 0; k < 3; k++) { fx.pos[k] = e->pos[k]; fx.dir[k] = e->dir[k]; }
        hta_net_server_fx(&s->host_server, &fx);
    }
    if (e->kind == HTA_EV_WRECK) {
        hta_net_fx fx = { .kind = HTA_NET_FX_WRECK,
                          .entity = e->a >= 0 && e->a < HTA_GAME_MAX_UNITS ? (uint8_t)e->a : 255,
                          .weapon = (uint8_t)(e->b & 31), .material = 0 };
        for (int k = 0; k < 3; k++) { fx.pos[k] = e->pos[k]; fx.dir[k] = e->dir[k]; }
        hta_net_server_fx(&s->host_server, &fx);
    }
    if (e->kind == HTA_EV_KILL && e->a >= 0 && e->a < HTA_GAME_MAX_UNITS) {
        hta_net_kill kill = {0};
        kill.victim = (uint8_t)e->a;
        kill.killer = e->b >= 0 && e->b < HTA_GAME_MAX_UNITS ? (uint8_t)e->b : 255;
        size_t k = 0;
        while (k < sizeof(kill.text) - 1 && e->text[k]) {
            unsigned char ch = (unsigned char)e->text[k];
            kill.text[k] = (char)(ch >= 32 && ch < 127 ? ch : '?'); k++;
        }
        kill.text[k] = 0;
        if (e->a < (int32_t)s->game.unit_count && s->game.units[e->a].gibbed) {
            kill.flags = HTA_NET_KILL_GIBBED;
            kill.amount = e->amount > 4.25f ? 4.25f : e->amount > 0.0f ? e->amount : 0.0f;
            for (int c = 0; c < 3; c++) {
                kill.pos[c] = fminf(fmaxf(e->pos[c], -99999.0f), 99999.0f);
                kill.from[c] = fminf(fmaxf(e->dir[c], -99999.0f), 99999.0f);
            }
        }
        hta_net_server_kill(&s->host_server, &kill, now);
    }
}

/* The breakable props and their debris (world_fx): set up once the map's
 * collision exists, solid while whole, smashed by cars, stepped. Runs
 * whether or not anything is drawn -- in the draw branch, props froze
 * behind a menu or a killcam. The GPU half stays with the platform. */
static void world_fx(hta_session *s, float dt)
{
    if (s->map_loaded && s->wfx_world != (const void *)s->mesh.vertices) {
        if (!s->wfx.ready) {
            if (hta_wfx_init(&s->wfx, &s->col, &s->video))
                hta_wfx_choose_weather(&s->wfx, hta_wfx_parse_weather(s->video_cfg, s->video_cfg_len));
        } else {
            hta_wfx_reset(&s->wfx);
        }
        /* An imported map's breakables and weather. Props come back
         * after 30 s (ours) so a long match keeps its cover. */
        hta_wfx_load_map(&s->wfx, s->world_loaded ? &s->world_ext : NULL, 30.0f);
        s->props_synced = s->props_count_warned = false;
        if (s->wfx.props.count)
            hta_log("[wfx] %u breakable props, weather %s", s->wfx.props.count,
                    hta_weather_name(s->wfx.weather.kind));
        s->wfx_world = s->mesh.vertices;
    }
    /* A LAN client breaks and rebuilds props only as the host says. */
    s->wfx.props.remote = s->net_enabled && !s->net_hosting;
    hta_match_nav_props(s);
    /* Props are solid while whole, and movers wherever they are: their
     * instances ride with the vehicles' in the grid everyone collides with. */
    if ((s->wfx.ready && s->wfx.props.count) || s->went.loaded) {
        uint32_t nv = s->vehicles.loaded ? s->vehicles.count : 0u;
        uint32_t cap = (uint32_t)(sizeof(s->col_merged) / sizeof(s->col_merged[0])) - HTA_WDEF_MAX_ENTITIES;
        uint32_t n = s->wfx.ready ? hta_props_instances(&s->wfx.props, s->vehicles.inst, nv, s->col_merged, cap) : 0u;
        if (!s->wfx.ready) for (uint32_t i = 0; i < nv && n < cap; i++) s->col_merged[n++] = s->vehicles.inst[i];
        n += hta_went_instances(&s->went, s->col_merged + n, HTA_WDEF_MAX_ENTITIES);
        s->col.instance_count = n;
        s->col.instances = s->col_merged;
        /* A broad phase over them: every ray, ground probe and
         * debris contact looks at the few near it, not all. */
        hta_collision_index_instances(&s->col, &s->col_index, 0.25f);
    }
    /* Cars smash props they drive into (they do not collide). */
    for (uint32_t i = 0; s->wfx.props.count && s->vehicles.loaded && i < s->vehicles.count; i++) {
        const hta_vehicle *car = &s->vehicles.cars[i];
        if (!car->active) continue;
        float sp = hta_vehicles_speed(&s->vehicles, i);
        float v3[3] = { cosf(car->yaw) * sp, sinf(car->yaw) * sp, 0.0f };
        hta_wfx_ram(&s->wfx, car->pos, v3, car->body_radius > 0.1f ? car->body_radius : 1.0f);
    }
    /* What broke or came back: an explosive one is a real blast (the
     * host's game hurts people). The platform hides or shows its
     * triangles from the prop outbox. */
    hta_prop_event pe;
    while (s->wfx.ready && hta_props_pop(&s->wfx.props, &pe)) {
        if (pe.kind == HTA_PROP_EV_EXPLODED) {
            if (s->game_on && (!s->net_enabled || s->net_hosting))
                hta_game_blast(&s->game, -1, pe.pos, pe.damage, pe.radius * 0.3f, pe.radius);
            hta_props_blast(&s->wfx.props, pe.pos, pe.damage, pe.radius, &s->wfx.rigid, &s->wfx.fx);
        }
        if (s->prop_outbox_count < sizeof(s->prop_outbox) / sizeof(s->prop_outbox[0]))
            s->prop_outbox[s->prop_outbox_count++] = pe;
    }
    if (s->wfx.ready) hta_wfx_update(&s->wfx, dt, s->me >= 0 ? &s->cam : NULL);
}

/* The host's player presses use: at a button in reach, that is the
 * button's (and not a vehicle's). Runs before the game spends the press. */
static void world_interact(hta_session *s)
{
    if (!s->went.loaded || !s->game_on || s->game.over) return;
    for (uint32_t i = 0; i < s->game.unit_count && i < HTA_WENT_MAX_ACTORS; i++) {
        hta_unit *u = &s->game.units[i];
        if (u->kind == HTA_UNIT_NONE || !u->alive || !u->in.action || u->vehicle >= 0) continue;
        float cp = cosf(u->eye.pitch), fwd[3] = { cosf(u->eye.yaw) * cp, sinf(u->eye.yaw) * cp, sinf(u->eye.pitch) };
        int32_t used = hta_went_interact(&s->went, (uint8_t)i, u->eye.pos, fwd);
        if (used >= 0) {
            u->in.action = false;
            hta_log("[world] unit %u used %s", i, s->went.defs->entity[used].id);
        }
    }
}

/* One unit, moved by a teleport: the host's own player is the platform's
 * body (s->player, with the camera), everybody else the game's. */
static void teleport_unit(hta_session *s, uint32_t i, const float pos[3], float yaw)
{
    hta_unit *u = &s->game.units[i];
    if ((int32_t)i == s->me) {
        float d[3] = { pos[0] - s->player.pos[0], pos[1] - s->player.pos[1], pos[2] - s->player.pos[2] };
        for (int k = 0; k < 3; k++) { s->player.pos[k] = pos[k]; s->cam.pos[k] += d[k]; s->player.velocity[k] = 0.0f; }
        s->cam.yaw = yaw;
    }
    for (int k = 0; k < 3; k++) { u->body.pos[k] = pos[k]; u->body.velocity[k] = 0.0f; }
    u->eye.pos[0] = pos[0]; u->eye.pos[1] = pos[1]; u->eye.pos[2] = pos[2] + u->body.eye_height;
    u->eye.yaw = yaw;
    s->went_teleports++;
    hta_log("[world] unit %u teleported to (%.2f %.2f %.2f)", i, pos[0], pos[1], pos[2]);
}

/* World entities, every tick. The host senses triggers from where the game
 * put everyone and runs the queued events; everybody moves the movers. */
static void world_entities(hta_session *s, float dt, bool authority)
{
    hta_world_entities *w = &s->went;
    if (!w->loaded) return;
    w->remote = !authority;
    if (authority && s->game_on) {
        for (uint32_t i = 0; i < HTA_WENT_MAX_ACTORS; i++) {
            const hta_unit *u = i < s->game.unit_count ? &s->game.units[i] : NULL;
            bool here = u && u->kind != HTA_UNIT_NONE && u->alive && u->vehicle < 0;
            hta_went_sense(w, (uint8_t)i, here ? u->body.pos : NULL, here);
        }
        /* The script phase (X3): scripted uses and ability presses, whose
         * requests the queue below dispatches this same tick. */
        if (s->script) hta_script_phase(s->script, dt);
        else w->call_count = 0;
    }
    hta_went_step(w, dt);
    /* X7: the event bindings' damage, through the game's own pipeline
     * (hta_game_hurt: shields, health, protection, teams, death, kill feed,
     * score), exactly as Lua's game.damage; no player is credited. */
    for (uint32_t k = 0; authority && k < w->hurt_count; k++) {
        const hta_went_hurt *h = &w->hurts[k];
        if (h->actor >= s->game.unit_count || !s->game.units[h->actor].alive) continue;
        hta_game_hurt(&s->game, h->actor, -1, h->amount, NULL);
        s->went_hurts++;
        hta_log("[world] unit %u hurt %g by %s (binding %s)", h->actor, (double)h->amount, w->defs->entity[h->source].id,
                h->binding ? w->defs->binding[h->binding - 1].id : "?");
    }
    /* X7: a binding's sound goes to the joiners as a world-sound effect
     * (they cannot derive it from state, as they do a mover's). */
    for (uint32_t k = 0; authority && s->net_hosting && k < w->cue_count; k++) {
        const hta_went_cue *c = &w->cues[k];
        if (!c->binding) continue;
        hta_net_fx fx = { .kind = HTA_NET_FX_WORLD_SOUND, .entity = c->entity, .weapon = (uint8_t)(c->sound & 0xFF),
                          .material = (uint8_t)(c->sound >> 8) };
        for (int q = 0; q < 3; q++) fx.pos[q] = c->pos[q];
        hta_net_server_fx(&s->host_server, &fx);
    }
    for (uint32_t k = 0; k < w->trace_count; k++) hta_log("[bind] %s", w->trace_line[k]);
    if (w->trace_dropped) hta_log("[bind] (%u more trace lines this step dropped)", w->trace_dropped);
    w->trace_count = w->trace_dropped = 0;
    /* X5: movers that started to move sound their definition's sound. */
    if (w->cue_count) {
        float fwd[3];
        hta_camera_forward(&s->cam, fwd);
        float right[3] = { fwd[1], -fwd[0], 0.0f }, len = sqrtf(right[0] * right[0] + right[1] * right[1]);
        if (len > 1e-4f) { right[0] /= len; right[1] /= len; } else right[0] = 1.0f;
        hta_world_sounds_play(&s->world_sounds, &s->audio, w, s->cam.pos, right);
        for (uint32_t k = 0; k < w->cue_count; k++) {
            const hta_went_cue *c = &w->cues[k];
            s->world_sounds_heard++;
            const char *sid = c->sound < s->world_ext.assets.sound_count ? s->world_ext.assets.sound[c->sound].id : "?";
            if (c->binding)
                hta_log("[world] sound %s at %s (binding %s)", sid, w->defs->entity[c->entity].id,
                        c->binding <= w->defs->binding_count ? w->defs->binding[c->binding - 1].id : "from the host");
            else
                hta_log("[world] sound %s: %s started %s", sid, w->defs->entity[c->entity].id,
                        w->st[c->entity].phase == HTA_MOVER_OPENING ? "opening" : "closing");
        }
    }
    for (uint32_t k = 0; authority && k < w->teleport_count; k++) {
        const hta_went_teleport *t = &w->teleports[k];
        if (t->actor < s->game.unit_count && s->game.units[t->actor].alive)
            teleport_unit(s, t->actor, t->pos, t->yaw);
    }
    if (w->diag_count != s->went_diag_seen) {
        hta_log("[world] %s (%u problems so far)", w->diag, w->diag_count);
        s->went_diag_seen = w->diag_count;
    }
    if (authority && s->game_on && s->game.mode == HTA_MODE_SCENARIO && !s->game.over)
        for (uint32_t i = 0; i < w->defs->count; i++)
            if (!strcmp(w->defs->entity[i].id, "nightshift:entity/shift_complete") &&
                hta_went_relay_active(w, i) == HTA_WRELAY_ACTIVE) {
                hta_game_complete_scenario(&s->game, "Shift complete");
                hta_log("[scenario] Shift complete at the freight lift");
                break;
            }
}

void hta_session_tick(hta_session *s, float dt, double now, void (*unit_added)(hta_session *))
{
    s->outbox_count = s->prop_outbox_count = 0;
    s->round_restarted = false;
    bool authority = !s->net_enabled || s->net_hosting;
    /* Everybody else: the bots think and fight, their rounds fly, the
     * dead come back and the score is kept. */
    if (s->game_on && authority) {
        world_interact(s);
        if (s->game.mode==HTA_MODE_RACING) hta_racing_tick(s,dt);
        else hta_game_update(&s->game, dt);
        hta_game_event e;
        while (hta_game_pop(&s->game, &e)) {
            hta_wfx_game_event(&s->wfx, &e, &s->game);
            host_send(s, &e, now);
            if (e.kind == HTA_EV_GAME_OVER && s->game.mode != HTA_MODE_SCENARIO)
                s->over_timer = HTA_POSTGAME;
            if (s->outbox_count < sizeof(s->outbox) / sizeof(s->outbox[0]))
                s->outbox[s->outbox_count++] = e;
        }
        if (s->game.mode!=HTA_MODE_RACING && s->over_timer > 0.0f) {
            s->over_timer -= dt;
            if (s->over_timer <= 0.0f) {
                hta_match_nav_props(s);
                hta_game_start(&s->game);
                hta_went_reset(&s->went);
                if (s->script) hta_script_reset(s->script);
                if (s->net_hosting) s->world_round++;
                s->round_restarted = true;
                hta_log("[game] a new game");
            }
        }
    }
    world_entities(s, dt, authority);
    world_fx(s, dt);
    /* A host: its joiners' packets in, their units driven, the world out. */
    if (s->net_enabled && s->net_hosting) {
        uint64_t refused = s->host_server.stats.refused;
        hta_net_server_pump(&s->host_server, now);
        if (s->host_server.stats.refused != refused)
            hta_log("[net] refused a joiner: %s", s->host_server.last_refusal == HTA_NET_REJECT_CONTENT
                    ? "different characters/weapons" : s->host_server.last_refusal == HTA_NET_REJECT_MAP
                    ? "different map" : s->host_server.last_refusal == HTA_NET_REJECT_VERSION
                    ? "different protocol version" : "full");
        hta_host_peers(s, now, unit_added);
        hta_host_world(s);
    }
}
