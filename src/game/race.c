#include "race.h"
#include <math.h>
#include <string.h>

static bool point(const float p[3])
{
    return p && isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) &&
        fabsf(p[0])<=4096 && fabsf(p[1])<=4096 && fabsf(p[2])<=4096;
}

bool hta_race_track_valid(const hta_race_track *t)
{
    if (!t || t->gate_count<2 || t->gate_count>HTA_RACE_MAX_CHECKPOINTS ||
        t->lap_count<1 || t->lap_count>9 || t->grid_count<1 ||
        t->grid_count>HTA_RACE_MAX_RACERS || t->pad_count>HTA_RACE_MAX_PADS ||
        !hta_arcade_tuning_valid(&t->vehicle)) return false;
    for (unsigned i=0;i<t->gate_count;i++) {
        const hta_race_gate *g=&t->gate[i];
        float len=hypotf(g->forward[0],g->forward[1]);
        if (!point(g->pos) || !point(g->recovery) || !isfinite(g->recovery_yaw) ||
            !isfinite(len) || fabsf(len-1)>0.01f || !isfinite(g->half_width) ||
            g->half_width<0.5f || g->half_width>30 || !isfinite(g->half_height) ||
            g->half_height<0.5f || g->half_height>10) return false;
        if (i) {
            float dx=g->pos[0]-t->gate[i-1].pos[0],dy=g->pos[1]-t->gate[i-1].pos[1];
            float d=hypotf(dx,dy);
            if (d<2 || d>1000) return false;
        }
    }
    float end_dx=t->gate[0].pos[0]-t->gate[t->gate_count-1].pos[0];
    float end_dy=t->gate[0].pos[1]-t->gate[t->gate_count-1].pos[1];
    if (hypotf(end_dx,end_dy)<2 || hypotf(end_dx,end_dy)>1000) return false;
    for (unsigned i=0;i<t->grid_count;i++)
        if (!point(t->grid[i]) || !isfinite(t->grid_yaw[i])) return false;
    for (unsigned i=0;i<t->pad_count;i++)
        if (!point(t->pad[i].pos) || !isfinite(t->pad[i].radius) ||
            t->pad[i].radius<0.5f || t->pad[i].radius>20) return false;
    return true;
}

bool hta_race_init(hta_race *r, const hta_race_track *t, uint8_t racers)
{
    if (!r || !hta_race_track_valid(t) || racers>t->grid_count) return false;
    memset(r,0,sizeof(*r));
    r->track=*t; r->count=racers; r->phase=HTA_RACE_READY;
    for (unsigned i=0;i<racers;i++) hta_race_join(r,(uint8_t)i);
    return true;
}

bool hta_race_join(hta_race *r, uint8_t slot)
{
    if (!r || slot>=r->track.grid_count || r->phase>=HTA_RACE_GO ||
        r->racer[slot].active) return false;
    hta_race_entry *e=&r->racer[slot];
    memset(e,0,sizeof(*e)); e->active=true; e->lap=1; e->next_gate=1;
    memcpy(e->last_pos,r->track.grid[slot],sizeof(e->last_pos));
    if (r->count<=slot) r->count=(uint8_t)(slot+1);
    return true;
}

void hta_race_leave(hta_race *r, uint8_t slot)
{
    if (!r || slot>=r->count || !r->racer[slot].active) return;
    if (r->racer[slot].finished && r->finished_count) r->finished_count--;
    r->racer[slot].active=false;
}

void hta_race_start(hta_race *r)
{
    if (r && r->phase==HTA_RACE_READY) { r->phase=HTA_RACE_COUNTDOWN; r->countdown=3; }
}

void hta_race_step(hta_race *r, float dt)
{
    if (!r || !isfinite(dt) || dt<=0 || dt>0.1f) return;
    if (r->phase==HTA_RACE_COUNTDOWN) {
        r->countdown-=dt;
        if (r->countdown<=0) { r->countdown=0; r->phase=HTA_RACE_GO; }
    } else if (r->phase==HTA_RACE_GO) {
        r->elapsed+=dt;
        unsigned active=0;
        for (unsigned i=0;i<r->count;i++) active+=r->racer[i].active;
        if ((active && r->finished_count==active) ||
            (r->finished_count && r->elapsed>=r->result_wait)) r->phase=HTA_RACE_RESULTS;
    }
}

