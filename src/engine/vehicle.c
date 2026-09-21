#include "vehicle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAD 0.01745329252f
static float clamp(float x, float a, float b) { return fmaxf(a, fminf(b, x)); }
static float approach(float x, float to, float step)
{ return x < to ? fminf(to, x+step) : fmaxf(to, x-step); }
static bool tag_offset(const hta_cache *c, uint32_t id, uint32_t cls, uint32_t *off)
{
    int32_t i = hta_cache_find_tag_by_id(c, id); hta_tag_entry t;
    return i >= 0 && hta_cache_tag(c, (uint32_t)i, &t) &&
        (!cls || t.primary_class == cls) && hta_cache_ptr_to_offset(c, t.tag_data_ptr, off);
}
static bool array(const hta_cache *c, uint32_t off, uint32_t *count, uint32_t *arr)
{
    uint32_t ptr;
    return hta_read_reflexive(c, off, count, &ptr) && *count &&
        hta_cache_ptr_to_offset(c, ptr, arr);
}
static void rotate(const hta_vehicle *v, const float p[3], float out[3])
{
    float cy=cosf(v->yaw), sy=sinf(v->yaw), cp=cosf(v->pitch), sp=sinf(v->pitch);
    float cr=cosf(v->roll), sr=sinf(v->roll);
    float y=p[1]*cr-p[2]*sr, z=p[1]*sr+p[2]*cr;
    float x=p[0]*cp+z*sp;
    out[0]=x*cy-y*sy; out[1]=x*sy+y*cy; out[2]=-p[0]*sp+z*cp;
}
static void place(const hta_vehicle *v, const float p[3], float out[3])
{ rotate(v,p,out); for(int k=0;k<3;k++)out[k]+=v->pos[k]; }

bool hta_vehicle_read(hta_vehicle *v, const hta_cache *c, uint32_t tag)
{
    uint32_t off; uint16_t type=0;
    if (!v || !tag_offset(c,tag,HTA_TAG_VEHI,&off) ||
        !hta_rd_u16(c,off+756,&type) || type!=1) return false; /* human jeep */
    memset(v,0,sizeof(*v)); v->tag_id=tag;
    hta_rd_u32(c,off+52,&v->model_id);
    /* Vehicle reconciles at 1008, Unit at 752. Speed is wu/tick;
     * acceleration is change in that speed per tick, hence 30 squared. */
    float *values[]={&v->forward,&v->reverse,&v->accel,&v->decel,
                    &v->turn_left,&v->turn_right,&v->circumference,&v->turn_rate};
    for(uint32_t i=0;i<8;i++)
        if(!hta_rd_f32(c,off+760+i*4,values[i]) || !isfinite(*values[i]))return false;
    v->forward*=HTA_TICK_HZ; v->reverse*=HTA_TICK_HZ;
    v->accel*=HTA_TICK_HZ*HTA_TICK_HZ; v->decel*=HTA_TICK_HZ*HTA_TICK_HZ;
    v->turn_left*=RAD; v->turn_right*=RAD; v->turn_rate*=RAD;
    if(!(v->forward>0 && v->reverse>0 && v->accel>0 && v->decel>0 && v->turn_rate>0))return false;
    uint32_t count,arr; bool driver=false;
    if(!array(c,off+740,&count,&arr) || count>64)return false;
    for(uint32_t i=0;i<count;i++) {
        uint32_t flags=0; hta_rd_u32(c,arr+i*284,&flags);
        if(!(flags&(1u<<2)) || (flags&(1u<<1)))continue;
        char marker[33]={0}; hta_rd_bytes(c,arr+i*284+36,marker,32);
        driver=hta_model_marker_position(c,v->model_id,marker,v->seat);
        if(driver)break;
    }
    if(!driver)return false;
    uint32_t pid=0,po; hta_rd_u32(c,off+140,&pid);
    if(!tag_offset(c,pid,HTA_FOURCC('p','h','y','s'),&po))return false;
    hta_rd_f32(c,po+28,&v->gravity_scale);
    if(!isfinite(v->gravity_scale) || v->gravity_scale<=0)v->gravity_scale=1;
    if(!array(c,po+116,&count,&arr) || count>HTA_VEHICLE_MASS_POINTS)return false;
    float front=-INFINITY,back=INFINITY; uint32_t wheels=0;
    for(uint32_t i=0;i<count;i++) {
        hta_vehicle_point *p=&v->points[v->point_count]; uint16_t powered=0xffff;
        hta_rd_u16(c,arr+i*128+32,&powered); p->wheel=powered!=0xffff;
        for(int k=0;k<3;k++)
            if(!hta_rd_f32(c,arr+i*128+56+k*4,&p->pos[k]) || !isfinite(p->pos[k]))return false;
        hta_rd_f32(c,arr+i*128+104,&p->radius);
        if(!isfinite(p->radius) || p->radius<=0)return false;
        if(p->wheel){front=fmaxf(front,p->pos[0]);back=fminf(back,p->pos[0]);wheels++;}
        v->body_radius=fmaxf(v->body_radius,hypotf(p->pos[0],p->pos[1])+p->radius);
        v->point_count++;
    }
    v->wheelbase=front-back;
    return wheels>=4 && v->wheelbase>0.1f;
}

