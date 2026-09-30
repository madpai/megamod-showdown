/* The host's side of a networked match, per tick (docs/DEDICATED_SERVER.md,
 * stage S1): joiners' controls into their units, and the match out as WORLD,
 * projectiles, vehicles, drops and GAME snapshots. Moved verbatim from
 * platform_android.c onto hta_session, so the phone host and a dedicated
 * server run the same code. The one piece of presentation -- a joiner's
 * unit needs its look uploaded to the GPU -- is the platform's callback. */
#include "host_net.h"
#include "racing.h"
#include "survival.h"
#include "../platform/platform.h"
#include "../script/script.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

void hta_host_peers(hta_session *s, double now, void (*unit_added)(hta_session *s))
{
    if (!s->game_on) return;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) {
        hta_net_peer *p=&s->host_server.peers[i];
        int32_t unit=s->peer_unit[i];
        if (!p->active) {
            if (unit>=0 && unit!=s->me && (uint32_t)unit<HTA_RACE_MAX_RACERS)
                hta_racing_leave(s,(uint8_t)unit);
            if (unit>=0 && unit!=s->me) hta_survival_leave(s,unit);
            if (unit>=0 && unit!=s->me) hta_game_remove(&s->game,unit);
            s->peer_unit[i]=-1;
            s->peer_melee_seen[i]=s->peer_grenade_seen[i]=0;
            s->peer_reload_seen[i]=s->peer_pickup_seen[i]=0;
            s->peer_action_seen[i]=0;
            s->peer_ability_seen[i]=0;
            s->peer_ready[i]=false;
            continue;
        }
        /* Our own client, over loopback: by its id once it has one, and by
         * its HELLO's nonce before -- the tick pumps the server before the
         * client hears WELCOME, and an id check alone gave us a second,
         * remote body for one frame that never left. */
        if (p->player.id==s->net.id || (s->net.nonce && p->nonce==s->net.nonce)) {
            s->peer_unit[i]=(int8_t)s->me;
            continue;
        }
        if (unit<0) {
            char name[HTA_GAME_NAME];
            snprintf(name,sizeof(name),"Player %u",p->player.id);
            unit=hta_game_add(&s->game,HTA_UNIT_REMOTE,name,HTA_TEAM_AUTO);
            if (unit<0) continue;
            s->peer_unit[i]=(int8_t)unit;
            if (s->game.mode==HTA_MODE_RACING && (uint32_t)unit<HTA_RACE_MAX_RACERS)
                hta_racing_join(s,(uint8_t)unit); /* after GO: spectator until next race */
            s->peer_ready[i]=false;
            s->peer_class_reject[i]=false;
            if (!s->game.classes) hta_game_spawn(&s->game,unit);
            else s->game.units[unit].dead_for=1e4f;   /* nobody there to see yet */
            if(s->game.mode==HTA_MODE_SURVIVAL && !hta_survival_join(s,unit,p->identity,false)) {
                hta_game_remove(&s->game,unit);s->peer_unit[i]=-1;p->active=false;continue;
            }
            if (unit_added) unit_added(s);   /* the platform uploads its look */
            hta_log("[net] player %u joined game unit %d",p->player.id,unit);
        }
        hta_unit *u=&s->game.units[unit];
        hta_unit_input *in=&u->in;
        if (!p->has_control || now-p->last_control_at>0.3) {
            in->move.move_forward=in->move.move_right=0.0f;
            in->move.fire=in->move.jump=in->move.crouch=false;
            in->fire2=false;
            continue;
        }
        const hta_net_control *c=&p->control;
        /* Their look and their class, from their own menu. */
        int32_t requested=c->character && c->character<=s->game.character_count ? (int32_t)(c->character-1) : -1;
        bool host_reserved = requested >= 0 && !s->allow_duplicate_heroes &&
            s->next_character == requested && s->me >= 0 &&
            s->game.characters[requested]->unique_limit == 1;
        if (host_reserved || !hta_game_assign_character(&s->game, unit, requested)) {
            s->peer_class_reject[i]=true;
            s->peer_ready[i]=false;
            u->respawn=1.0f;
            continue;
        }
        s->peer_class_reject[i]=false;
        if (s->game.classes && s->game.mode!=HTA_MODE_SURVIVAL) {
            int32_t a=c->loadout[0]==255 ? -1 : c->loadout[0], b=c->loadout[1]==255 ? -1 : c->loadout[1];
            if (a!=u->loadout[0] || b!=u->loadout[1]) hta_game_set_loadout(&s->game,unit,a,b);
            if (!s->peer_ready[i]) {
                if ((c->flags&HTA_NET_READY) && (!s->game.teams || c->team>0)) {
                    if (s->game.teams) u->team=c->team-1;
                    s->peer_ready[i]=true;
                    if (!u->alive) u->respawn=0.0f;   /* in on the next tick */
                } else {
                    u->respawn=1.0f;                  /* still choosing */
                    continue;
                }
            }
        }
        if(s->game.mode==HTA_MODE_SURVIVAL) {
            u->team=HTA_TEAM_RED;
            hta_survival_request(s,unit,c->rpg_action,c->rpg_item,c->rpg_serial);
        }
        in->move.move_forward=c->forward;
        in->move.move_right=c->right;
        in->move.jump=(c->flags&HTA_NET_JUMP)!=0;
        in->move.fire=(c->flags&HTA_NET_TRIGGER)!=0;
        in->move.crouch=(c->flags&HTA_NET_DUCK)!=0;
        in->fire2=(c->flags&HTA_NET_ALT)!=0;
        in->move.look_yaw=in->move.look_pitch=0.0f;
        if (s->game.mode==HTA_MODE_RACING) {
            /* The selected character remains the peer's roster identity.
             * Racing controls only drive the native vehicle; character
             * flight, weapons and abilities cannot fire through it. */
            in->move.fire=false;
            in->fire2=false;
            u->flying=false;
            u->body.fly=false;
            continue;
        }
        if (s->peer_action_seen[i]!=c->action_count) {
            s->peer_action_seen[i]=c->action_count; in->action=true;
        }
        u->eye.yaw=c->yaw; u->eye.pitch=c->pitch;
        hta_body_attr attr=hta_game_body(&s->game,unit);
        u->flying=(c->flags&HTA_NET_FLY) && attr.can_fly && u->vehicle<0;
        const hta_game_weapon *held=hta_game_held(&s->game,unit);
        u->body.fly=u->flying || (held && held->mount && u->vehicle<0);
        u->body.fly_speed=held && held->mount ? held->asset->fly_speed : attr.fly_speed;
        if (s->peer_ability_seen[i]!=c->ability_count) {
            s->peer_ability_seen[i]=c->ability_count;
            hta_session_ability(s,unit);
        }
        if (u->slot!=c->weapon_slot) in->swap=true;
        if (s->peer_melee_seen[i]!=c->melee_count) {
            s->peer_melee_seen[i]=c->melee_count; in->melee=true;
        }
        if (s->peer_grenade_seen[i]!=c->grenade_count) {
            s->peer_grenade_seen[i]=c->grenade_count; in->grenade=true;
        }
        if (s->peer_reload_seen[i]!=c->reload_count) {
            s->peer_reload_seen[i]=c->reload_count; in->reload=true;
        }
        if (s->peer_pickup_seen[i]!=c->pickup_count) {
            s->peer_pickup_seen[i]=c->pickup_count; in->pickup=true;
            /* A joiner puts the flag down with SWAP, which it sends as a
             * pickup: its gun slot does not change while it carries. */
            if (u->flag>=0) in->swap=true;
        }
    }
}

