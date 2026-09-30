#include "arcade_racer.h"
#include <math.h>
#include <string.h>

static float limit(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static bool bounded(float x, float lo, float hi) { return isfinite(x) && x >= lo && x <= hi; }

hta_arcade_tuning hta_arcade_default_tuning(void)
{
    /* First prototype values in MegaMod world units; device feel is unmeasured. */
    return (hta_arcade_tuning){
        .max_speed=38, .reverse_speed=8, .acceleration=18, .brake_acceleration=36,
        .coast_drag=2.5f, .grip=12, .drift_grip=3.2f,
        .steer_low=2.5f, .steer_high=0.9f, .steer_fade_speed=20, .drift_yaw=1.4f,
        .boost_speed=52, .boost_acceleration=30, .boost_seconds=0.7f,
        .jump_gravity=18, .air_steer=0.25f, .wall_restitution=0.12f,
        .wall_speed_loss=0.20f, .radius=0.55f, .height=0.65f,
        .ground_clearance=0.22f
    };
}

bool hta_arcade_tuning_valid(const hta_arcade_tuning *t)
{
    return t && bounded(t->max_speed,2,120) && bounded(t->reverse_speed,0,30) &&
        bounded(t->acceleration,0.1f,100) && bounded(t->brake_acceleration,0.1f,150) &&
        bounded(t->coast_drag,0,30) && bounded(t->grip,0.1f,50) &&
        bounded(t->drift_grip,0.1f,30) && t->drift_grip < t->grip &&
        bounded(t->steer_low,0.1f,6) && bounded(t->steer_high,0.1f,4) &&
        t->steer_high <= t->steer_low && bounded(t->steer_fade_speed,1,120) &&
        bounded(t->drift_yaw,1,3) && bounded(t->boost_speed,t->max_speed,150) &&
        bounded(t->boost_acceleration,0.1f,150) && bounded(t->boost_seconds,0.1f,3) &&
        bounded(t->jump_gravity,1,100) && bounded(t->air_steer,0,1) &&
        bounded(t->wall_restitution,0,0.5f) && bounded(t->wall_speed_loss,0,0.8f) &&
        bounded(t->radius,0.1f,2) && bounded(t->height,0.1f,3) &&
        bounded(t->ground_clearance,0.05f,1);
}

void hta_arcade_reset(hta_arcade_racer *r, const float pos[3], float yaw)
{
    if (!r || !pos || !isfinite(yaw)) return;
    for (int i=0;i<3;i++) if (!isfinite(pos[i]) || fabsf(pos[i])>4096) return;
    memset(r,0,sizeof(*r));
    memcpy(r->pos,pos,sizeof(r->pos));
    r->yaw=yaw;
}

void hta_arcade_boost_pad(hta_arcade_racer *r, const hta_arcade_tuning *t)
{
    if (!r || !hta_arcade_tuning_valid(t)) return;
    r->boost_time=fmaxf(r->boost_time,t->boost_seconds);
}

unsigned hta_arcade_charge_tier(const hta_arcade_racer *r)
{
    if (!r || !r->drifting || r->drift_time<0.35f || r->drift_work<0.45f) return 0;
    if (r->drift_work>=3.5f && r->drift_time>=1.2f) return 3;
    if (r->drift_work>=1.5f && r->drift_time>=0.7f) return 2;
    return 1;
}

static void one_step(hta_arcade_racer *r, const hta_arcade_tuning *t,
                     hta_arcade_input in, const hta_collision *world, float h)
{
    float old[3]; memcpy(old,r->pos,sizeof(old));
    float fx=cosf(r->yaw), fy=sinf(r->yaw);
    float along=r->vel[0]*fx+r->vel[1]*fy;
    bool drift_now=in.drift && r->grounded && along>t->max_speed*0.18f;
    float side=-r->vel[0]*fy+r->vel[1]*fx;
    float speed=hypotf(r->vel[0],r->vel[1]);
    if (drift_now) {
        r->drift_time+=h;
        /* Useful work needs turning and measurable slip, not a held button. */
        if (fabsf(in.steer)>0.18f && fabsf(side)>0.45f)
            r->drift_work+=h*limit(fabsf(side)*fabsf(in.steer),0,10);
    } else if (r->drifting) {
        unsigned tier=hta_arcade_charge_tier(r);
        if (tier) r->boost_time=fmaxf(r->boost_time,t->boost_seconds*(0.6f+0.3f*tier));
        r->boost_tier=tier;
        r->drift_time=r->drift_work=0;
    }
    r->drifting=drift_now;
    float blend=limit(speed/t->steer_fade_speed,0,1);
    float steer=t->steer_low+(t->steer_high-t->steer_low)*blend;
    float motion=limit(fabsf(along)/4,0,1);
    float yaw_delta=in.steer*steer*motion*(drift_now?t->drift_yaw:1)*
        (r->grounded?1:t->air_steer)*h;
    if (along<0) yaw_delta=-yaw_delta;
    r->yaw=remainderf(r->yaw+yaw_delta,6.28318530718f);
    fx=cosf(r->yaw); fy=sinf(r->yaw);
    along=r->vel[0]*fx+r->vel[1]*fy;
    side=-r->vel[0]*fy+r->vel[1]*fx;
    float cap=r->boost_time>0?t->boost_speed:t->max_speed;
    float demand=in.throttle>=0 ? in.throttle*cap : in.throttle*t->reverse_speed;
    float rate=in.brake ? t->brake_acceleration :
        fabsf(in.throttle)>0.01f ? (r->boost_time>0?t->boost_acceleration:t->acceleration) : t->coast_drag;
    if (in.brake) demand=0;
    if (r->grounded) {
        float d=limit(demand-along,-rate*h,rate*h);
        along+=d;
        side*=expf(-(drift_now?t->drift_grip:t->grip)*h);
    }
    r->vel[0]=fx*along-fy*side;
    r->vel[1]=fy*along+fx*side;
    if (r->boost_time>0) r->boost_time=fmaxf(0,r->boost_time-h);
    r->pos[0]+=r->vel[0]*h;
    r->pos[1]+=r->vel[1]*h;
    if (world && world->built) {
        r->vel[2]-=t->jump_gravity*h;
        r->pos[2]+=r->vel[2]*h;
        r->grounded=false;
        float z;
        if (hta_collision_ground(world,r->pos[0],r->pos[1],old[2]+t->height,&z) &&
            r->pos[2]<=z+t->ground_clearance && old[2]>=z-0.4f) {
            r->pos[2]=z+t->ground_clearance;
            float rise=(r->pos[2]-old[2])/h;
            r->vel[2]=rise>0?fminf(rise,t->max_speed*0.5f):0;
            r->grounded=true;
            float above[3]={r->pos[0],r->pos[1],r->pos[2]+t->height};
            float down[3]={0,0,-1},normal[3];
            if (hta_collision_ray(world,above,down,t->height+1,NULL,NULL,normal) && normal[2]>0.3f) {
                float fwd=normal[0]*fx+normal[1]*fy;
                float right=-normal[0]*fy+normal[1]*fx;
                float want_pitch=limit(atan2f(-fwd,normal[2]),-0.6f,0.6f);
                float want_roll=limit(atan2f(-right,normal[2]),-0.6f,0.6f);
                float blend=limit(h*12,0,1);
                r->pitch+=(want_pitch-r->pitch)*blend;
                r->roll+=(want_roll-r->roll)*blend;
            }
        }
        /* Sweep the body centre to catch a thin barrier at high speed. */
        float dir[3]={r->pos[0]-old[0],r->pos[1]-old[1],r->pos[2]-old[2]};
        float length=sqrtf(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
        float n[3], hit[3], at;
        if (length>1e-6f && hta_collision_ray(world,old,dir,1,&at,hit,n) && n[2]<0.4f) {
            r->pos[0]=old[0]+dir[0]*fmaxf(0,at-t->radius/length);
            r->pos[1]=old[1]+dir[1]*fmaxf(0,at-t->radius/length);
            float vn=r->vel[0]*n[0]+r->vel[1]*n[1];
            r->collision_speed=fmaxf(r->collision_speed,-vn);
            if (vn<0) {
                r->vel[0]=(r->vel[0]-(1+t->wall_restitution)*vn*n[0])*(1-t->wall_speed_loss);
                r->vel[1]=(r->vel[1]-(1+t->wall_restitution)*vn*n[1])*(1-t->wall_speed_loss);
            }
            r->boost_time=0;
        }
        float x=r->pos[0], y=r->pos[1];
        hta_collision_depenetrate(world,&x,&y,r->pos[2]-t->ground_clearance,
                                  t->height,t->radius);
        if (hypotf(x-r->pos[0],y-r->pos[1])>0.001f) {
            r->pos[0]=x; r->pos[1]=y;
            r->vel[0]*=1-t->wall_speed_loss;
            r->vel[1]*=1-t->wall_speed_loss;
        }
    } else { r->vel[2]=0; r->grounded=true; }
    if (!r->grounded) {
        r->pitch*=expf(-h*2);
        r->roll*=expf(-h*2);
    }
}

void hta_arcade_step(hta_arcade_racer *r, const hta_arcade_tuning *t,
                     hta_arcade_input in, const hta_collision *world, float dt)
{
    if (!r || !hta_arcade_tuning_valid(t) || !isfinite(dt) || dt<=0) return;
    in.throttle=isfinite(in.throttle)?limit(in.throttle,-1,1):0;
    in.steer=isfinite(in.steer)?limit(in.steer,-1,1):0;
    r->collision_speed=0;
    dt=fminf(dt,0.1f);
    unsigned steps=(unsigned)ceilf(dt*120);
    float h=dt/(float)steps;
    for (unsigned i=0;i<steps;i++) one_step(r,t,in,world,h);
}

void hta_arcade_contact(hta_arcade_racer *a, hta_arcade_racer *b,
                        const hta_arcade_tuning *t)
{
    if (!a || !b || a==b || !hta_arcade_tuning_valid(t)) return;
    float dx=a->pos[0]-b->pos[0],dy=a->pos[1]-b->pos[1];
    float d=hypotf(dx,dy), min=2*t->radius;
    if (d>=min || fabsf(a->pos[2]-b->pos[2])>t->height) return;
    if (d<0.0001f) { dx=1; dy=0; d=1; } else { dx/=d; dy/=d; }
    float push=(min-d)*0.5f;
    a->pos[0]+=dx*push; a->pos[1]+=dy*push;
    b->pos[0]-=dx*push; b->pos[1]-=dy*push;
    float rel=(a->vel[0]-b->vel[0])*dx+(a->vel[1]-b->vel[1])*dy;
    if (rel<0) {
        float impulse=limit(-(1+t->wall_restitution)*rel*0.5f,0,20);
        a->vel[0]+=dx*impulse; a->vel[1]+=dy*impulse;
        b->vel[0]-=dx*impulse; b->vel[1]-=dy*impulse;
        a->collision_speed=b->collision_speed=fmaxf(a->collision_speed,-rel);
    }
}

void hta_arcade_contact_swept(hta_arcade_racer *a, hta_arcade_racer *b,
                              const float old_a[3], const float old_b[3],
                              const hta_arcade_tuning *t)
{
    if (!a || !b || !old_a || !old_b || !hta_arcade_tuning_valid(t)) return;
    float dx=a->pos[0]-b->pos[0], dy=a->pos[1]-b->pos[1];
    if (dx*dx+dy*dy < 4*t->radius*t->radius) {
        hta_arcade_contact(a,b,t);
        return;
    }
    /* Relative segment of the two body centres over this host frame. */
    float rx=old_a[0]-old_b[0], ry=old_a[1]-old_b[1];
    float vx=dx-rx, vy=dy-ry;
    float vv=vx*vx+vy*vy;
    if (vv<1e-8f) return;
    float at=limit(-(rx*vx+ry*vy)/vv,0,1);
    if (at>=1) return;
    float cx=rx+vx*at, cy=ry+vy*at;
    if (cx*cx+cy*cy>=4*t->radius*t->radius) return;
    float az=old_a[2]+(a->pos[2]-old_a[2])*at;
    float bz=old_b[2]+(b->pos[2]-old_b[2])*at;
    if (fabsf(az-bz)>t->height) return;
    /* Rewind only this one frame's crossing, then use the same bounded
     * separation/impulse as a resting overlap. */
    float before=fmaxf(0,at-0.0001f);
    for (int k=0;k<3;k++) {
        a->pos[k]=old_a[k]+(a->pos[k]-old_a[k])*before;
        b->pos[k]=old_b[k]+(b->pos[k]-old_b[k])*before;
    }
    hta_arcade_contact(a,b,t);
}
