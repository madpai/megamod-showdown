#include "racing.h"
#include <math.h>
#include <string.h>

bool hta_racing_join(hta_session *s, uint8_t unit)
{
    if (!s || s->game.mode!=HTA_MODE_RACING || !hta_race_join(&s->race,unit)) return false;
    hta_arcade_reset(&s->race_car[unit],s->race.track.grid[unit],s->race.track.grid_yaw[unit]);
    return true;
}

void hta_racing_leave(hta_session *s, uint8_t unit)
{
    if (s && s->game.mode==HTA_MODE_RACING) hta_race_leave(&s->race,unit);
}

void hta_racing_reset(hta_session *s)
{
    if (!s || s->game.mode!=HTA_MODE_RACING || !s->went.loaded ||
        !s->went.defs->has_racing) return;
    hta_race_track track=s->went.defs->racing;
    bool active[HTA_RACE_MAX_RACERS]={0};
    for (unsigned i=0;i<HTA_RACE_MAX_RACERS;i++)
        active[i]=i<track.grid_count && i<s->game.unit_count &&
                  (s->game.units[i].kind==HTA_UNIT_LOCAL ||
                   s->game.units[i].kind==HTA_UNIT_REMOTE);
    hta_race_init(&s->race,&track,0);
    s->race_ready_wait=0;
    memset(s->race_pad_wait,0,sizeof(s->race_pad_wait));
    memset(s->race_pad_hits,0,sizeof(s->race_pad_hits));
    memset(s->race_peak_speed,0,sizeof(s->race_peak_speed));
    memset(s->race_reset_held,0,sizeof(s->race_reset_held));
    for (unsigned i=0;i<HTA_RACE_MAX_RACERS;i++) if (active[i]) hta_racing_join(s,(uint8_t)i);
    s->game.over=false;
    s->over_timer=0;
}

static void mirror_pose(hta_session *s, unsigned i)
{
    hta_unit *u=&s->game.units[i];
    const hta_arcade_racer *c=&s->race_car[i];
    for (int k=0;k<3;k++) {
        u->body.pos[k]=c->pos[k]; u->body.velocity[k]=c->vel[k];
        u->eye.pos[k]=c->pos[k];
    }
    u->body.on_ground=c->grounded;
    u->eye.yaw=c->yaw; u->eye.pitch=c->pitch;
    u->eye.pos[2]+=u->body.eye_height;
}

void hta_racing_tick(hta_session *s, float dt)
{
    if (!s || s->game.mode!=HTA_MODE_RACING || dt<=0 || !isfinite(dt)) return;
    if (s->race.phase==HTA_RACE_READY) {
        for (unsigned i=0;i<s->race.count;i++) if (s->race.racer[i].active) {
            s->race_ready_wait+=dt;
            if (s->race_ready_wait>=8.0f) hta_race_start(&s->race);
            break;
        }
    }
    hta_race_step(&s->race,fminf(dt,0.1f));
    const hta_race_track *t=&s->race.track;
    float old_pos[HTA_RACE_MAX_RACERS][3];
    for (unsigned i=0;i<s->race.count;i++)
        memcpy(old_pos[i],s->race_car[i].pos,sizeof(old_pos[i]));
    for (unsigned i=0;i<s->race.count;i++) {
        hta_race_entry *entry=&s->race.racer[i];
        if (!entry->active || i>=s->game.unit_count) continue;
        hta_arcade_racer *car=&s->race_car[i];
        hta_unit *u=&s->game.units[i];
        if (s->race.phase==HTA_RACE_GO && !entry->finished) {
            hta_arcade_input in={0};
            if ((int)i==s->me && (!s->net_enabled || s->net_hosting)) in=s->race_local_input;
            else {
                in.throttle=u->in.move.move_forward;
                in.steer=u->in.move.move_right;
                in.drift=u->in.move.crouch;
            }
            /* Down-stick brakes forward motion, then reverses once slow.
             * Apply the same rule for a local driver and a remote peer. */
            float along=car->vel[0]*cosf(car->yaw)+car->vel[1]*sinf(car->yaw);
            in.brake=in.throttle< -0.15f && along>0.75f;
            bool reset_down=((int)i==s->me && s->race_reset_local) ||
                            ((int)i!=s->me && u->in.move.jump);
            bool reset=reset_down && !s->race_reset_held[i];
            s->race_reset_held[i]=reset_down;
            if (reset || car->pos[2]<-10 || !isfinite(car->pos[0]) ||
                fabsf(car->pos[0])>4090 || fabsf(car->pos[1])>4090) {
                float p[3],yaw;
                if (hta_race_recovery(&s->race,(uint8_t)i,p,&yaw)) {
                    hta_arcade_reset(car,p,yaw);
                    memcpy(entry->last_pos,p,sizeof(entry->last_pos));
                }
            } else {
                hta_arcade_step(car,&t->vehicle,in,s->col.built ? &s->col : NULL,dt);
                s->race_peak_speed[i]=fmaxf(s->race_peak_speed[i],
                                            hypotf(car->vel[0],car->vel[1]));
                for (unsigned p=0;p<t->pad_count;p++) {
                    float *wait=&s->race_pad_wait[i][p];
                    *wait=fmaxf(0,*wait-dt);
                    float dx=car->pos[0]-t->pad[p].pos[0],dy=car->pos[1]-t->pad[p].pos[1];
                    if (!*wait && dx*dx+dy*dy<t->pad[p].radius*t->pad[p].radius &&
                        fabsf(car->pos[2]-t->pad[p].pos[2])<1.5f) {
                        hta_arcade_boost_pad(car,&t->vehicle); *wait=1.25f;
                        s->race_pad_hits[i]++;
                    }
                }
                hta_race_advance(&s->race,(uint8_t)i,car->pos);
            }
        }
        mirror_pose(s,i);
    }
    s->race_reset_local=false;
    for (unsigned i=0;i<s->race.count;i++) if (s->race.racer[i].active)
        for (unsigned j=i+1;j<s->race.count;j++) if (s->race.racer[j].active) {
            hta_arcade_contact_swept(&s->race_car[i],&s->race_car[j],
                                     old_pos[i],old_pos[j],&t->vehicle);
            mirror_pose(s,i); mirror_pose(s,j);
        }
    if (s->race.phase==HTA_RACE_RESULTS) {
        s->game.over=true;
        s->over_timer+=dt;
        if (s->over_timer>=8) hta_racing_reset(s);
    }
}