void hta_vehicles_free(hta_vehicles *v)
{
    if(!v)return;
    hta_collision_free(&v->collision); hta_bsp_free(&v->mesh); hta_bsp_free(&v->coll_mesh);
    free(v->rest);free(v->coll_rest);memset(v,0,sizeof(*v));v->driver=-1;
}
static void pose_mesh(hta_bsp_mesh *m,const hta_vertex *rest,const hta_vehicle *v,
                       uint32_t first,uint32_t count)
{
    float basis[3][3];
    for (int k=0;k<3;k++) { float axis[3]={0};axis[k]=1;rotate(v,axis,basis[k]); }
    for(uint32_t i=first;i<first+count;i++) {
        m->vertices[i]=rest[i];
        for(int k=0;k<3;k++) {
            m->vertices[i].pos[k]=v->pos[k]+basis[0][k]*rest[i].pos[0]+
                basis[1][k]*rest[i].pos[1]+basis[2][k]*rest[i].pos[2];
            m->vertices[i].normal[k]=basis[0][k]*rest[i].normal[0]+
                basis[1][k]*rest[i].normal[1]+basis[2][k]*rest[i].normal[2];
        }
    }
}
void hta_vehicles_pose(hta_vehicles *v)
{
    if(!v || !v->loaded)return;
    for(uint32_t i=0;i<v->count;i++) {
        hta_vehicle *car=&v->cars[i];
        pose_mesh(&v->mesh,v->rest,car,car->first_vertex,car->vertex_count);
        pose_mesh(&v->coll_mesh,v->coll_rest,car,car->first_coll,car->coll_count);
    }
    for(int k=0;k<3;k++) {v->coll_mesh.bounds_min[k]=INFINITY;v->coll_mesh.bounds_max[k]=-INFINITY;}
    for(uint32_t i=0;i<v->coll_mesh.vertex_count;i++)for(int k=0;k<3;k++) {
        float p=v->coll_mesh.vertices[i].pos[k];
        v->coll_mesh.bounds_min[k]=fminf(v->coll_mesh.bounds_min[k],p);
        v->coll_mesh.bounds_max[k]=fmaxf(v->coll_mesh.bounds_max[k],p);
    }
    hta_collision_free(&v->collision);
    hta_collision_build(&v->collision,&v->coll_mesh);
    v->upload_frames=8; /* every in-flight vertex slot */
}
bool hta_vehicles_load(hta_vehicles *v,const hta_cache *c,const hta_resource_map *bm,
                        char *err,size_t n)
{
    if(!v || !c)return false;
    memset(v,0,sizeof(*v));v->driver=-1;
    uint32_t off,count,arr,pn,pa;
    if(!tag_offset(c,c->scenario_tag_id,HTA_TAG_SCNR,&off) ||
       !array(c,off+HTA_SCENARIO_VEHICLES_OFF,&count,&arr) ||
       !array(c,off+HTA_SCENARIO_VEHICLE_PAL,&pn,&pa))return false;
    v->mesh.textures=calloc(256,sizeof(*v->mesh.textures));
    if(!v->mesh.textures)goto fail;
    for(uint32_t i=0;i<count && i<HTA_VEHICLE_PLACEMENTS;i++) {
        uint16_t type; uint32_t id=0;
        if(!hta_rd_u16(c,arr+i*120,&type) || type>=pn)continue;
        hta_rd_u32(c,pa+type*48+12,&id);
        hta_vehicle car;
        if(!hta_vehicle_read(&car,c,id))continue;
        if(v->count>=HTA_VEHICLE_MAX)goto fail;
        car.placement=i;
        for(int k=0;k<3;k++)hta_rd_f32(c,arr+i*120+8+k*4,&car.pos[k]);
        hta_rd_f32(c,arr+i*120+20,&car.yaw);
        hta_rd_f32(c,arr+i*120+24,&car.pitch);hta_rd_f32(c,arr+i*120+28,&car.roll);
        car.first_vertex=v->mesh.vertex_count;car.first_coll=v->coll_mesh.vertex_count;
        if(!hta_model_instance(&v->mesh,c,bm,car.model_id,NULL,NULL,err,n) ||
           !hta_model_collision_instance(&v->coll_mesh,c,id))goto fail;
        car.vertex_count=v->mesh.vertex_count-car.first_vertex;
        car.coll_count=v->coll_mesh.vertex_count-car.first_coll;
        v->cars[v->count++]=car;v->skip[i]=1;
    }
    if(!v->count)goto fail;
    v->rest=malloc(v->mesh.vertex_count*sizeof(*v->rest));
    v->coll_rest=malloc(v->coll_mesh.vertex_count*sizeof(*v->coll_rest));
    if(!v->rest || !v->coll_rest)goto fail;
    memcpy(v->rest,v->mesh.vertices,v->mesh.vertex_count*sizeof(*v->rest));
    memcpy(v->coll_rest,v->coll_mesh.vertices,v->coll_mesh.vertex_count*sizeof(*v->coll_rest));
    v->loaded=true;hta_vehicles_pose(v);
    if(!v->collision.built)goto fail;
    if(err)snprintf(err,n,"%u drivable human jeeps",v->count);
    return true;
fail:
    hta_vehicles_free(v);
    if(err)snprintf(err,n,"vehicle load failed; keeping static placements");
    return false;
}

