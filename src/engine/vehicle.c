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
    hta_rd_f32(c,po+36,&v->ground_depth);
    if(!isfinite(v->ground_depth) || v->ground_depth<=0)v->ground_depth=0;
    hta_rd_f32(c,po+32,&v->ground_friction);
    if(!isfinite(v->ground_friction) || v->ground_friction<0)v->ground_friction=0;
    hta_rd_f32(c,po+8,&v->mass);
    hta_rd_f32(c,po+88,&v->yaw_inertia);
    for(int k=0;k<3;k++)hta_rd_f32(c,po+12+k*4,&v->center_of_mass[k]);
    if(!isfinite(v->mass) || v->mass<=0 || !isfinite(v->yaw_inertia) ||
       v->yaw_inertia<=0)return false;
    if(!array(c,po+116,&count,&arr) || count>HTA_VEHICLE_MASS_POINTS)return false;
    float front=-INFINITY,back=INFINITY; uint32_t wheels=0;
    for(uint32_t i=0;i<count;i++) {
        hta_vehicle_point *p=&v->points[v->point_count]; uint16_t powered=0xffff;
        hta_rd_u16(c,arr+i*128+32,&powered); p->wheel=powered!=0xffff;
        hta_rd_u16(c,arr+i*128+34,&p->node);
        for(int k=0;k<3;k++)
            if(!hta_rd_f32(c,arr+i*128+56+k*4,&p->pos[k]) || !isfinite(p->pos[k]))return false;
        hta_rd_f32(c,arr+i*128+104,&p->radius);
        if(!isfinite(p->radius) || p->radius<=0)return false;
        if(p->wheel){front=fmaxf(front,p->pos[0]);back=fminf(back,p->pos[0]);wheels++;}
        p->visual_radius=p->radius;
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
    free(v->rest);free(v->coll_rest);free(v->render_nodes);
    memset(v,0,sizeof(*v));v->driver=-1;
}
static void pose_mesh(hta_bsp_mesh *m,const hta_vertex *rest,const uint16_t *nodes,
                      const hta_vehicle *v,uint32_t first,uint32_t count)
{
    float basis[3][3];
    for (int k=0;k<3;k++) { float axis[3]={0};axis[k]=1;rotate(v,axis,basis[k]); }
    int16_t wheel_by_node[256];
    for(uint32_t j=0;j<256;j++)wheel_by_node[j]=-1;
    if(nodes)for(uint32_t j=0;j<v->point_count;j++)
        if(v->points[j].wheel && v->points[j].node<256)
            wheel_by_node[v->points[j].node]=(int16_t)j;
    float cs=cosf(v->wheel_spin),ss=sinf(v->wheel_spin);
    float ct=cosf(v->steering),st=sinf(v->steering);
    for(uint32_t i=first;i<first+count;i++) {
        m->vertices[i]=rest[i];
        float p[3]={rest[i].pos[0],rest[i].pos[1],rest[i].pos[2]};
        float n[3]={rest[i].normal[0],rest[i].normal[1],rest[i].normal[2]};
        int16_t wheel=nodes && nodes[i]<256 ? wheel_by_node[nodes[i]] : -1;
        if(wheel>=0) {
            const hta_vehicle_point *pt=&v->points[wheel];
            /* The model vertices are already in bind/model space. Rotate
             * around the phys mass point matching their model node. */
            float x=p[0]-pt->pos[0], y=p[1]-pt->pos[1], z=p[2]-pt->pos[2];
            float sx=x*cs+z*ss,sz=-x*ss+z*cs;
            float nx=n[0]*cs+n[2]*ss,nz=-n[0]*ss+n[2]*cs;
            float c=pt->pos[0]>0 ? ct : 1,s=pt->pos[0]>0 ? st : 0;
            p[0]=pt->pos[0]+sx*c-y*s;
            p[1]=pt->pos[1]+sx*s+y*c;
            p[2]=pt->pos[2]+sz+pt->travel;
            n[0]=nx*c-n[1]*s;n[1]=nx*s+n[1]*c;n[2]=nz;
        }
        for(int k=0;k<3;k++) {
            m->vertices[i].pos[k]=v->pos[k]+basis[0][k]*p[0]+
                basis[1][k]*p[1]+basis[2][k]*p[2];
            m->vertices[i].normal[k]=basis[0][k]*n[0]+
                basis[1][k]*n[1]+basis[2][k]*n[2];
        }
    }
}
void hta_vehicles_pose(hta_vehicles *v)
{
    if(!v || !v->loaded)return;
    for(uint32_t i=0;i<v->count;i++) {
        hta_vehicle *car=&v->cars[i];
        pose_mesh(&v->mesh,v->rest,v->render_nodes,car,car->first_vertex,car->vertex_count);
        pose_mesh(&v->coll_mesh,v->coll_rest,NULL,car,car->first_coll,car->coll_count);
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
    uint32_t node_cap=0;
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
        if(!hta_model_instance_nodes(&v->mesh,&v->render_nodes,&node_cap,
                                     c,bm,car.model_id,err,n) ||
           !hta_model_collision_instance(&v->coll_mesh,c,id))goto fail;
        car.vertex_count=v->mesh.vertex_count-car.first_vertex;
        car.coll_count=v->coll_mesh.vertex_count-car.first_coll;
        for(uint32_t w=0;w<car.point_count;w++) {
            hta_vehicle_point *pt=&car.points[w];if(!pt->wheel)continue;
            float low=INFINITY;
            for(uint32_t j=car.first_vertex;j<car.first_vertex+car.vertex_count;j++)
                if(v->render_nodes[j]==pt->node)
                    low=fminf(low,v->mesh.vertices[j].pos[2]);
            if(isfinite(low) && pt->pos[2]>low)
                pt->visual_radius=pt->pos[2]-low;
        }
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
        const hta_vehicle *car=&v->cars[i];
        if(hypotf(car->speed,hypotf(car->lateral_vel[0],car->lateral_vel[1]))>
           HTA_VEHICLE_EXIT_SPEED)continue;
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
    if(hypotf(car->speed,hypotf(car->lateral_vel[0],car->lateral_vel[1]))>
       HTA_VEHICLE_EXIT_SPEED || fabsf(car->yaw_rate)>.2f || !car->grounded)return false;
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
        car->speed=0;car->lateral_vel[0]=car->lateral_vel[1]=0;
        car->yaw_rate=0;v->driver=-1;return true;
    }
    return false;
}