/* What this player carries, into the game's copy of them: the game drops
 * it when they die and tells everybody else what they hold. */
void hta_host_mirror_local(hta_session *s)
{
    if (!s->game_on || s->me<0 || s->me>=(int32_t)s->game.unit_count) return;
    hta_unit *local=&s->game.units[s->me];
    local->slot=s->held_slot&1u;
    /* An imported weapon is its own roster entry, not its base's: by tag
     * alone the broom went back to being a plasma pistol, for everyone. */
    for (unsigned slot=0;s->game.mode!=HTA_MODE_SURVIVAL && slot<2;slot++)
        local->carry[slot].weapon=slot>=s->held_count ? -1 :
            s->held_asset[slot]>=0 ? s->held_asset[slot] : hta_game_weapon_index(&s->game,s->held[slot]);
    local->carry[local->slot].ammo=s->ammo;
    unsigned other=local->slot^1u;
    if (other<HTA_CARRY_MAX && s->held_ammo_set[other]) local->carry[other].ammo=s->held_ammo[other];
    local->grenades=s->nade_count;
    local->powerup=s->powerup;
}

void hta_host_world(hta_session *s)
{
    /* A host with its own player waits for its loopback client; a
     * headless one (no player) has none to wait for. */
    if (!s->game_on || (s->me >= 0 && !s->net.connected)) return;
    hta_host_mirror_local(s);
    if(s->survival.active) for(unsigned p=0;p<HTA_NET_MAX_PLAYERS;p++) {
        int unit=s->peer_unit[p];if(unit<0||unit>=16||!s->survival.bound[unit])continue;
        hta_net_rpg r={0};hta_survival_snapshot(s,unit,&r);hta_net_server_rpg(&s->host_server,p,&r);
    }
    hta_net_world w={0};
    w.time=s->game.time; w.round=s->world_round;
    w.bot_count=(uint8_t)s->bot_count; w.over=s->game.over;
    w.winner=s->game.winner>=0 && s->game.winner<HTA_GAME_MAX_UNITS
        ? (uint8_t)s->game.winner : 255;
    w.score_limit=(uint8_t)s->game.score_limit;
    w.time_limit=(uint8_t)s->time_limit_min;
    w.respawn_time=(uint8_t)s->respawn_delay;
    if (s->items.loaded) {
        w.item_count=(uint8_t)s->items.count;
        for (uint8_t i=0;i<w.item_count;i++) {
            if (s->items.slot[i].present) w.item_present[i>>3]|=(uint8_t)(1u<<(i&7u));
            w.item_choice[i]=(uint8_t)s->items.slot[i].choice;
        }
    }
    for (uint32_t i=0;i<s->game.unit_count && w.count<HTA_NET_MAX_ENTITIES;i++) {
        const hta_unit *u=&s->game.units[i];
        if (u->kind==HTA_UNIT_NONE) continue;
        hta_net_entity *e=&w.entities[w.count++];
        e->id=(uint8_t)i;
        e->kind=u->kind==HTA_UNIT_BOT ? HTA_NET_ENTITY_BOT : HTA_NET_ENTITY_PLAYER;
        e->character=u->character>=0 && u->character<63 ? (uint8_t)(u->character+1) : 0;
        e->peer_id=u->kind==HTA_UNIT_LOCAL ? s->net.id : 0;
        if (u->kind==HTA_UNIT_REMOTE)
            for (unsigned p=0;p<HTA_NET_MAX_PLAYERS;p++)
                if (s->peer_unit[p]==(int8_t)i) {
                    e->peer_id=s->host_server.peers[p].player.id;
                    if (s->peer_class_reject[p]) e->flags|=HTA_NET_ENTITY_CLASS_REJECT;
                }
        if (u->alive) e->flags|=HTA_NET_ENTITY_ALIVE;
        if (u->body.on_ground) e->flags|=HTA_NET_ENTITY_GROUNDED;
        if (u->body.crouch_t>0.5f) e->flags|=HTA_NET_ENTITY_CROUCH;
        if (u->fired) e->flags|=HTA_NET_ENTITY_FIRE;
        if (u->meleed) e->flags|=HTA_NET_ENTITY_MELEE;
        if (u->threw) e->flags|=HTA_NET_ENTITY_GRENADE;
        if (s->game.teams && u->team==HTA_TEAM_BLUE) e->flags|=HTA_NET_ENTITY_BLUE;
        const hta_game_weapon *held=hta_game_held(&s->game,(int32_t)i);
        e->weapon=held ? (uint8_t)(held-s->game.weapons) : 255;
        for (int k=0;k<3;k++) e->pos[k]=u->body.pos[k];
        for (int k=0;k<2;k++) e->velocity[k]=u->body.velocity[k];
        e->yaw=u->eye.yaw; e->pitch=u->eye.pitch;
        e->health=u->vitals.health; e->shield=u->vitals.shield;
        e->score=(int16_t)u->score; e->kills=(int16_t)u->kills;
        e->deaths=(int16_t)u->deaths;
        for (int slot=0;slot<2;slot++)
            e->carry[slot]=u->carry[slot].weapon>=0 ?
                (uint8_t)u->carry[slot].weapon : 255;
        e->slot=(uint8_t)(u->slot&1u);
        e->grenades=(uint8_t)u->grenades;
        e->powerup=u->powerup;
        const hta_ammo *ammo=&u->carry[e->slot].ammo;
        e->ammo_loaded=(uint16_t)ammo->loaded;
        e->ammo_reserve=(uint16_t)ammo->reserve;
        size_t j=0;
        while (j<HTA_NET_ENTITY_NAME-1 && u->name[j]) {
            unsigned char ch=(unsigned char)u->name[j];
            e->name[j]=(char)(ch>=32 && ch<127 ? ch : '?'); j++;
        }
        e->name[j]=0;
    }
    hta_net_server_world(&s->host_server,&w);
    hta_net_projectiles projectiles={0};
    for (uint32_t p=0;p<s->game.pool_count;p++)
        for (uint32_t slot=0;slot<HTA_PROJ_MAX;slot++) {
            const hta_projectile *q=&s->game.pools[p].live[slot];
            if (!q->alive || projectiles.count>=HTA_NET_MAX_PROJECTILES) continue;
            hta_net_projectile *out=&projectiles.live[projectiles.count++];
            out->pool=(uint8_t)p; out->slot=(uint8_t)slot;
            for (int k=0;k<3;k++) { out->pos[k]=q->pos[k]; out->dir[k]=q->dir[k]; }
            out->speed=q->speed;
        }
    const hta_projectiles *local_pools[2]={&s->proj,&s->nades};
    for (uint8_t p=0;p<2;p++) {
        const hta_projectiles *pool=local_pools[p];
        if (!pool->loaded) continue;
        for (uint8_t slot=0;slot<HTA_PROJ_MAX;slot++) {
            const hta_projectile *q=&pool->live[slot];
            if (!q->alive || projectiles.count>=HTA_NET_MAX_PROJECTILES) continue;
            hta_net_projectile *out=&projectiles.live[projectiles.count++];
            out->pool=(uint8_t)(p ? HTA_NET_POOL_HOST_GRENADES : HTA_NET_POOL_HOST_WEAPON);
            out->slot=slot;
            for (int k=0;k<3;k++) { out->pos[k]=q->pos[k]; out->dir[k]=q->dir[k]; }
            out->speed=q->speed;
        }
    }
    hta_net_server_projectiles(&s->host_server,&projectiles);
    if (s->vehicles.loaded) {
        static hta_net_vehicles cars;
        memset(&cars,0,sizeof(cars));
        for (uint32_t i=0;i<s->vehicles.count && cars.count<HTA_NET_MAX_VEHICLES;i++) {
            const hta_vehicle *v=&s->vehicles.cars[i];
            hta_net_vehicle *o=&cars.cars[cars.count++];
            o->index=(uint8_t)i;
            o->flags=(uint8_t)((v->active ? HTA_NET_VEHICLE_ACTIVE : 0) |
                               (v->grounded ? HTA_NET_VEHICLE_GROUNDED : 0) |
                               (v->ctl.driven ? HTA_NET_VEHICLE_DRIVEN : 0));
            for (int k=0;k<3;k++) o->pos[k]=v->pos[k];
            o->yaw=v->yaw; o->pitch=v->pitch; o->roll=v->roll+v->bank;
            o->aim_yaw=v->aim_yaw; o->aim_pitch=v->aim_pitch;
            o->steering=v->steering; o->wheel_spin=v->wheel_spin;
            o->barrel_spin=v->barrel_spin; o->speed=v->speed;
            if (o->speed>300.0f) o->speed=300.0f;
            if (o->speed<-300.0f) o->speed=-300.0f;
            for (unsigned k=0;k<HTA_NET_VEHICLE_SEATS;k++)
                o->occupant[k]=k<HTA_VEHICLE_SEATS && v->occupant[k]>=0 &&
                    v->occupant[k]<(int8_t)HTA_NET_MAX_ENTITIES ? (uint8_t)v->occupant[k] : 255;
            unsigned w=0;
            for (uint32_t k=0;k<v->point_count && w<4;k++) {
                if (!v->points[k].wheel) continue;
                float t=v->points[k].travel;
                o->travel[w++]=t>0.25f ? 0.25f : t<-0.25f ? -0.25f : t;
            }
            for (int k=0;k<3;k++) {
                if (o->pos[k]>327.0f) o->pos[k]=327.0f;
                if (o->pos[k]<-327.0f) o->pos[k]=-327.0f;
            }
        }
        hta_net_server_vehicles(&s->host_server,&cars);
    }
    static hta_net_drops drops;
    memset(&drops,0,sizeof(drops));
    for (int i=0;i<HTA_GAME_MAX_DROPS && drops.count<HTA_NET_MAX_DROPS;i++) {
        const hta_game_drop *d=&s->game.drops[i];
        if (!d->live || d->weapon<0 || d->weapon>=(int32_t)HTA_NET_MAX_WEAPONS) continue;
        drops.drop[drops.count].weapon=(uint8_t)d->weapon;
        for (int k=0;k<3;k++) {
            float v=d->pos[k];
            drops.drop[drops.count].pos[k]=v>327.0f ? 327.0f : v<-327.0f ? -327.0f : v;
        }
        drops.drop[drops.count].yaw=d->yaw;
        drops.count++;
    }
    hta_net_server_drops(&s->host_server,&drops);
    /* The rules beside WORLD: mode, scores, both flags, every hull. */
    static hta_net_game gm;
    memset(&gm,0,sizeof(gm));
    gm.mode=(uint8_t)(s->game.mode<HTA_MODE_COUNT ? s->game.mode : 0);
    gm.options=(s->game.classes ? HTA_NET_GAME_CLASSES : 0) |
               (s->game.allow_duplicate_heroes ? HTA_NET_GAME_DUPLICATES : 0);
    gm.score_limit=(uint8_t)(s->game.score_limit>255 ? 255 : s->game.score_limit<0 ? 0 : s->game.score_limit);
    for (int t=0;t<2;t++) {
        int sc=s->game.team_score[t];
        gm.team_score[t]=(int16_t)(sc>32767 ? 32767 : sc<-32767 ? -32767 : sc);
        const hta_game_flag *f=&s->game.flags[t];
        gm.flag[t].present=f->present ? 1 : 0;
        gm.flag[t].state=f->state<=HTA_FLAG_DROPPED ? f->state : HTA_FLAG_HOME;
        gm.flag[t].carrier=f->state==HTA_FLAG_CARRIED && f->carrier>=0 &&
            f->carrier<(int32_t)HTA_NET_MAX_ENTITIES ? (uint8_t)f->carrier : 255;
        for (int k=0;k<3;k++) {
            float v=f->pos[k];
            gm.flag[t].pos[k]=v>327.0f ? 327.0f : v<-327.0f ? -327.0f : v;
        }
        gm.flag[t].yaw=f->yaw;
    }
    gm.winner_team=s->game.over && s->game.winner_team>=0 ? (uint8_t)s->game.winner_team : 255;
    for (uint32_t i=0;i<s->vehicles.count && i<HTA_NET_MAX_VEHICLES;i++) {
        float h=hta_game_hull(&s->game,(int32_t)i);
        gm.hull[i]=!s->vehicles.cars[i].active ? 0 :
                   (uint8_t)(h*254.0f+1.0f>255.0f ? 255.0f : h*254.0f+1.0f);
    }
    if (s->wfx.ready)
        gm.prop_count=(uint16_t)hta_props_broken_mask(&s->wfx.props,gm.prop_broken,HTA_NET_MAX_PROPS);
    if (s->game.mode==HTA_MODE_RACING) {
        gm.race_phase=(uint8_t)s->race.phase;
        gm.race_countdown=(uint8_t)fminf(30.0f,ceilf(fmaxf(0,s->race.countdown)*10.0f));
        gm.race_elapsed=(uint16_t)fminf(60000.0f,s->race.elapsed*100.0f);
        for (unsigned i=0;i<s->race.count && i<8;i++) {
            const hta_race_entry *e=&s->race.racer[i];
            if (!e->active) continue;
            gm.race[i].lap=e->lap;
            gm.race[i].next_gate=e->next_gate;
            gm.race[i].flags=(uint8_t)(1u|(e->finished?2u:0u)|
                                (s->race_car[i].drifting?4u:0u)|(e->wrong_way?8u:0u)|
                                (s->race_car[i].boost_time>0?16u:0u));
            gm.race[i].finish_order=e->finish_order;
            gm.race[i].boost_tier=(uint8_t)(s->race_car[i].boost_time>0 ?
                                           s->race_car[i].boost_tier : 0);
            gm.race[i].position=(uint8_t)hta_race_position(&s->race,(uint8_t)i);
        }
    }
    hta_net_server_game(&s->host_server,&gm);
    /* The world's replicated state as it is now (X8): every mover's phase
     * and progress and every relay's flag, by replication index. A joiner,
     * late or not, takes the state and never replays how it came about; the
     * server decides when it goes out (net/session.h). */
    /* A host-only world still sends the five-byte empty state. A newcomer
     * can then mark it synced, and its wire cost matches the contract. */
    if (s->went.loaded) {
        static hta_went_mover_state ms[HTA_WREP_MAX_SPATIAL];
        static hta_net_world_state ws;
        memset(&ws,0,sizeof(ws));
        uint32_t n=hta_went_snapshot(&s->went,ms,HTA_NET_WSTATE_MAX_SPATIAL);
        ws.spatial_total=(uint16_t)n;
        for (uint32_t i=0;i<n;i++) {
            ws.phase[i]=ms[i].phase; ws.t[i]=ms[i].t_q;
            hta_net_bit_set(ws.spatial_has,i,true);
        }
        uint32_t f=hta_went_flags(&s->went,ws.flag,HTA_NET_WSTATE_MAX_FLAGS);
        ws.flag_total=(uint16_t)f;
        for (uint32_t i=0;i<f;i++) hta_net_bit_set(ws.flag_has,i,true);
        hta_net_server_world_state(&s->host_server,&ws);
    }
}


void hta_session_ability(hta_session *s, int32_t unit)
{
    if(s->game.mode==HTA_MODE_SURVIVAL) {hta_survival_cast(s,unit);return;}
    if (hta_game_ability(&s->game, unit)) return;
    if (s->script && hta_game_ability_charge(&s->game, unit) < 0.0f) hta_script_ability(s->script, unit);
}