int32_t hta_vehicles_near(const hta_vehicles *v,const hta_collision *world,const float feet[3])
{
    if(!v || !v->loaded || v->driver>=0 || !feet)return -1;
    float best=HTA_VEHICLE_ENTER_REACH;int32_t result=-1;
    hta_collision terrain={0};if(world){terrain=*world;terrain.extra=NULL;}
    for(uint32_t i=0;i<v->count;i++) {
        const hta_vehicle *car=&v->cars[i];if(fabsf(car->speed)>HTA_VEHICLE_EXIT_SPEED)continue;
        float seat[3];place(car,car->seat,seat);
        float d[3]={seat[0]-feet[0],seat[1]-feet[1],seat[2]-feet[2]-0.35f};
        float dist=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(dist>=best)continue;
        float eye[3]={feet[0],feet[1],feet[2]+0.35f};
        if(hta_collision_ray(&terrain,eye,d,1,NULL,NULL,NULL))continue;
        best=dist;result=(int32_t)i;
    }
    return result;
}
bool hta_vehicles_enter(hta_vehicles *v,int32_t car)
{
    if(!v || !v->loaded || v->driver>=0 || car<0 || (uint32_t)car>=v->count)return false;
    v->driver=car;v->look_yaw=0;v->look_pitch=-0.15f;return true;
}

/* Check exits against the entire current world (including this vehicle),
 * headroom and the swept path from the door. Refuse unsafe or moving exits. */