typedef struct {
    float normal[2], point[3], other_point[3];
    int32_t other; /* -1 for fixed terrain */
} hta_vehicle_hit;
static void hit_normal(hta_vehicle_hit *hit,float x,float y)
{
    float len=hypotf(x,y);
    hit->normal[0]=len>1e-6f ? x/len : -1;
    hit->normal[1]=len>1e-6f ? y/len : 0;
}
static bool blocked(const hta_vehicles *fleet,uint32_t index,const hta_vehicle *next,
                    const hta_vehicle *old,const hta_collision *world,
                    hta_vehicle_hit *hit)
{
    float old_overlap=0,new_overlap=0,deepest=0;
    float old_static=0,new_static=0,deepest_static=0;
    bool ray_hit=false;
    hta_vehicle_hit fixed={.other=-1};
    hta_vehicle_hit pair={.other=-1};
    for(uint32_t k=0;k<next->point_count;k++) {
        const hta_vehicle_point *pt=&next->points[k];float at[3],prev[3];
        place(next,pt->pos,at);place(old,pt->pos,prev);
        float x=at[0],y=at[1];
        hta_collision_depenetrate(world,&x,&y,at[2]-pt->radius+HTA_VEHICLE_CLEARANCE,
                                  pt->radius*2,pt->radius);
        float push=hypotf(x-at[0],y-at[1]);
        if(push>HTA_VEHICLE_CLEARANCE){
            float pen=push-HTA_VEHICLE_CLEARANCE;new_static+=pen;
            if(pen>deepest_static){deepest_static=pen;
                memcpy(fixed.point,at,sizeof(at));
                hit_normal(&fixed,x-at[0],y-at[1]);}
        }
        float d[3]={at[0]-prev[0],at[1]-prev[1],at[2]-prev[2]};
        /* Wheel centres follow the ground by design. Sweeping their centres
         * into a rising floor can report a hit before support lifts the body
         * onto it, making a driveable hill act like a wall. The wheel's
         * horizontal volume above still checks actual walls. */
        float normal[3];
        if(!pt->wheel && hta_collision_ray(world,prev,d,1,NULL,NULL,normal)){
            ray_hit=true;
            if(!deepest_static){memcpy(fixed.point,at,sizeof(at));
                hit_normal(&fixed,normal[0],normal[1]);}
        }
        for(uint32_t j=0;j<fleet->count;j++) {
            if(j==index)continue;
            const hta_vehicle *other=&fleet->cars[j];
            float separation=next->body_radius+other->body_radius;
            if (hypotf(next->pos[0]-other->pos[0],next->pos[1]-other->pos[1])>separation)continue;
            for(uint32_t q=0;q<other->point_count;q++) {
                float b[3];place(other,other->points[q].pos,b);
                float dx=at[0]-b[0],dy=at[1]-b[1],dz=at[2]-b[2];
                float r=pt->radius+other->points[q].radius;
                float next_d2=dx*dx+dy*dy+dz*dz;
                if(next_d2<r*r){
                    float pen=r*r-next_d2;new_overlap+=pen;
                    if(pen>deepest){deepest=pen;pair.other=(int32_t)j;
                        memcpy(pair.point,at,sizeof(at));
                        memcpy(pair.other_point,b,sizeof(b));
                        hit_normal(&pair,dx,dy);}
                }
                dx=prev[0]-b[0];dy=prev[1]-b[1];dz=prev[2]-b[2];
                float old_d2=dx*dx+dy*dy+dz*dz;
                if(old_d2<r*r)old_overlap+=r*r-old_d2;
            }
        }
    }
    /* A hard contact may leave two proxy spheres overlapping. Permit a move
     * that decreases total penetration, so reverse can release the car. */
    /* Most steps never touch a structure. Only score the old pose when a
     * candidate has contact; it permits motion out of an existing wedge
     * without doubling every ordinary collision query. */
    if(new_static>0 || ray_hit)for(uint32_t k=0;k<old->point_count;k++){
        const hta_vehicle_point *pt=&old->points[k];float prev[3];
        place(old,pt->pos,prev);
        float ox=prev[0],oy=prev[1];
        hta_collision_depenetrate(world,&ox,&oy,prev[2]-pt->radius+HTA_VEHICLE_CLEARANCE,
                                  pt->radius*2,pt->radius);
        float old_push=hypotf(ox-prev[0],oy-prev[1]);
        if(old_push>HTA_VEHICLE_CLEARANCE)
            old_static+=old_push-HTA_VEHICLE_CLEARANCE;
    }
    bool separating_static=old_static>0 && new_static<old_static-1e-5f;
    if((new_static>0 && !separating_static) || (ray_hit && !separating_static)){
        if(hit)*hit=fixed;
        return true;
    }
    bool stop=new_overlap>0 && (old_overlap==0 || new_overlap>=old_overlap-1e-5f);
    if(stop && hit)*hit=pair;
    return stop;
}
static void planar_velocity(const hta_vehicle *car,float out[2])
{
    out[0]=cosf(car->yaw)*car->speed+car->lateral_vel[0];
    out[1]=sinf(car->yaw)*car->speed+car->lateral_vel[1];
}
static void set_planar_velocity(hta_vehicle *car,const float v[2])
{
    float fwd[2]={cosf(car->yaw),sinf(car->yaw)};
    car->speed=v[0]*fwd[0]+v[1]*fwd[1];
    car->lateral_vel[0]=v[0]-car->speed*fwd[0];
    car->lateral_vel[1]=v[1]-car->speed*fwd[1];
}
static float contact_arm(const hta_vehicle *car,const float point[3],const float normal[2])
{
    float center[3];place(car,car->center_of_mass,center);
    return (point[0]-center[0])*normal[1]-(point[1]-center[1])*normal[0];
}
/* A 2-D rigid-body normal impulse. The mass, centre of mass and yaw moment
 * are from phys; only restitution is our own choice. The perpendicular
 * velocity remains as lateral slip and gradually loses energy to the tires. */