static bool crossed(const hta_race_gate *g, const float a[3], const float b[3])
{
    float ax=a[0]-g->pos[0], ay=a[1]-g->pos[1];
    float bx=b[0]-g->pos[0], by=b[1]-g->pos[1];
    float da=ax*g->forward[0]+ay*g->forward[1];
    float db=bx*g->forward[0]+by*g->forward[1];
    if (!(da<0 && db>=0) || db-da<0.0001f) return false;
    float fraction=-da/(db-da);
    float sx=ax+(bx-ax)*fraction, sy=ay+(by-ay)*fraction;
    float lateral=-sx*g->forward[1]+sy*g->forward[0];
    float height=a[2]+(b[2]-a[2])*fraction-g->pos[2];
    return fabsf(lateral)<=g->half_width && fabsf(height)<=g->half_height;
}

bool hta_race_advance(hta_race *r, uint8_t who, const float pos[3])
{
    if (!r || who>=r->count || !point(pos)) return false;
    hta_race_entry *e=&r->racer[who];
    bool advanced=false;
    if (r->phase==HTA_RACE_GO && e->active && !e->finished) {
        float sample_dt=fmaxf(0,fminf(0.1f,r->elapsed-e->last_sample_time));
        e->last_sample_time=r->elapsed;
        const hta_race_gate *g=&r->track.gate[e->next_gate];
        advanced=crossed(g,e->last_pos,pos);
        if (advanced) {
            e->next_gate=(uint8_t)((e->next_gate+1)%r->track.gate_count);
            e->progress=0;
            if (e->next_gate==1) {
                if (e->lap==r->track.lap_count) {
                    e->finished=true;
                    e->finish_time=r->elapsed;
                    e->finish_order=++r->finish_order_next;
                    r->finished_count++;
                    if (r->finished_count==1) r->result_wait=r->elapsed+30;
                } else e->lap++;
            }
        }
        unsigned prev=(e->next_gate+r->track.gate_count-1)%r->track.gate_count;
        const float *a=r->track.gate[prev].pos,*b=r->track.gate[e->next_gate].pos;
        float dx=b[0]-a[0],dy=b[1]-a[1],size=dx*dx+dy*dy;
        if (size>1) {
            float fraction=((pos[0]-a[0])*dx+(pos[1]-a[1])*dy)/size;
            float next=fmaxf(0,fminf(0.99f,fraction));
            float along=((pos[0]-e->last_pos[0])*dx+(pos[1]-e->last_pos[1])*dy)/sqrtf(size);
            if (along<-0.05f && !advanced) e->wrong_way_time+=sample_dt;
            else e->wrong_way_time=fmaxf(0,e->wrong_way_time-sample_dt*1.5f);
            e->wrong_way=e->wrong_way_time>1.2f;
            e->progress=next;
        }
    }
    memcpy(e->last_pos,pos,sizeof(e->last_pos));
    return advanced;
}

static float score(const hta_race *r, const hta_race_entry *e)
{
    if (e->finished) return 100000-(float)e->finish_order*100;
    return (float)(e->lap-1)*r->track.gate_count+
           (float)((e->next_gate+r->track.gate_count-1)%r->track.gate_count)+
           e->progress;
}

unsigned hta_race_position(const hta_race *r, uint8_t who)
{
    if (!r || who>=r->count || !r->racer[who].active) return 0;
    unsigned pos=1; float mine=score(r,&r->racer[who]);
    for (unsigned i=0;i<r->count;i++) if (i!=who && r->racer[i].active) {
        float other=score(r,&r->racer[i]);
        if (other>mine || (other==mine && i<who)) pos++;
    }
    return pos;
}

bool hta_race_recovery(const hta_race *r, uint8_t who, float pos[3], float *yaw)
{
    if (!r || who>=r->count || !pos || !yaw) return false;
    const hta_race_entry *e=&r->racer[who];
    if (!e->active) return false;
    unsigned previous=(e->next_gate+r->track.gate_count-1)%r->track.gate_count;
    if (e->lap==1 && e->next_gate==1) {
        memcpy(pos,r->track.grid[who],sizeof(r->track.grid[who]));
        *yaw=r->track.grid_yaw[who];
    } else {
        memcpy(pos,r->track.gate[previous].recovery,sizeof(r->track.gate[previous].recovery));
        *yaw=r->track.gate[previous].recovery_yaw;
    }
    return true;
}
