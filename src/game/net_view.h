/* A LAN joiner's view of a host's match, with no platform in it: what a
 * desktop client (htaplay), a test, or one day the Android joiner itself
 * needs from the protocol.
 *
 *   hta_net_view_init(&v);
 *   each frame:
 *     hta_net_client_pump(&net, now);
 *     hta_net_view_update(&v, &net, now, &local_player, &cam, wfx);   // wfx may be NULL
 *     hta_net_view_send(&v, &net, now, &input, &cam, &local_player);  // at most 20 Hz
 *     for each i: hta_net_view_entity(&v, i, now, &pose) -> draw it
 *
 * What it does, as the Android joiner does:
 * - Sends CONTROL (the host drives our unit from it) and our predicted
 *   state. One-shot actions ride in counters, so a lost packet loses none.
 * - Keeps every WORLD entity's last two snapshots and eases between them.
 * - Reconciles our own prediction with the host's word: a snap past
 *   0.5 wu (or on respawn), else 12% of the way per WORLD.
 * - Replays the host's KILLs (a feed, gibs where the host gibbed), FX
 *   (detonations, wrecks) and GAME (mode, scores, props) into the world
 *   effects, so debris, gibs, props and their sounds match the host's. */
#ifndef HTA_NET_VIEW_H
#define HTA_NET_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include "../engine/camera.h"
#include "../engine/player.h"
#include "../net/session.h"
#include "world_fx.h"

#define HTA_NET_VIEW_FEED 6u
#define HTA_NET_VIEW_FEED_SECONDS 6.0f

typedef struct {
    bool     live;                /* in the host's latest WORLD */
    bool     alive;
    bool     gibbed;              /* the host blew this body apart */
    hta_net_entity from, to;      /* the two snapshots to ease between */
    double   to_at;               /* when `to` arrived */
    double   died_at;
} hta_net_view_entity_state;

typedef struct {
    float    pos[3], yaw, pitch;
    uint8_t  flags, weapon, kind, character;
    float    health, shield;
    bool     alive, gibbed;
    const char *name;
} hta_net_pose;

/* What the player did this frame. */
typedef struct {
    float forward, right;         /* -1..1 */
    bool  jump, fire, crouch, alt;
    bool  melee, grenade, reload, pickup, action, ability;   /* pressed this frame */
    uint8_t weapon_slot;
} hta_net_view_input;

typedef struct {
    hta_net_view_entity_state ent[HTA_NET_MAX_ENTITIES];
    int32_t  me;                  /* my entity id, -1 until the host lists us */
    bool     me_alive, respawned, snapped;
    uint32_t world_tick;
    double   last_world_at;
    uint16_t round;
    /* the match, from WORLD and GAME */
    uint8_t  mode, score_limit, winner_team, over;
    int16_t  team_score[2];
    float    time;
    /* a kill feed */
    char     feed[HTA_NET_VIEW_FEED][96];
    double   feed_at[HTA_NET_VIEW_FEED];
    /* the counters CONTROL carries */
    uint16_t melee, grenade, reload, pickup, action, ability;
    double   last_send;
    bool     props_synced;
    /* diagnostics */
    uint32_t kills, gibs, fx, corrections;
} hta_net_view;

void hta_net_view_init(hta_net_view *v);
/* Drains what the pump received. `local` and `cam` are our predicted body
 * and eye (moved by the caller with hta_player_update); `wfx` receives the
 * host's effects and may be NULL. */
void hta_net_view_update(hta_net_view *v, hta_net_client *net, double now,
                         hta_player *local, hta_camera *cam, hta_world_fx *wfx);
/* CONTROL and STATE to the host, at most every 50 ms. Returns true when
 * it sent. `ready`: we have what we need to be spawned (the Android joiner
 * waits for its class; a plain client is always ready). */
bool hta_net_view_send(hta_net_view *v, hta_net_client *net, double now,
                       const hta_net_view_input *in, const hta_camera *cam,
                       const hta_player *local, bool ready);
/* Entity `i` as it should be drawn now. False when it is not in the match
 * (or it is us). */
bool hta_net_view_entity(const hta_net_view *v, uint32_t i, double now, hta_net_pose *out);
/* The feed's lines younger than HTA_NET_VIEW_FEED_SECONDS, newest first. */
uint32_t hta_net_view_feed(const hta_net_view *v, double now, const char **lines, uint32_t max);

#endif