bool hta_vehicles_exit(hta_vehicles *v,const hta_collision *world,hta_player *p,hta_camera *cam)
{
    if(!v || v->driver<0 || !world || !p || !cam)return false;
    hta_vehicle *car=&v->cars[v->driver];
    if(fabsf(car->speed)>HTA_VEHICLE_EXIT_SPEED || !car->grounded)return false;
    const float candidates[4][3]={{0,1.1f,0.3f},{0,-1.1f,0.3f},{-1.5f,0,0.3f},{1.5f,0,0.3f}};
    for(int i=0;i<4;i++) {
        float at[3],z;place(car,candidates[i],at);
        if(!hta_collision_ground(world,at[0],at[1],at[2],&z) || fabsf(z-car->pos[2])>0.8f)continue;
        float x=at[0],y=at[1];
        hta_collision_depenetrate(world,&x,&y,z,p->phys.coll_stand,p->radius);
        if(hypotf(x-at[0],y-at[1])>HTA_VEHICLE_CLEARANCE)continue;
        float up[3]={0,0,1},origin[3]={x,y,z+HTA_VEHICLE_CLEARANCE};
        bool blocked=false;
        for(int q=0;q<5;q++) {
            origin[0]=x+(q==1?p->radius:q==2?-p->radius:0);
            origin[1]=y+(q==3?p->radius:q==4?-p->radius:0);
            if(hta_collision_ray(world,origin,up,p->phys.coll_stand,NULL,NULL,NULL))blocked=true;
        }
        hta_collision terrain=*world;terrain.extra=NULL;
        float start[3];place(car,car->seat,start);
        float d[3]={x-start[0],y-start[1],z+p->phys.cam_stand-start[2]};
        if(blocked || hta_collision_ray(&terrain,start,d,1,NULL,NULL,NULL))continue;
        p->pos[0]=x;p->pos[1]=y;p->pos[2]=z;memset(p->velocity,0,sizeof(p->velocity));
        p->on_ground=true;p->landed=false;p->eye_height=p->phys.cam_stand;p->crouch_t=0;
        cam->yaw=car->yaw+v->look_yaw;cam->pitch=0;
        memcpy(cam->pos,p->pos,sizeof(p->pos));cam->pos[2]+=p->eye_height;
        car->speed=0;v->driver=-1;return true;
    }
    return false;
}