static void impact(hta_vehicles *fleet,uint32_t index,const hta_vehicle_hit *hit)
{
    hta_vehicle *car=&fleet->cars[index];
    hta_vehicle *other=hit->other>=0 ? &fleet->cars[hit->other] : NULL;
    if(car->mass<=0 || car->yaw_inertia<=0)return;
    float a[2],b[2]={0,0};planar_velocity(car,a);
    if(other)planar_velocity(other,b);
    float arm=contact_arm(car,hit->point,hit->normal);
    float other_arm=other ? contact_arm(other,hit->other_point,hit->normal) : 0;
    float relative=(a[0]-b[0])*hit->normal[0]+(a[1]-b[1])*hit->normal[1]
        +car->yaw_rate*arm-(other ? other->yaw_rate*other_arm : 0);
    if(relative>=0)return;
    float inverse=1/car->mass+arm*arm/car->yaw_inertia;
    if(other && other->mass>0 && other->yaw_inertia>0)
        inverse+=1/other->mass+other_arm*other_arm/other->yaw_inertia;
    if(inverse<=0)return;
    float impulse=-(1+HTA_VEHICLE_RESTITUTION)*relative/inverse;
    a[0]+=impulse*hit->normal[0]/car->mass;
    a[1]+=impulse*hit->normal[1]/car->mass;
    car->yaw_rate=clamp(car->yaw_rate+impulse*arm/car->yaw_inertia,
                        -car->turn_rate,car->turn_rate);
    set_planar_velocity(car,a);
    if(other && other->mass>0 && other->yaw_inertia>0){
        b[0]-=impulse*hit->normal[0]/other->mass;
        b[1]-=impulse*hit->normal[1]/other->mass;
        other->yaw_rate=clamp(other->yaw_rate-impulse*other_arm/other->yaw_inertia,
                              -other->turn_rate,other->turn_rate);
        set_planar_velocity(other,b);
        other->rest_time=0;
    }
}
static void support(hta_vehicle *v,const hta_collision *world,float gravity,float dt)
{
    bool was_grounded = v->grounded;
    float old_z=v->pos[2];
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
    /* Carry the speed gained while climbing over a crest into free flight.
     * On a rising surface, the ground itself sets height; on level or falling
     * ground, only gravity can pull the chassis down. */
    if(was_grounded)
        v->fall_speed=(isfinite(target) && target>old_z+1e-4f) ? 0 : v->rise_speed;
    v->fall_speed-=gravity*v->gravity_scale*dt;
    v->pos[2]+=v->fall_speed*dt;v->grounded=false;
    /* The physics tag's ground depth is the short suspension reach. It keeps
     * a slow jeep planted while its wheel contacts alternate on rough ground.
     * At road speed the chassis follows its ballistic path over a crest. */
    float slow=fabsf(v->speed)<v->forward*HTA_VEHICLE_ADHESION_SPEED_FRACTION
        ? v->ground_depth : 0;
    if(isfinite(target) && (v->pos[2]<=target ||
       (was_grounded && v->pos[2]-target<=slow)) && target-v->pos[2]<0.5f) {
        v->pos[2]=target;v->fall_speed=0;v->grounded=true;
        v->rise_speed=(was_grounded && fabsf(v->speed)>HTA_VEHICLE_EXIT_SPEED && target>old_z)
            ? fminf((target-old_z)/dt,fabsf(v->speed)*tanf(HTA_VEHICLE_MAX_SLOPE)) : 0;
        if(nf && nb && nl && nr && width>0.1f) {
            float pitch=-atan2f(front/nf-back/nb,v->wheelbase);
            float roll=atan2f(left/nl-right/nr,width);
            v->pitch=approach(v->pitch,clamp(pitch,-HTA_VEHICLE_MAX_SLOPE,HTA_VEHICLE_MAX_SLOPE),dt*2);
            v->roll=approach(v->roll,clamp(roll,-HTA_VEHICLE_MAX_SLOPE,HTA_VEHICLE_MAX_SLOPE),dt*2);
        }
    } else {
        v->rise_speed=0;
    }
    unsigned touching=0;
    for(uint32_t k=0;k<v->point_count;k++) {
        hta_vehicle_point *pt=&v->points[k];if(!pt->wheel)continue;
        float wanted=0,at[3],z;
        place(v,pt->pos,at);
        if(hta_collision_ground(world,at[0],at[1],at[2]+pt->radius,&z)) {
            /* The art is smaller than the collision wheel. Allow that
             * difference in addition to the tag's ground depth, so even
             * the lowest wheel can reach terrain under a tilted chassis. */
            float reach=v->ground_depth+fabsf(pt->radius-pt->visual_radius);
            float gap=at[2]-pt->radius-z;
            if(gap<=v->ground_depth && gap>=-v->ground_depth) {
                touching++;
                wanted=clamp(z+pt->visual_radius-at[2],-reach,reach);
            }
        }
        pt->travel=wanted;
    }
    v->traction=touching>=2;
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
        if (!driven && car->grounded && car->speed == 0.0f &&
            car->lateral_vel[0]==0 && car->lateral_vel[1]==0 &&
            car->yaw_rate==0 && car->rest_time >= HTA_VEHICLE_SETTLE_TIME) continue;
        float gas=driven?clamp(throttle,-1,1):0;
        float turn=driven?clamp(steer,-1,1):0;
        float target=gas>=0?gas*car->forward:gas*car->reverse;
        float rate=car->accel;
        if(!driven || brake || fabsf(gas)<0.01f || gas*car->speed<0){rate=car->decel;target=0;}
        if(car->grounded || car->traction)car->speed=approach(car->speed,target,rate*h);
        float lateral=hypotf(car->lateral_vel[0],car->lateral_vel[1]);
        if(lateral>0 && (car->grounded || car->traction)){
            float left=fmaxf(0,lateral-car->decel*car->ground_friction*h);
            car->lateral_vel[0]*=left/lateral;car->lateral_vel[1]*=left/lateral;
        }
        car->yaw_rate*=expf(-HTA_VEHICLE_YAW_DAMP*h);
        if(fabsf(car->yaw_rate)<.001f)car->yaw_rate=0;
        car->steering=approach(car->steering,turn<0?-turn*car->turn_left:turn*car->turn_right,car->turn_rate*h);
        if(car->grounded || car->traction)
            car->yaw+=car->speed*tanf(car->steering)/car->wheelbase*h;
        car->yaw+=car->yaw_rate*h;
        car->pos[0]+=(cosf(car->yaw)*car->speed+car->lateral_vel[0])*h;
        car->pos[1]+=(sinf(car->yaw)*car->speed+car->lateral_vel[1])*h;
        support(car,&terrain,gravity,h);
        hta_vehicle_hit hit={.other=-1};
        if(blocked(v,i,car,&old,&terrain,&hit)) {
            hta_vehicle attempted=*car;
            *car=old;
            car->steering=attempted.steering;
            car->speed=attempted.speed;
            memcpy(car->lateral_vel,attempted.lateral_vel,sizeof(car->lateral_vel));
            car->yaw_rate=attempted.yaw_rate;
            /* The blocked horizontal move still advances suspension/gravity. */
            support(car,&terrain,gravity,h);
            impact(v,i,&hit);
            /* Engine force at a turned front axle produces torque even when
             * translation is blocked. This lets the tires work the jeep out
             * of a shallow wedge, subject to the same collision check next
             * step. Mass/inertia and drive acceleration come from the tags. */
            if(driven && (car->grounded || car->traction) && !brake &&
               fabsf(gas)>.01f && fabsf(car->steering)>.01f){
                float alpha=car->mass*car->accel*gas*sinf(car->steering)*
                            (car->wheelbase*.5f)/car->yaw_inertia;
                car->yaw_rate=clamp(car->yaw_rate+alpha*h,-car->turn_rate,car->turn_rate);
            }
            /* Rotate around the contact point when drive/impact torque can
             * improve the fit. A centre pivot often jams both ends of a
             * long jeep against the walls of a narrow passage. */
            if(fabsf(car->yaw_rate)>.001f){
                hta_vehicle pivot=*car;
                float angle=clamp(car->yaw_rate*h,-.03f,.03f);
                float dx=car->pos[0]-hit.point[0],dy=car->pos[1]-hit.point[1];
                float c=cosf(angle),s=sinf(angle);
                pivot.pos[0]=hit.point[0]+dx*c-dy*s;
                pivot.pos[1]=hit.point[1]+dx*s+dy*c;
                pivot.yaw+=angle;
                if(!blocked(v,i,&pivot,car,&terrain,NULL))*car=pivot;
            }
            changed=true;
        }
        car->rest_time=(!driven && car->grounded && car->speed==0 &&
                        car->lateral_vel[0]==0 && car->lateral_vel[1]==0 &&
                        car->yaw_rate==0)
            ? fminf(HTA_VEHICLE_SETTLE_TIME,car->rest_time+h) : 0;
        if(car->circumference>0){
            float wheel_target=driven && !brake && fabsf(gas)>.01f
                ? (gas>=0?gas*car->forward:gas*car->reverse) : car->speed;
            float wheel_rate=driven && !brake && fabsf(gas)>.01f &&
                             car->wheel_speed*wheel_target>=0 ? car->accel : car->decel;
            car->wheel_speed=approach(car->wheel_speed,wheel_target,wheel_rate*h);
            car->wheel_spin+=car->wheel_speed*h/car->circumference*6.2831853f;
            if(fabsf(car->wheel_spin)>6.2831853f)
                car->wheel_spin=remainderf(car->wheel_spin,6.2831853f);
        }
        if(memcmp(old.pos,car->pos,sizeof(old.pos)) || old.yaw!=car->yaw ||
           old.pitch!=car->pitch || old.roll!=car->roll ||
           old.steering!=car->steering || old.wheel_spin!=car->wheel_spin)changed=true;
        for(uint32_t k=0;k<car->point_count;k++)
            if(old.points[k].travel!=car->points[k].travel)changed=true;
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
    p->velocity[0]=cosf(car->yaw)*car->speed+car->lateral_vel[0];
    p->velocity[1]=sinf(car->yaw)*car->speed+car->lateral_vel[1];
    p->velocity[2]=car->fall_speed;
    cam->fov_y=p->phys.fov_y;
    float origin[3]={car->pos[0],car->pos[1],car->pos[2]+HTA_VEHICLE_CAMERA_UP};
    float fwd[3];hta_camera_forward(cam,fwd);
    float d[3]={-fwd[0],-fwd[1],-fwd[2]},distance=HTA_VEHICLE_CAMERA_BACK,t;
    hta_collision terrain={0};if(world){terrain=*world;terrain.extra=NULL;}
    if(hta_collision_ray(&terrain,origin,d,distance,&t,NULL,NULL))distance=fmaxf(0,t-0.15f);
    for(int k=0;k<3;k++)cam->pos[k]=origin[k]+d[k]*distance;
}