static bool blocked(const hta_vehicles *fleet,uint32_t index,const hta_vehicle *next,
                      const hta_vehicle *old,const hta_collision *world)
{
    for(uint32_t k=0;k<next->point_count;k++) {
        const hta_vehicle_point *pt=&next->points[k];float at[3],prev[3];
        place(next,pt->pos,at);place(old,pt->pos,prev);
        float x=at[0],y=at[1];
        hta_collision_depenetrate(world,&x,&y,at[2]-pt->radius+HTA_VEHICLE_CLEARANCE,
                                  pt->radius*2,pt->radius);
        if(hypotf(x-at[0],y-at[1])>HTA_VEHICLE_CLEARANCE)return true;
        float d[3]={at[0]-prev[0],at[1]-prev[1],at[2]-prev[2]};
        if(hta_collision_ray(world,prev,d,1,NULL,NULL,NULL))return true;
        for(uint32_t j=0;j<fleet->count;j++) {
            if(j==index)continue;
            const hta_vehicle *other=&fleet->cars[j];
            float separation=next->body_radius+other->body_radius;
            if (hypotf(next->pos[0]-other->pos[0],next->pos[1]-other->pos[1])>separation)continue;
            for(uint32_t q=0;q<other->point_count;q++) {
                float b[3];place(other,other->points[q].pos,b);
                float dx=at[0]-b[0],dy=at[1]-b[1],dz=at[2]-b[2];
                float r=pt->radius+other->points[q].radius;
                if(dx*dx+dy*dy+dz*dz<r*r)return true;
            }
        }
    }
    return false;
}
static void support(hta_vehicle *v,const hta_collision *world,float gravity,float dt)
{
    bool was_grounded = v->grounded;
    float front=0,back=0,left=0,right=0;unsigned nf=0,nb=0,nl=0,nr=0;
    float target=-INFINITY,width=0;
    for(uint32_t k=0;k<v->point_count;k++) {
        const hta_vehicle_point *pt=&v->points[k];if(!pt->wheel)continue;
        float at[3],z;place(v,pt->pos,at);
        if(!hta_collision_ground(world,at[0],at[1],at[2]+pt->radius,&z))continue;
        target=fmaxf(target,z+pt->radius-(at[2]-v->pos[2]));
        if(pt->pos[0]>0){front+=z;nf++;}else{back+=z;nb++;}
        if(pt->pos[1]>0){left+=z;nl++;}else{right+=z;nr++;}
        width=fmaxf(width,fabsf(pt->pos[1])*2);
    }
    v->fall_speed-=gravity*v->gravity_scale*dt;
    v->pos[2]+=v->fall_speed*dt;v->grounded=false;
    if(isfinite(target) && (v->pos[2]<=target ||
        (was_grounded && v->pos[2]-target<=HTA_VEHICLE_CLEARANCE)) && target-v->pos[2]<0.5f) {
        v->pos[2]=target;v->fall_speed=0;v->grounded=true;
        if(nf && nb && nl && nr && width>0.1f) {
            float pitch=-atan2f(front/nf-back/nb,v->wheelbase);
            float roll=atan2f(left/nl-right/nr,width);
            v->pitch=approach(v->pitch,clamp(pitch,-HTA_VEHICLE_MAX_SLOPE,HTA_VEHICLE_MAX_SLOPE),dt*2);
            v->roll=approach(v->roll,clamp(roll,-HTA_VEHICLE_MAX_SLOPE,HTA_VEHICLE_MAX_SLOPE),dt*2);
        }
    }
}
void hta_vehicles_update(hta_vehicles *v,const hta_collision *world,float throttle,
                         float steer,bool brake,float gravity,float dt)
{
    if(!v || !v->loaded || !world || !world->built || !(dt>0))return;
    hta_collision terrain=*world;terrain.extra=NULL;
    dt=fminf(dt,0.1f);bool changed=false;
    unsigned steps=(unsigned)ceilf(dt/HTA_VEHICLE_STEP);float h=dt/steps;
    for(unsigned step=0;step<steps;step++)for(uint32_t i=0;i<v->count;i++) {
        hta_vehicle *car=&v->cars[i],old=*car;
        bool driven=(int32_t)i==v->driver;
        if (!driven && car->grounded && car->speed == 0.0f) continue;
        float gas=driven?clamp(throttle,-1,1):0;
        float turn=driven?clamp(steer,-1,1):0;
        float target=gas>=0?gas*car->forward:gas*car->reverse;
        float rate=car->accel;
        if(!driven || brake || fabsf(gas)<0.01f || gas*car->speed<0){rate=car->decel;target=0;}
        if(car->grounded)car->speed=approach(car->speed,target,rate*h);
        car->steering=approach(car->steering,turn<0?-turn*car->turn_left:turn*car->turn_right,car->turn_rate*h);
        if(car->grounded)car->yaw+=car->speed*tanf(car->steering)/car->wheelbase*h;
        car->pos[0]+=cosf(car->yaw)*car->speed*h;
        car->pos[1]+=sinf(car->yaw)*car->speed*h;
        support(car,&terrain,gravity,h);
        if(blocked(v,i,car,&old,&terrain)) {
            *car=old;car->speed=0;
            /* Resting still must acquire ground support on the first update. */
            support(car,&terrain,gravity,h);
        }
        if(car->circumference>0)car->wheel_spin+=car->speed*h/car->circumference*6.2831853f;
        if(memcmp(old.pos,car->pos,sizeof(old.pos)) || old.yaw!=car->yaw ||
           old.pitch!=car->pitch || old.roll!=car->roll)changed=true;
    }
    if(changed)hta_vehicles_pose(v);
}
void hta_vehicles_camera(hta_vehicles *v,const hta_collision *world,hta_player *p,
                         hta_camera *cam,float dyaw,float dpitch)
{
    if(!v || v->driver<0)return;
    const hta_vehicle *car=&v->cars[v->driver];
    v->look_yaw=clamp(v->look_yaw+dyaw,-2.6f,2.6f);
    v->look_pitch=clamp(v->look_pitch+dpitch,-0.8f,0.5f);
    cam->yaw=car->yaw+v->look_yaw;cam->pitch=v->look_pitch;
    float seat[3];place(car,car->seat,seat);memcpy(p->pos,seat,sizeof(seat));
    p->pos[2]-=p->phys.cam_stand;p->footstep=false;p->landed=false;p->on_ground=car->grounded;
    p->velocity[0]=cosf(car->yaw)*car->speed;p->velocity[1]=sinf(car->yaw)*car->speed;p->velocity[2]=car->fall_speed;
    cam->fov_y=p->phys.fov_y;
    float origin[3]={car->pos[0],car->pos[1],car->pos[2]+HTA_VEHICLE_CAMERA_UP};
    float fwd[3];hta_camera_forward(cam,fwd);
    float d[3]={-fwd[0],-fwd[1],-fwd[2]},distance=HTA_VEHICLE_CAMERA_BACK,t;
    hta_collision terrain={0};if(world){terrain=*world;terrain.extra=NULL;}
    if(hta_collision_ray(&terrain,origin,d,distance,&t,NULL,NULL))distance=fmaxf(0,t-0.15f);
    for(int k=0;k<3;k++)cam->pos[k]=origin[k]+d[k]*distance;
}
