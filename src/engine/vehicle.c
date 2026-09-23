#include "vehicle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAD 0.01745329252f
#define PI_F 3.14159265f

/* GBXModel markers sit immediately before the node list. ModelMarker is 64
 * bytes, its instances at +52; ModelMarkerInstance is 32: region, then
 * permutation, then node (u8 each), translation at +4, rotation at +16. */
#define MOD2_MARKERS      0x0ACu
#define MARKER_SIZE       64u
#define MARKER_INSTANCES  52u
#define MARKER_INST_NODE  2u
#define MARKER_INST_T     4u
#define MARKER_INST_Q     16u
/* UnitSeat, 284 bytes (reconciled with Invader). */
#define SEAT_SIZE         284u
#define SEAT_LABEL        4u
#define SEAT_MARKER       36u
#define SEAT_YAW_RATE     124u
#define SEAT_PITCH_RATE   128u
#define SEAT_CAMERA       132u
#define SEAT_PITCH_RANGE  200u
#define SEAT_HUD_TEXT     236u
#define SEAT_YAW_MIN      240u
#define SEAT_YAW_MAX      244u
/* Unit / Vehicle. */
#define UNIT_RIDER_DAMAGE 388u
#define UNIT_WEAPONS      728u
#define UNIT_SEATS        740u
#define VEHI_TYPE         756u
#define VEHI_SPEEDS       760u
#define VEHI_FIXED_PITCH  868u
#define OBJ_ANIM          56u
#define OBJ_PHYSICS       140u

static float clamp(float x, float a, float b) { return fmaxf(a, fminf(b, x)); }
static float approach(float x, float to, float step)
{ return x < to ? fminf(to, x+step) : fmaxf(to, x-step); }
float hta_angle_wrap(float a)
{
    if (!isfinite(a)) return 0.0f;
    a = fmodf(a + PI_F, 2.0f * PI_F);
    if (a < 0.0f) a += 2.0f * PI_F;
    return a - PI_F;
}
static float approach_angle(float x, float to, float step)
{
    float d = hta_angle_wrap(to - x);
    if (fabsf(d) <= step) return to;
    return hta_angle_wrap(x + (d > 0 ? step : -step));
}
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
    float roll=v->roll+v->bank;
    float cr=cosf(roll), sr=sinf(roll);
    float y=p[1]*cr-p[2]*sr, z=p[1]*sr+p[2]*cr;
    float x=p[0]*cp+z*sp;
    out[0]=x*cy-y*sy; out[1]=x*sy+y*cy; out[2]=-p[0]*sp+z*cp;
}
static void place(const hta_vehicle *v, const float p[3], float out[3])
{ rotate(v,p,out); for(int k=0;k<3;k++)out[k]+=v->pos[k]; }
static void axis_quat(float q[4], float x, float y, float z, float angle)
{
    float s = sinf(angle * 0.5f);
    q[0] = x * s; q[1] = y * s; q[2] = z * s; q[3] = cosf(angle * 0.5f);
}
static void car_quat(const hta_vehicle *v, float q[4])
{
    /* Rz(yaw) . Ry(pitch) . Rx(roll), as rotate() does. */
    hta_transform a, b, c, ab, abc;
    hta_xf_identity(&a); hta_xf_identity(&b); hta_xf_identity(&c);
    axis_quat(a.q, 0, 0, 1, v->yaw);
    axis_quat(b.q, 0, 1, 0, v->pitch + v->sway[0]);
    axis_quat(c.q, 1, 0, 0, v->roll + v->bank + v->sway[1]);
    hta_xf_mul(&ab, &a, &b);
    hta_xf_mul(&abc, &ab, &c);
    memcpy(q, abc.q, sizeof(abc.q));
}
static bool has_type(const hta_vehicles *v, const hta_vehicle *car)
{ return v && car->type < v->type_count && v->types[car->type].loaded; }

/* ------------------------------------------------------------ car physics */

bool hta_vehicle_read(hta_vehicle *v, const hta_cache *c, uint32_t tag)
{
    uint32_t off; uint16_t kind=0;
    if (!v || !tag_offset(c,tag,HTA_TAG_VEHI,&off) || !hta_rd_u16(c,off+VEHI_TYPE,&kind))
        return false;
    if (kind!=HTA_VK_TANK && kind!=HTA_VK_JEEP && kind!=HTA_VK_SCOUT &&
        kind!=HTA_VK_FIGHTER && kind!=HTA_VK_TURRET) return false;
    memset(v,0,sizeof(*v)); v->tag_id=tag; v->kind=kind;
    for(uint32_t s=0;s<HTA_VEHICLE_SEATS;s++)v->occupant[s]=-1;
    hta_rd_u32(c,off+52,&v->model_id);
    /* Vehicle reconciles at 1008, Unit at 752. Speed is wu/tick;
     * acceleration is change in that speed per tick, hence 30 squared. */
    float *values[]={&v->forward,&v->reverse,&v->accel,&v->decel,
                    &v->turn_left,&v->turn_right,&v->circumference,&v->turn_rate};
    for(uint32_t i=0;i<8;i++)
        if(!hta_rd_f32(c,off+VEHI_SPEEDS+i*4,values[i]) || !isfinite(*values[i]))return false;
    v->forward*=HTA_TICK_HZ; v->reverse*=HTA_TICK_HZ;
    v->accel*=HTA_TICK_HZ*HTA_TICK_HZ; v->decel*=HTA_TICK_HZ*HTA_TICK_HZ;
    /* The driver's seat: where the driver sits, and -- for everything but
     * the jeep, whose steering is its own -- how fast the hull may turn. */
    uint32_t count,arr; bool driver=false; float seat_yaw_rate=0;
    if(!array(c,off+UNIT_SEATS,&count,&arr) || count>64)return false;
    for(uint32_t i=0;i<count;i++) {
        uint32_t flags=0; hta_rd_u32(c,arr+i*SEAT_SIZE,&flags);
        if(!(flags&HTA_SEAT_DRIVER) || (flags&HTA_SEAT_LOCKED))continue;
        char marker[33]={0}; hta_rd_bytes(c,arr+i*SEAT_SIZE+SEAT_MARKER,marker,32);
        driver=hta_model_marker_position(c,v->model_id,marker,v->seat);
        hta_rd_f32(c,arr+i*SEAT_SIZE+SEAT_YAW_RATE,&seat_yaw_rate);
        if(driver)break;
    }
    if(kind==HTA_VK_JEEP) {
        v->turn_left*=RAD; v->turn_right*=RAD; v->turn_rate*=RAD;
        if(!(v->forward>0 && v->reverse>0 && v->accel>0 && v->decel>0 && v->turn_rate>0))
            return false;
    } else {
        /* A tank, a Ghost and a Banshee carry no steering of their own: the
         * hull turns at the driver's seat yaw rate, degrees per second. */
        v->turn_left=v->turn_right=0;
        v->turn_rate=(isfinite(seat_yaw_rate) && seat_yaw_rate>0 ? seat_yaw_rate : 90.0f)*RAD;
        if(kind==HTA_VK_SCOUT) {
            if(!(v->decel>0))v->decel=v->accel;
            if(!(v->reverse>0))v->reverse=v->forward*HTA_SCOUT_REVERSE_FRACTION;
        }
        if(kind!=HTA_VK_TURRET && !(v->forward>0 && v->accel>0))return false;
        if(!(v->decel>0))v->decel=v->accel;
        if(!(v->reverse>0))v->reverse=v->forward*0.5f;
    }
    if(!driver && kind!=HTA_VK_TURRET)return false;
    uint32_t pid=0,po; hta_rd_u32(c,off+OBJ_PHYSICS,&pid);
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
    if(!array(c,po+116,&count,&arr))return false;
    if(count>HTA_VEHICLE_MASS_POINTS)count=HTA_VEHICLE_MASS_POINTS;
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
    v->wheelbase=wheels ? front-back : 0;
    if(kind==HTA_VK_JEEP)return wheels>=4 && v->wheelbase>0.1f;
    if(kind==HTA_VK_TANK)return wheels>=4;
    return v->point_count>0;
}

/* ------------------------------------------------------------ type loading */

typedef struct {
    int16_t node;
    float t[3], q[4];
} marker_ref;
static bool find_marker(const hta_cache *c, uint32_t model, const char *name, marker_ref *out)
{
    uint32_t base, mc, mo;
    if(!name[0] || !tag_offset(c,model,HTA_TAG_MOD2,&base) ||
       !array(c,base+MOD2_MARKERS,&mc,&mo))return false;
    for(uint32_t i=0;i<mc;i++){
        char nm[33]={0}; hta_rd_bytes(c,mo+i*MARKER_SIZE,nm,32);
        if(strcmp(nm,name))continue;
        uint32_t ic,io; uint8_t node=0;
        if(!array(c,mo+i*MARKER_SIZE+MARKER_INSTANCES,&ic,&io))return false;
        hta_rd_u8(c,io+MARKER_INST_NODE,&node);
        out->node=(int16_t)node;
        for(int k=0;k<3;k++)hta_rd_f32(c,io+MARKER_INST_T+k*4,&out->t[k]);
        for(int k=0;k<4;k++)hta_rd_f32(c,io+MARKER_INST_Q+k*4,&out->q[k]);
        /* The same sense the skeleton reads its node rotations in. */
        out->q[0]=-out->q[0];out->q[1]=-out->q[1];out->q[2]=-out->q[2];
        float n=sqrtf(out->q[0]*out->q[0]+out->q[1]*out->q[1]+out->q[2]*out->q[2]+out->q[3]*out->q[3]);
        if(!(n>1e-6f)){out->q[0]=out->q[1]=out->q[2]=0;out->q[3]=1;}
        else for(int k=0;k<4;k++)out->q[k]/=n;
        return true;
    }
    return false;
}
static int16_t find_node(const hta_cache *c, uint32_t model, const char *name)
{
    uint32_t base,n,arr;
    if(!tag_offset(c,model,HTA_TAG_MOD2,&base) || !array(c,base+HTA_MOD2_NODES,&n,&arr))return -1;
    for(uint32_t i=0;i<n && i<HTA_VEHICLE_NODES;i++){
        char nm[33]={0}; hta_rd_bytes(c,arr+i*HTA_NODE_SIZE,nm,32);
        if(!strcmp(nm,name))return (int16_t)i;
    }
    return -1;
}
static void load_nodes(hta_vehicle_type *t, const hta_cache *c)
{
    uint32_t base,n,arr;
    t->node_count=0;
    if(!tag_offset(c,t->model_id,HTA_TAG_MOD2,&base) || !array(c,base+HTA_MOD2_NODES,&n,&arr))return;
    if(n>HTA_VEHICLE_NODES)n=HTA_VEHICLE_NODES;
    for(uint32_t i=0;i<n;i++){
        uint32_t no=arr+i*HTA_NODE_SIZE; uint16_t par=0xffff;
        hta_rd_u16(c,no+HTA_NODE_PARENT,&par);
        int16_t p=(int16_t)par;
        t->node_parent[i]=(p<0 || (uint32_t)p>=i) ? -1 : p;
        hta_transform local; hta_xf_identity(&local);
        for(int k=0;k<3;k++)hta_rd_f32(c,no+HTA_NODE_DEF_T+k*4,&local.t[k]);
        for(int k=0;k<4;k++)hta_rd_f32(c,no+HTA_NODE_DEF_Q+k*4,&local.q[k]);
        local.q[0]=-local.q[0];local.q[1]=-local.q[1];local.q[2]=-local.q[2];
        if(!isfinite(local.q[3]) || !isfinite(local.t[0]))hta_xf_identity(&local);
        if(t->node_parent[i]<0)t->node_rest[i]=local;
        else hta_xf_mul(&t->node_rest[i],&t->node_rest[t->node_parent[i]],&local);
        memcpy(t->node_pivot[i],t->node_rest[i].t,sizeof(t->node_pivot[i]));
    }
    t->node_count=n;
}
static void marker_model_pos(const hta_vehicle_type *t, const marker_ref *m, float out[3])
{
    if(m->node>=0 && (uint32_t)m->node<t->node_count)hta_xf_point(out,&t->node_rest[m->node],m->t);
    else memcpy(out,m->t,sizeof(m->t));
}

/* Regroup the model's submeshes by the node that moves them: all the
 * hull's triangles in one run, each wheel's in another, the turret's in a
 * third. The vertices stay as they are, in model space. */
static bool split_parts(hta_vehicle_type *t, hta_bsp_mesh *src, const uint16_t *nodes,
                        const bool movable[HTA_VEHICLE_NODES])
{
    int16_t part_of[HTA_VEHICLE_NODES];
    for(uint32_t n=0;n<HTA_VEHICLE_NODES;n++){
        if(n<t->node_count && movable[n])part_of[n]=(int16_t)n;
        else if(n<t->node_count && t->node_parent[n]>=0)part_of[n]=part_of[t->node_parent[n]];
        else part_of[n]=-1;   /* the hull */
    }
    uint32_t *idx=malloc((size_t)src->index_count*sizeof(uint32_t));
    hta_submesh *subs=malloc((size_t)(src->submesh_count*(HTA_VEHICLE_NODES+1)+1)*sizeof(hta_submesh));
    if(!idx || !subs){free(idx);free(subs);return false;}
    uint32_t ni=0,ns=0;
    int16_t order[HTA_VEHICLE_NODES+1]; uint32_t np=0;
    order[np++]=-1;
    for(uint32_t n=0;n<t->node_count;n++)if(part_of[n]==(int16_t)n)order[np++]=(int16_t)n;
    t->part_count=0;
    for(uint32_t p=0;p<np;p++){
        uint32_t first=ns;
        for(uint32_t s=0;s<src->submesh_count;s++){
            const hta_submesh *sm=&src->submeshes[s];
            uint32_t start=ni;
            for(uint32_t i=sm->first_index;i+2<sm->first_index+sm->index_count && i+2<src->index_count;i+=3){
                uint32_t a=src->indices[i];
                uint16_t node=a<src->vertex_count && nodes ? nodes[a] : 0;
                int16_t part=node<HTA_VEHICLE_NODES ? part_of[node] : -1;
                if(part!=order[p])continue;
                idx[ni++]=src->indices[i];idx[ni++]=src->indices[i+1];idx[ni++]=src->indices[i+2];
            }
            if(ni==start)continue;
            subs[ns]=*sm;subs[ns].first_index=start;subs[ns].index_count=ni-start;ns++;
        }
        if(ns==first)continue;
        t->part_node[t->part_count]=order[p];
        t->part_first[t->part_count]=first;
        t->part_submeshes[t->part_count]=ns-first;
        t->part_count++;
    }
    t->mesh=*src;
    t->mesh.indices=idx;t->mesh.index_count=ni;
    t->mesh.submeshes=subs;t->mesh.submesh_count=ns;
    free(src->indices);free(src->submeshes);
    memset(src,0,sizeof(*src));
    return t->part_count>0;
}

static bool load_type(hta_vehicle_type *t, const hta_cache *c, const hta_resource_map *bm,
                      uint32_t tag, const hta_vehicle *proto, char *err, size_t n)
{
    uint32_t off;
    memset(t,0,sizeof(*t));
    if(!tag_offset(c,tag,HTA_TAG_VEHI,&off))return false;
    t->tag_id=tag;t->model_id=proto->model_id;t->kind=proto->kind;
    t->yaw_node=t->pitch_node=t->barrel_node=-1;
    t->trigger_node[0]=t->trigger_node[1]=-1;
    int32_t ti=hta_cache_find_tag_by_id(c,tag);hta_tag_entry te;char path[256]={0};
    if(ti>=0 && hta_cache_tag(c,(uint32_t)ti,&te))hta_cache_tag_path(c,&te,path,sizeof(path));
    const char *slash=strrchr(path,'\\');
    {const char *nm=slash?slash+1:path;size_t len=strlen(nm);if(len>=sizeof(t->name))len=sizeof(t->name)-1;memcpy(t->name,nm,len);t->name[len]=0;}
    hta_rd_f32(c,off+UNIT_RIDER_DAMAGE,&t->rider_damage);
    if(!isfinite(t->rider_damage) || t->rider_damage<0)t->rider_damage=0;
    { uint16_t hn=0xffff; hta_rd_u16(c,off+316,&hn); t->hud_name=(int16_t)hn; }
    hta_rd_f32(c,off+VEHI_FIXED_PITCH,&t->fixed_gun_pitch);
    if(!isfinite(t->fixed_gun_pitch))t->fixed_gun_pitch=0;
    hta_rd_u32(c,off+OBJ_ANIM+12,&t->anim_id);
    uint32_t wc,wa;
    if(array(c,off+UNIT_WEAPONS,&wc,&wa))hta_rd_u32(c,wa+12,&t->weapon_tag);
    if(t->weapon_tag==0xFFFFFFFFu)t->weapon_tag=0;
    load_nodes(t,c);

    /* Seats: the tag's own, with their markers resolved against the model. */
    uint32_t sc,sa;
    if(array(c,off+UNIT_SEATS,&sc,&sa))
        for(uint32_t i=0;i<sc && t->seat_count<HTA_VEHICLE_SEATS;i++){
            uint32_t q=sa+i*SEAT_SIZE; hta_vehicle_seat *s=&t->seats[t->seat_count];
            memset(s,0,sizeof(*s));
            hta_rd_u32(c,q,&s->flags);
            hta_rd_bytes(c,q+SEAT_LABEL,s->label,31);
            char marker[33]={0},camera[33]={0},enter[48];
            hta_rd_bytes(c,q+SEAT_MARKER,marker,32);
            hta_rd_bytes(c,q+SEAT_CAMERA,camera,32);
            marker_ref m;
            if(!find_marker(c,t->model_id,marker,&m))continue;
            s->node=m.node;memcpy(s->pos,m.t,sizeof(m.t));memcpy(s->rot,m.q,sizeof(m.q));
            snprintf(enter,sizeof(enter),"%s enter",marker);
            marker_ref e;
            if(find_marker(c,t->model_id,enter,&e))marker_model_pos(t,&e,s->enter);
            else {
                marker_model_pos(t,&m,s->enter);
                /* No door marker (the Ghost): step off to the seat's side. */
                s->enter[1]+=s->enter[1]>=0 ? 0.5f : -0.5f;
                s->enter[2]=0;
            }
            s->camera_node=-1;
            if(camera[0] && find_marker(c,t->model_id,camera,&e)){
                s->camera_node=e.node;memcpy(s->camera,e.t,sizeof(e.t));
            }
            hta_rd_f32(c,q+SEAT_YAW_RATE,&s->yaw_rate);
            hta_rd_f32(c,q+SEAT_PITCH_RATE,&s->pitch_rate);
            s->yaw_rate*=RAD;s->pitch_rate*=RAD;
            hta_rd_f32(c,q+SEAT_PITCH_RANGE,&s->pitch_min);
            hta_rd_f32(c,q+SEAT_PITCH_RANGE+4,&s->pitch_max);
            hta_rd_f32(c,q+SEAT_YAW_MIN,&s->yaw_min);
            hta_rd_f32(c,q+SEAT_YAW_MAX,&s->yaw_max);
            uint16_t hud=0xffff;hta_rd_u16(c,q+SEAT_HUD_TEXT,&hud);s->hud_text=(int16_t)hud;
            t->seat_count++;
        }

    /* The gun: which nodes aim it and where it fires from. */
    static const char *const YAW[]={"frame gun mount base","frame turret rotator"};
    static const char *const PITCH[]={"frame gun","frame turret"};
    for(int k=0;k<2 && t->yaw_node<0;k++)t->yaw_node=find_node(c,t->model_id,YAW[k]);
    for(int k=0;k<2 && t->pitch_node<0;k++)t->pitch_node=find_node(c,t->model_id,PITCH[k]);
    t->barrel_node=find_node(c,t->model_id,"frame barrels");
    static const char *const TRIG[2]={"primary trigger","secondary trigger"};
    for(int k=0;k<2;k++){
        marker_ref m;
        if(find_marker(c,t->model_id,TRIG[k],&m)){
            t->has_trigger[k]=true;t->trigger_node[k]=m.node;memcpy(t->trigger[k],m.t,sizeof(m.t));
        }
    }

    /* Parts: which nodes move on their own. */
    bool movable[HTA_VEHICLE_NODES]={0};
    for(uint32_t i=0;i<proto->point_count;i++){
        uint16_t nd=proto->points[i].node;
        if(proto->points[i].wheel && nd<t->node_count &&
           (t->kind==HTA_VK_JEEP || t->kind==HTA_VK_TANK) && nd>0)movable[nd]=true;
    }
    if(t->yaw_node>0)movable[t->yaw_node]=true;
    if(t->pitch_node>0)movable[t->pitch_node]=true;
    if(t->barrel_node>0)movable[t->barrel_node]=true;
    hta_bsp_mesh raw={0};uint16_t *nodes=NULL;uint32_t cap=0;
    raw.textures=calloc(256,sizeof(*raw.textures));
    if(!raw.textures)return false;
    if(!hta_model_instance_nodes(&raw,&nodes,&cap,c,bm,t->model_id,err,n) ||
       !split_parts(t,&raw,nodes,movable)){
        free(nodes);hta_bsp_free(&raw);hta_bsp_free(&t->mesh);return false;
    }
    free(nodes);

    /* Collision, once, in model space. */
    if(!hta_model_collision_instance(&t->coll_mesh,c,tag) || !t->coll_mesh.vertex_count){
        hta_bsp_free(&t->mesh);hta_bsp_free(&t->coll_mesh);return false;
    }
    for(int k=0;k<3;k++){t->coll_mesh.bounds_min[k]=INFINITY;t->coll_mesh.bounds_max[k]=-INFINITY;}
    for(uint32_t i=0;i<t->coll_mesh.vertex_count;i++){
        const float *p=t->coll_mesh.vertices[i].pos;
        for(int k=0;k<3;k++){
            t->coll_mesh.bounds_min[k]=fminf(t->coll_mesh.bounds_min[k],p[k]);
            t->coll_mesh.bounds_max[k]=fmaxf(t->coll_mesh.bounds_max[k],p[k]);
        }
        t->coll_radius=fmaxf(t->coll_radius,sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]));
    }
    if(!hta_collision_build(&t->coll,&t->coll_mesh)){
        hta_bsp_free(&t->mesh);hta_bsp_free(&t->coll_mesh);return false;
    }
    t->loaded=true;
    return true;
}

void hta_vehicles_free(hta_vehicles *v)
{
    if(!v)return;
    for(uint32_t i=0;i<v->type_count;i++){
        hta_collision_free(&v->types[i].coll);
        hta_bsp_free(&v->types[i].coll_mesh);
        hta_bsp_free(&v->types[i].mesh);
    }
    memset(v,0,sizeof(*v));
}

/* Measure each wheel's art: the collision sphere is bigger than the tire. */
static void wheel_art(hta_vehicle *car, const hta_vehicle_type *t)
{
    for(uint32_t w=0;w<car->point_count;w++){
        hta_vehicle_point *pt=&car->points[w];if(!pt->wheel)continue;
        float low=INFINITY;
        for(uint32_t p=0;p<t->part_count;p++){
            if(t->part_node[p]!=(int16_t)pt->node)continue;
            for(uint32_t s=t->part_first[p];s<t->part_first[p]+t->part_submeshes[p];s++){
                const hta_submesh *sm=&t->mesh.submeshes[s];
                for(uint32_t i=sm->first_index;i<sm->first_index+sm->index_count;i++)
                    low=fminf(low,t->mesh.vertices[t->mesh.indices[i]].pos[2]);
            }
        }
        if(isfinite(low) && pt->pos[2]>low)pt->visual_radius=pt->pos[2]-low;
    }
}

bool hta_vehicles_load(hta_vehicles *v,const hta_cache *c,const hta_resource_map *bm,
                        char *err,size_t n)
{
    if(!v || !c)return false;
    memset(v,0,sizeof(*v));
    uint32_t off,count,arr,pn,pa;
    if(!tag_offset(c,c->scenario_tag_id,HTA_TAG_SCNR,&off) ||
       !array(c,off+HTA_SCENARIO_VEHICLES_OFF,&count,&arr) ||
       !array(c,off+HTA_SCENARIO_VEHICLE_PAL,&pn,&pa))return false;
    int16_t type_of[64];
    for(uint32_t i=0;i<64;i++)type_of[i]=-1;
    uint32_t kinds[8]={0};
    for(uint32_t i=0;i<count && i<HTA_VEHICLE_PLACEMENTS;i++) {
        uint16_t pal; uint32_t id=0;
        if(!hta_rd_u16(c,arr+i*120,&pal) || pal>=pn || pal>=64)continue;
        hta_rd_u32(c,pa+pal*48+12,&id);
        hta_vehicle car;
        if(!hta_vehicle_read(&car,c,id))continue;
        if(type_of[pal]<0){
            if(v->type_count>=HTA_VEHICLE_TYPES)continue;
            char terr[HTA_ERRLEN]="";
            if(!load_type(&v->types[v->type_count],c,bm,id,&car,terr,sizeof(terr)))continue;
            type_of[pal]=(int16_t)v->type_count++;
        }
        if(v->count>=HTA_VEHICLE_MAX)break;
        car.type=(uint16_t)type_of[pal];car.placement=i;
        wheel_art(&car,&v->types[car.type]);
        for(int k=0;k<3;k++)hta_rd_f32(c,arr+i*120+8+k*4,&car.home_pos[k]);
        hta_rd_f32(c,arr+i*120+20,&car.home_yaw);
        hta_rd_f32(c,arr+i*120+24,&car.home_pitch);hta_rd_f32(c,arr+i*120+28,&car.home_roll);
        int8_t team=0;hta_rd_u8(c,arr+i*120+88,(uint8_t *)&team);car.team=(uint8_t)team;
        hta_rd_u16(c,arr+i*120+90,&car.spawn_flags);
        v->cars[v->count++]=car;v->skip[i]=1;
        if(car.kind<8)kinds[car.kind]++;
    }
    if(!v->count){hta_vehicles_free(v);if(err)snprintf(err,n,"no movable vehicles");return false;}
    v->loaded=true;
    hta_vehicles_roster(v,HTA_VROSTER_ALL);
    if(err)snprintf(err,n,"%u vehicles in %u types: %u jeeps, %u tanks, %u scouts, %u fighters, %u turrets",
                    v->count,v->type_count,kinds[HTA_VK_JEEP],kinds[HTA_VK_TANK],
                    kinds[HTA_VK_SCOUT],kinds[HTA_VK_FIGHTER],kinds[HTA_VK_TURRET]);
    return true;
}

void hta_vehicles_reset(hta_vehicles *v, uint32_t i)
{
    if(!v || i>=v->count)return;
    hta_vehicle *car=&v->cars[i];
    memcpy(car->pos,car->home_pos,sizeof(car->pos));
    car->yaw=car->home_yaw;car->pitch=car->home_pitch;car->roll=car->home_roll;car->bank=0;
    car->sway[0]=car->sway[1]=car->sway_vel[0]=car->sway_vel[1]=0;
    car->tumble[0]=car->tumble[1]=0;
    car->speed=car->steering=car->fall_speed=car->rise_speed=0;
    car->lateral_vel[0]=car->lateral_vel[1]=car->yaw_rate=0;
    car->wheel_speed=car->barrel_speed=0;
    car->aim_yaw=car->aim_pitch=0;car->aiming=false;
    car->rest_time=0;car->idle=0;car->grounded=false;car->traction=false;
    memset(&car->ctl,0,sizeof(car->ctl));
    for(uint32_t k=0;k<car->point_count;k++)car->points[k].travel=0;
    for(uint32_t s=0;s<HTA_VEHICLE_SEATS;s++)car->occupant[s]=-1;
}

void hta_vehicles_roster(hta_vehicles *v, int roster)
{
    if(!v || !v->loaded)return;
    for(uint32_t i=0;i<v->count;i++){
        hta_vehicle *car=&v->cars[i];
        const hta_vehicle_type *t=&v->types[car->type];
        bool jeep=car->kind==HTA_VK_JEEP;
        bool rocket=jeep && t->name[0]=='r';   /* `rwarthog` */
        /* ScenarioVehicleMultiplayerSpawnFlags: bit 0 slayer default,
         * bit 8 slayer allowed. A placement with neither set still spawns
         * for "all", as a map with no flags at all would want. */
        bool allowed=(car->spawn_flags&0x0100u) || !car->spawn_flags;
        bool on=false;
        switch(roster){
        case HTA_VROSTER_NONE: on=false; break;
        case HTA_VROSTER_DEFAULT: on=(car->spawn_flags&1u)!=0; break;
        case HTA_VROSTER_WARTHOGS: on=allowed && jeep && !rocket; break;
        case HTA_VROSTER_ROCKET_WARTHOGS: on=allowed && rocket; break;
        case HTA_VROSTER_GHOSTS: on=allowed && car->kind==HTA_VK_SCOUT; break;
        case HTA_VROSTER_SCORPIONS: on=allowed && car->kind==HTA_VK_TANK; break;
        case HTA_VROSTER_BANSHEES: on=allowed && car->kind==HTA_VK_FIGHTER; break;
        default: on=allowed; break;
        }
        car->active=on;
        hta_vehicles_reset(v,i);
    }
    hta_vehicles_sync(v);
}

/* ------------------------------------------------------------ seats */

uint32_t hta_vehicles_seat_count(const hta_vehicles *v, uint32_t car)
{
    if(!v || car>=v->count || !has_type(v,&v->cars[car]))return 0;
    return v->types[v->cars[car].type].seat_count;
}
const hta_vehicle_seat *hta_vehicles_seat(const hta_vehicles *v, uint32_t car, uint32_t seat)
{
    if(seat>=hta_vehicles_seat_count(v,car))return NULL;
    return &v->types[v->cars[car].type].seats[seat];
}
int32_t hta_vehicles_driver_seat(const hta_vehicles *v, uint32_t car)
{
    uint32_t n=hta_vehicles_seat_count(v,car);
    for(uint32_t s=0;s<n;s++)if(v->types[v->cars[car].type].seats[s].flags&HTA_SEAT_DRIVER)return (int32_t)s;
    return -1;
}
int32_t hta_vehicles_gunner_seat(const hta_vehicles *v, uint32_t car)
{
    uint32_t n=hta_vehicles_seat_count(v,car);
    for(uint32_t s=0;s<n;s++)if(v->types[v->cars[car].type].seats[s].flags&HTA_SEAT_GUNNER)return (int32_t)s;
    return -1;
}
float hta_vehicles_speed(const hta_vehicles *v, uint32_t car)
{
    if(!v || car>=v->count)return 0;
    const hta_vehicle *c=&v->cars[car];
    return sqrtf(c->speed*c->speed+c->lateral_vel[0]*c->lateral_vel[0]+
                 c->lateral_vel[1]*c->lateral_vel[1]+c->fall_speed*c->fall_speed*
                 (c->kind==HTA_VK_FIGHTER));
}
static bool seat_open(const hta_vehicles *v, uint32_t car, uint32_t seat)
{
    const hta_vehicle *c=&v->cars[car];
    const hta_vehicle_seat *s=hta_vehicles_seat(v,car,seat);
    if(!s || (s->flags&HTA_SEAT_LOCKED) || c->occupant[seat]>=0)return false;
    if(s->flags&HTA_SEAT_NEEDS_DRIVER){
        int32_t d=hta_vehicles_driver_seat(v,car);
        if(d<0 || c->occupant[d]<0)return false;
    }
    return true;
}

int32_t hta_vehicles_near(const hta_vehicles *v,const hta_collision *world,
                          const float feet[3],int32_t *out_seat)
{
    if(out_seat)*out_seat=-1;
    if(!v || !v->loaded || !feet)return -1;
    float best=HTA_VEHICLE_ENTER_REACH;int32_t result=-1;
    hta_collision terrain={0};if(world){terrain=*world;terrain.extra=NULL;terrain.instances=NULL;terrain.instance_count=0;}
    for(uint32_t i=0;i<v->count;i++) {
        const hta_vehicle *car=&v->cars[i];
        if(!car->active || !has_type(v,car))continue;
        if(hta_vehicles_speed(v,i)>HTA_VEHICLE_EXIT_SPEED)continue;
        const hta_vehicle_type *t=&v->types[car->type];
        /* Bigger vehicles have their doors further out. */
        float reach=fmaxf(HTA_VEHICLE_ENTER_REACH,car->body_radius*0.6f);
        if(hypotf(car->pos[0]-feet[0],car->pos[1]-feet[1])>car->body_radius+reach+1)continue;
        for(uint32_t s=0;s<t->seat_count;s++){
            if(!seat_open(v,i,s))continue;
            float door[3],seatw[3];place(car,t->seats[s].enter,door);
            hta_transform root;hta_vehicles_seat_transform(v,i,s,&root);
            memcpy(seatw,root.t,sizeof(seatw));
            /* Near the door, or near the seat itself. */
            float dd=hypotf(door[0]-feet[0],door[1]-feet[1]);
            float ds=hypotf(seatw[0]-feet[0],seatw[1]-feet[1]);
            float dz=fminf(fabsf(door[2]-feet[2]),fabsf(seatw[2]-feet[2]-0.2f));
            float dist=fminf(dd,ds)+fmaxf(0,dz-0.6f);
            if(dist>=best*(reach/HTA_VEHICLE_ENTER_REACH))continue;
            /* Driver's seat first when two doors are as near. */
            if(!(t->seats[s].flags&HTA_SEAT_DRIVER))dist+=0.05f;
            float eye[3]={feet[0],feet[1],feet[2]+0.35f};
            float d[3]={seatw[0]-eye[0],seatw[1]-eye[1],seatw[2]-eye[2]};
            if(world && hta_collision_ray(&terrain,eye,d,1,NULL,NULL,NULL))continue;
            best=dist*(HTA_VEHICLE_ENTER_REACH/reach);result=(int32_t)i;
            if(out_seat)*out_seat=(int32_t)s;
        }
    }
    return result;
}

bool hta_vehicles_enter(hta_vehicles *v,uint32_t car,uint32_t seat,int32_t unit)
{
    if(!v || !v->loaded || car>=v->count || !v->cars[car].active || unit<0 || unit>127)return false;
    if(!seat_open(v,car,seat))return false;
    for(uint32_t i=0;i<v->count;i++)for(uint32_t s=0;s<HTA_VEHICLE_SEATS;s++)
        if(v->cars[i].occupant[s]==unit)return false;
    hta_vehicle *c=&v->cars[car];
    c->occupant[seat]=(int8_t)unit;c->rest_time=0;c->idle=0;
    const hta_vehicle_seat *s=hta_vehicles_seat(v,car,seat);
    if(s->flags&HTA_SEAT_DRIVER){memset(&c->ctl,0,sizeof(c->ctl));c->ctl.driven=true;
        c->ctl.yaw=c->yaw;}
    return true;
}

void hta_vehicles_vacate(hta_vehicles *v, int32_t unit)
{
    if(!v || unit<0)return;
    for(uint32_t i=0;i<v->count;i++){
        hta_vehicle *c=&v->cars[i];
        for(uint32_t s=0;s<HTA_VEHICLE_SEATS;s++){
            if(c->occupant[s]!=unit)continue;
            c->occupant[s]=-1;c->rest_time=0;
            const hta_vehicle_seat *seat=hta_vehicles_seat(v,i,s);
            if(seat && (seat->flags&HTA_SEAT_DRIVER)){
                float yaw=c->ctl.yaw;memset(&c->ctl,0,sizeof(c->ctl));c->ctl.yaw=yaw;
            }
            if(seat && (seat->flags&HTA_SEAT_GUNNER))c->aiming=false;
        }
    }
}

/* Check exits against the entire current world (including this vehicle),
 * headroom and the swept path from the seat. Refuse unsafe or moving
 * exits -- except from a Banshee, which Halo lets you bail out of. */
bool hta_vehicles_exit(hta_vehicles *v,const hta_collision *world,uint32_t ci,uint32_t seat,
                       float radius,float height,float out_feet[3],float *out_yaw)
{
    if(!v || ci>=v->count || !world || !out_feet)return false;
    hta_vehicle *car=&v->cars[ci];
    const hta_vehicle_seat *s=hta_vehicles_seat(v,ci,seat);
    if(!s)return false;
    bool flying=car->kind==HTA_VK_FIGHTER && !car->grounded;
    if(!flying && (hta_vehicles_speed(v,ci)>HTA_VEHICLE_EXIT_SPEED || fabsf(car->yaw_rate)>.2f))
        return false;
    hta_transform root;hta_vehicles_seat_transform(v,ci,seat,&root);
    /* The door first, then the four sides of the hull, scaled to its size. */
    float r=car->body_radius;
    float candidates[6][3]={
        {s->enter[0],s->enter[1],s->enter[2]+0.3f},
        {0,r*0.9f+radius,0.3f},{0,-(r*0.9f+radius),0.3f},
        {-(r+radius),0,0.3f},{r+radius,0,0.3f},{0,0,-0.2f}};
    hta_collision terrain=*world;terrain.extra=NULL;terrain.instances=NULL;terrain.instance_count=0;
    for(int i=0;i<6;i++) {
        if(i==5 && !flying)break;
        float at[3],z;place(car,candidates[i],at);
        if(i==5){at[2]=car->pos[2]-0.2f;}
        if(!hta_collision_ground(world,at[0],at[1],at[2]+(flying?0:0.3f),&z))continue;
        if(!flying && fabsf(z-car->pos[2])>1.0f)continue;
        if(flying)z=fmaxf(z,fminf(at[2],car->pos[2]-0.3f));
        float x=at[0],y=at[1];
        hta_collision_depenetrate(world,&x,&y,z,height,radius);
        if(hypotf(x-at[0],y-at[1])>HTA_VEHICLE_CLEARANCE+0.02f)continue;
        float up[3]={0,0,1},origin[3]={x,y,z+HTA_VEHICLE_CLEARANCE};
        bool blocked=false;
        for(int q=0;q<5;q++) {
            origin[0]=x+(q==1?radius:q==2?-radius:0);
            origin[1]=y+(q==3?radius:q==4?-radius:0);
            /* Each ray starts on its own ground: on a slope the uphill
             * ones would otherwise begin underground and hit it. */
            float gq;
            origin[2]=z+HTA_VEHICLE_CLEARANCE;
            if(hta_collision_ground(world,origin[0],origin[1],z+0.3f,&gq) && gq>z)
                origin[2]=gq+HTA_VEHICLE_CLEARANCE;
            if(hta_collision_ray(world,origin,up,height,NULL,NULL,NULL))blocked=true;
        }
        float start[3]={root.t[0],root.t[1],root.t[2]+0.3f};
        float d[3]={x-start[0],y-start[1],z+height*0.8f-start[2]};
        if(blocked || hta_collision_ray(&terrain,start,d,1,NULL,NULL,NULL))continue;
        out_feet[0]=x;out_feet[1]=y;out_feet[2]=z;
        if(out_yaw)*out_yaw=car->yaw;
        if(!flying){
            car->speed=0;car->lateral_vel[0]=car->lateral_vel[1]=0;car->yaw_rate=0;
        }
        hta_vehicles_vacate(v,car->occupant[seat]);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------ poses */

static void pivot_xf(hta_transform *o, const float q[4], const float pivot[3], float lift)
{
    hta_transform r; hta_xf_identity(&r); memcpy(r.q,q,sizeof(r.q));
    float rp[3]; hta_xf_vector(rp,&r,pivot);
    hta_xf_identity(o); memcpy(o->q,q,sizeof(o->q));
    for(int k=0;k<3;k++)o->t[k]=pivot[k]-rp[k];
    o->t[2]+=lift;
}

void hta_vehicles_nodes(const hta_vehicles *v, uint32_t ci, hta_transform out[HTA_VEHICLE_NODES])
{
    for(uint32_t n=0;n<HTA_VEHICLE_NODES;n++)hta_xf_identity(&out[n]);
    if(!v || ci>=v->count || !has_type(v,&v->cars[ci]))return;
    const hta_vehicle *car=&v->cars[ci];
    const hta_vehicle_type *t=&v->types[car->type];
    hta_transform local[HTA_VEHICLE_NODES];
    for(uint32_t n=0;n<t->node_count;n++)hta_xf_identity(&local[n]);
    /* Wheels and treads: each rides its mass point. */
    bool spins=car->kind==HTA_VK_JEEP;
    for(uint32_t n=1;n<t->node_count;n++){
        float travel=0,centre[3]={0,0,0};unsigned cnt=0;bool front=false;
        for(uint32_t k=0;k<car->point_count;k++){
            const hta_vehicle_point *p=&car->points[k];
            if(!p->wheel || p->node!=n)continue;
            travel+=p->travel;cnt++;
            for(int j=0;j<3;j++)centre[j]+=p->pos[j];
            if(p->pos[0]>0)front=true;
        }
        if(!cnt || (car->kind!=HTA_VK_JEEP && car->kind!=HTA_VK_TANK))continue;
        travel/=cnt;for(int j=0;j<3;j++)centre[j]/=cnt;
        float q[4]={0,0,0,1};
        if(spins){
            hta_transform steer,spin,both;hta_xf_identity(&steer);hta_xf_identity(&spin);
            axis_quat(steer.q,0,0,1,front?car->steering:0);
            /* Rolling forward turns the top of the tire forward: about -Y
             * in our sense, matching the old per-vertex pose. */
            axis_quat(spin.q,0,1,0,car->wheel_spin);
            hta_xf_mul(&both,&steer,&spin);memcpy(q,both.q,sizeof(q));
        }
        pivot_xf(&local[n],q,centre,travel);
    }
    if(t->yaw_node>0 && (uint32_t)t->yaw_node<t->node_count){
        float q[4];axis_quat(q,0,0,1,car->aim_yaw);
        pivot_xf(&local[t->yaw_node],q,t->node_pivot[t->yaw_node],0);
    }
    if(t->pitch_node>0 && (uint32_t)t->pitch_node<t->node_count){
        float q[4];axis_quat(q,0,1,0,-car->aim_pitch);
        pivot_xf(&local[t->pitch_node],q,t->node_pivot[t->pitch_node],0);
    }
    if(t->barrel_node>0 && (uint32_t)t->barrel_node<t->node_count){
        float q[4];axis_quat(q,1,0,0,car->barrel_spin);
        pivot_xf(&local[t->barrel_node],q,t->node_pivot[t->barrel_node],0);
    }
    /* Compose in model space: a child moves with everything above it. */
    for(uint32_t n=0;n<t->node_count;n++){
        int16_t p=t->node_parent[n];
        if(p>=0)hta_xf_mul(&out[n],&out[p],&local[n]);
        else out[n]=local[n];
    }
}

void hta_vehicles_world(const hta_vehicles *v, uint32_t ci, hta_transform *out)
{
    hta_xf_identity(out);
    if(!v || ci>=v->count)return;
    const hta_vehicle *car=&v->cars[ci];
    car_quat(car,out->q);
    memcpy(out->t,car->pos,sizeof(out->t));
}

bool hta_vehicles_seat_transform(const hta_vehicles *v, uint32_t ci, uint32_t seat,
                                 hta_transform *out_root)
{
    const hta_vehicle_seat *s=hta_vehicles_seat(v,ci,seat);
    if(!s || !out_root)return false;
    const hta_vehicle_type *t=&v->types[v->cars[ci].type];
    hta_transform nodes[HTA_VEHICLE_NODES],world,marker,node_world,m;
    hta_vehicles_nodes(v,ci,nodes);
    hta_vehicles_world(v,ci,&world);
    hta_xf_identity(&marker);memcpy(marker.t,s->pos,sizeof(s->pos));memcpy(marker.q,s->rot,sizeof(s->rot));
    if(s->node>=0 && (uint32_t)s->node<t->node_count){
        hta_transform posed;hta_xf_mul(&posed,&nodes[s->node],&t->node_rest[s->node]);
        hta_xf_mul(&node_world,&world,&posed);
    } else node_world=world;
    hta_xf_mul(&m,&node_world,&marker);
    *out_root=m;
    return true;
}

bool hta_vehicles_trigger(const hta_vehicles *v, uint32_t ci, uint32_t trigger,
                          float out_pos[3], float out_dir[3])
{
    if(!v || ci>=v->count || !has_type(v,&v->cars[ci]))return false;
    const hta_vehicle_type *t=&v->types[v->cars[ci].type];
    uint32_t k=trigger<2 && t->has_trigger[trigger] ? trigger : 0;
    hta_transform nodes[HTA_VEHICLE_NODES],world,node_world;
    hta_vehicles_nodes(v,ci,nodes);hta_vehicles_world(v,ci,&world);
    int16_t node=t->has_trigger[k] ? t->trigger_node[k] : -1;
    if(node>=0 && (uint32_t)node<t->node_count){
        hta_transform posed;hta_xf_mul(&posed,&nodes[node],&t->node_rest[node]);
        hta_xf_mul(&node_world,&world,&posed);
    } else node_world=world;
    float zero[3]={0,0,0},fwd[3]={1,0,0};
    hta_xf_point(out_pos,&node_world,t->has_trigger[k] ? t->trigger[k] : zero);
    hta_xf_vector(out_dir,&node_world,fwd);
    if(t->fixed_gun_pitch!=0 && t->pitch_node<0){
        /* The Banshee's guns point a little down from the hull. */
        float c=cosf(t->fixed_gun_pitch),s=sinf(t->fixed_gun_pitch);
        float up[3]={0,0,1},wup[3];hta_xf_vector(wup,&world,up);
        for(int j=0;j<3;j++)out_dir[j]=out_dir[j]*c-wup[j]*s;
    }
    return true;
}

void hta_vehicles_aim(hta_vehicles *v, uint32_t ci, float yaw, float pitch, float dt)
{
    if(!v || ci>=v->count)return;
    hta_vehicle *car=&v->cars[ci];
    car->aim_world[0]=yaw;car->aim_world[1]=pitch;car->aiming=true;
    if(!has_type(v,car))return;
    const hta_vehicle_type *t=&v->types[car->type];
    if(t->yaw_node<0 && t->pitch_node<0)return;
    /* The look in the hull's own frame. */
    float d[3]={cosf(pitch)*cosf(yaw),cosf(pitch)*sinf(yaw),sinf(pitch)},l[3];
    hta_transform world,inv;hta_vehicles_world(v,ci,&world);
    hta_xf_inverse(&inv,&world);hta_xf_vector(l,&inv,d);
    float want_yaw=atan2f(l[1],l[0]),want_pitch=atan2f(l[2],hypotf(l[0],l[1]));
    int32_t gs=hta_vehicles_gunner_seat(v,ci);
    const hta_vehicle_seat *s=gs>=0 ? &t->seats[gs] : NULL;
    if(s && s->pitch_max>s->pitch_min)want_pitch=clamp(want_pitch,s->pitch_min,s->pitch_max);
    if(s && s->yaw_max>s->yaw_min)want_yaw=clamp(want_yaw,s->yaw_min,s->yaw_max);
    /* The seat's own rates, degrees a second in the tag; zero is no limit. */
    float yr=s && s->yaw_rate>0 ? s->yaw_rate*dt : 10.0f;
    float pr=s && s->pitch_rate>0 ? s->pitch_rate*dt : 10.0f;
    if(t->yaw_node>=0)car->aim_yaw=approach_angle(car->aim_yaw,want_yaw,yr);
    if(t->pitch_node>=0)car->aim_pitch=approach(car->aim_pitch,want_pitch,pr);
}

void hta_vehicles_sync(hta_vehicles *v)
{
    if(!v)return;
    for(uint32_t i=0;i<v->count && i<HTA_VEHICLE_MAX;i++){
        hta_collision_instance *in=&v->inst[i];
        const hta_vehicle *car=&v->cars[i];
        in->active=car->active && has_type(v,car);
        if(!in->active){in->grid=NULL;continue;}
        const hta_vehicle_type *t=&v->types[car->type];
        in->grid=&t->coll;
        in->radius=t->coll_radius+0.05f;
        memcpy(in->pos,car->pos,sizeof(in->pos));
        float ex[3]={1,0,0},ey[3]={0,1,0},ez[3]={0,0,1},bx[3],by[3],bz[3];
        rotate(car,ex,bx);rotate(car,ey,by);rotate(car,ez,bz);
        for(int k=0;k<3;k++){in->rot[k*3+0]=bx[k];in->rot[k*3+1]=by[k];in->rot[k*3+2]=bz[k];}
    }
}

/* ------------------------------------------------------------ collision */

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
    bool flying=next->kind==HTA_VK_FIGHTER;
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
         * horizontal volume above still checks actual walls. A flyer's
         * points all sweep: it has no wheels on the ground. */
        float normal[3];
        if((!pt->wheel || flying) && hta_collision_ray(world,prev,d,1,NULL,NULL,normal) &&
           !(flying && normal[2]>0.7f)){
            ray_hit=true;
            if(!deepest_static){memcpy(fixed.point,at,sizeof(at));
                hit_normal(&fixed,normal[0],normal[1]);}
        }
        for(uint32_t j=0;j<fleet->count;j++) {
            if(j==index)continue;
            const hta_vehicle *other=&fleet->cars[j];
            if(!other->active)continue;
            float separation=next->body_radius+other->body_radius;
            if (hypotf(next->pos[0]-other->pos[0],next->pos[1]-other->pos[1])>separation ||
                fabsf(next->pos[2]-other->pos[2])>separation)continue;
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
     * that decreases total penetration, so reverse can release the car.
     * Most steps never touch a structure. Only score the old pose when a
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
    float spin=fmaxf(car->turn_rate,1.0f);
    a[0]+=impulse*hit->normal[0]/car->mass;
    a[1]+=impulse*hit->normal[1]/car->mass;
    car->yaw_rate=clamp(car->yaw_rate+impulse*arm/car->yaw_inertia,-spin,spin);
    set_planar_velocity(car,a);
    if(other && other->mass>0 && other->yaw_inertia>0){
        float ospin=fmaxf(other->turn_rate,1.0f);
        b[0]-=impulse*hit->normal[0]/other->mass;
        b[1]-=impulse*hit->normal[1]/other->mass;
        other->yaw_rate=clamp(other->yaw_rate-impulse*other_arm/other->yaw_inertia,-ospin,ospin);
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
    if(v->kind==HTA_VK_SCOUT)slow=v->ground_depth;   /* a hover follows the ground */
    if(isfinite(target) && (v->pos[2]<=target ||
       (was_grounded && v->pos[2]-target<=slow)) && target-v->pos[2]<0.5f) {
        v->pos[2]=target;v->fall_speed=0;v->grounded=true;
        v->rise_speed=(was_grounded && fabsf(v->speed)>HTA_VEHICLE_EXIT_SPEED && target>old_z)
            ? fminf((target-old_z)/dt,fabsf(v->speed)*tanf(HTA_VEHICLE_MAX_SLOPE)) : 0;
        float base=v->wheelbase>0.1f ? v->wheelbase : 0.5f;
        if(nf && nb && nl && nr && width>0.1f) {
            float pitch=-atan2f(front/nf-back/nb,base);
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
        pt->travel=v->kind==HTA_VK_JEEP || v->kind==HTA_VK_TANK ? wanted : 0;
    }
    v->traction=touching>=2;
}

/* A flying Banshee: no gravity, and the ground only as a floor. */
static void fly_support(hta_vehicle *v,const hta_collision *world,float dt)
{
    v->pos[2]+=v->fall_speed*dt;
    float lift=0;
    for(uint32_t k=0;k<v->point_count;k++){
        const hta_vehicle_point *pt=&v->points[k];float at[3],z;
        place(v,pt->pos,at);
        if(!hta_collision_ground(world,at[0],at[1],at[2]+pt->radius,&z))continue;
        float under=z+pt->radius-at[2];
        if(under>lift)lift=under;
    }
    v->grounded=false;
    if(lift>0){
        v->pos[2]+=fminf(lift,0.5f);
        if(v->fall_speed<0)v->fall_speed=0;
        v->grounded=true;
    }
    v->traction=v->grounded;
    for(uint32_t k=0;k<v->point_count;k++)v->points[k].travel=0;
}

/* ------------------------------------------------------------ driving */

static void drive_jeep(hta_vehicle *car, float h)
{
    bool driven=car->ctl.driven;
    float gas=driven?clamp(car->ctl.throttle,-1,1):0;
    float turn=driven?clamp(car->ctl.strafe,-1,1):0;
    bool brake=car->ctl.brake;
    float target=gas>=0?gas*car->forward:gas*car->reverse;
    float rate=car->accel;
    if(brake || gas*car->speed<0){rate=car->decel;target=0;}
    else if(!driven || fabsf(gas)<0.01f){rate=car->decel*HTA_VEHICLE_COAST_JEEP;target=0;}
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
}

/* Treads: throttle drives, the stick pivots the hull at the driver's seat
 * yaw rate -- in place if need be. */
static void drive_tank(hta_vehicle *car, float h)
{
    bool driven=car->ctl.driven;
    float gas=driven?clamp(car->ctl.throttle,-1,1):0;
    float turn=driven?clamp(car->ctl.strafe,-1,1):0;
    float target=gas>=0?gas*car->forward:gas*car->reverse;
    float rate=car->accel;
    if(car->ctl.brake || gas*car->speed<0){rate=car->decel;target=0;}
    else if(!driven || fabsf(gas)<0.01f){rate=car->decel*HTA_VEHICLE_COAST_TANK;target=0;}
    if(car->grounded || car->traction)car->speed=approach(car->speed,target,rate*h);
    float lateral=hypotf(car->lateral_vel[0],car->lateral_vel[1]);
    if(lateral>0 && (car->grounded || car->traction)){
        float left=fmaxf(0,lateral-car->decel*fmaxf(car->ground_friction,.2f)*h*4);
        car->lateral_vel[0]*=left/lateral;car->lateral_vel[1]*=left/lateral;
    }
    car->yaw_rate*=expf(-HTA_VEHICLE_YAW_DAMP*h);
    if(fabsf(car->yaw_rate)<.001f)car->yaw_rate=0;
    /* A tread turns the hull the way the stick pushes: right is clockwise. */
    car->steering=approach(car->steering,-turn*car->turn_rate,car->turn_rate*4*h);
    if(car->grounded || car->traction)car->yaw+=car->steering*h;
}

/* The Ghost: turn toward the driver's look; push along the stick. */
static void drive_scout(hta_vehicle *car, float h)
{
    bool driven=car->ctl.driven;
    /* The world velocity, before the hull turns under it. */
    float now[2];planar_velocity(car,now);
    if(driven)car->yaw=approach_angle(car->yaw,car->ctl.yaw,car->turn_rate*h);
    float f=driven?clamp(car->ctl.throttle,-1,1):0,s=driven?clamp(car->ctl.strafe,-1,1):0;
    float fwd=f>=0 ? f*car->forward : f*car->reverse;
    float side=s*car->forward*HTA_SCOUT_STRAFE_FRACTION;
    float cy=cosf(car->yaw),sy=sinf(car->yaw);
    float want[2]={cy*fwd+sy*side,sy*fwd-cy*side};
    bool pushing=fabsf(f)>.01f || fabsf(s)>.01f;
    /* Along the hull it answers the stick; across it, it slides. */
    float rate=(pushing?car->accel:car->decel*HTA_VEHICLE_COAST_HOVER)*h;
    float along=(want[0]-now[0])*cy+(want[1]-now[1])*sy;
    float across=(want[0]-now[0])*sy-(want[1]-now[1])*cy;
    float grip=rate*(pushing?HTA_VEHICLE_DRIFT_GRIP:1.0f);
    along=clamp(along,-rate,rate);across=clamp(across,-grip,grip);
    now[0]+=cy*along+sy*across;now[1]+=sy*along-cy*across;
    set_planar_velocity(car,now);
    car->yaw_rate*=expf(-HTA_VEHICLE_YAW_DAMP*h);
    if(fabsf(car->yaw_rate)<.001f)car->yaw_rate=0;
    car->steering=0;
}

/* The Banshee: nose follows the pilot's look, throttle flies along it. */
static void drive_fighter(hta_vehicle *car, float h)
{
    bool driven=car->ctl.driven;
    float old_yaw=car->yaw;
    float held[2];planar_velocity(car,held);
    if(driven){
        car->yaw=approach_angle(car->yaw,car->ctl.yaw,car->turn_rate*h);
        float want_pitch=-clamp(car->ctl.pitch,-HTA_FIGHTER_MAX_PITCH,HTA_FIGHTER_MAX_PITCH);
        car->pitch=approach(car->pitch,want_pitch,car->turn_rate*h);
        car->roll=approach(car->roll,0,h);
    }
    float f=driven?clamp(car->ctl.throttle,-1,1):0,s=driven?clamp(car->ctl.strafe,-1,1):0;
    float speed=f>=0 ? f*car->forward : f*car->reverse;
    float side=s*car->forward*HTA_FIGHTER_STRAFE_FRACTION;
    /* Flight is along the nose, pitch included. */
    float cp=cosf(-car->pitch),sp=sinf(-car->pitch),cy=cosf(car->yaw),sy=sinf(car->yaw);
    float want[3]={cy*cp*speed+sy*side,sy*cp*speed-cy*side,sp*speed};
    float now[3]={held[0],held[1],car->fall_speed};
    if(!driven)want[2]=now[2];   /* nobody flying it: gravity has the say */
    bool pushing=fabsf(f)>.01f || fabsf(s)>.01f;
    float rate=(pushing?car->accel:car->decel*HTA_VEHICLE_COAST_FLYER)*h;
    /* Along the nose it answers the stick; across it, it slides. */
    float dx=want[0]-now[0],dy=want[1]-now[1],dz=want[2]-now[2];
    float along=dx*cy+dy*sy,across=dx*sy-dy*cy;
    float grip=rate*(pushing?HTA_VEHICLE_DRIFT_GRIP:1.0f);
    along=clamp(along,-rate,rate);across=clamp(across,-grip,grip);
    /* Height holds: a pilot who lets go hangs where they are. */
    float lift=(pushing?car->accel:car->decel)*h;dz=clamp(dz,-lift,lift);
    dx=cy*along+sy*across;dy=sy*along-cy*across;
    now[0]+=dx;now[1]+=dy;now[2]+=dz;
    set_planar_velocity(car,now);car->fall_speed=now[2];
    float turn=hta_angle_wrap(car->yaw-old_yaw)/h;
    car->bank=approach(car->bank,clamp(-turn*HTA_FIGHTER_BANK,-.6f,.6f),h);
    car->yaw_rate*=expf(-HTA_VEHICLE_YAW_DAMP*h);
    if(fabsf(car->yaw_rate)<.001f)car->yaw_rate=0;
}

/* The body on its springs: nose up under throttle, down under the brakes,
 * rolled out of a turn. Visual and small; it rides in the pose. */
static void sway(hta_vehicle *car,const hta_vehicle *old,float h)
{
    float k=car->kind==HTA_VK_JEEP ? HTA_VEHICLE_SWAY_JEEP
          : car->kind==HTA_VK_TANK ? HTA_VEHICLE_SWAY_TANK
          : car->kind==HTA_VK_SCOUT ? HTA_VEHICLE_SWAY_HOVER : 0;
    float want[2]={0,0};
    if(k>0 && car->grounded && h>0){
        float a=(car->speed-old->speed)/h;
        float turn=hta_angle_wrap(car->yaw-old->yaw)/h;
        want[0]=clamp(-a*k,-HTA_VEHICLE_SWAY_MAX,HTA_VEHICLE_SWAY_MAX);
        want[1]=clamp(car->speed*turn*k,-HTA_VEHICLE_SWAY_MAX,HTA_VEHICLE_SWAY_MAX);
    }
    for(int i=0;i<2;i++){
        float acc=(want[i]-car->sway[i])*HTA_VEHICLE_SWAY_SPRING-car->sway_vel[i]*HTA_VEHICLE_SWAY_DAMP;
        car->sway_vel[i]+=acc*h;
        car->sway[i]=clamp(car->sway[i]+car->sway_vel[i]*h,-2*HTA_VEHICLE_SWAY_MAX,2*HTA_VEHICLE_SWAY_MAX);
        if(fabsf(car->sway[i])<1e-5f && fabsf(car->sway_vel[i])<1e-4f && want[i]==0)
            car->sway[i]=car->sway_vel[i]=0;
    }
}

void hta_vehicles_push(hta_vehicles *v, uint32_t ci, const float vel[3], float spin, float kick)
{
    if(!v || ci>=v->count || !vel)return;
    hta_vehicle *car=&v->cars[ci];
    if(!car->active || car->kind==HTA_VK_TURRET)return;
    float p[2];planar_velocity(car,p);
    p[0]+=vel[0];p[1]+=vel[1];
    set_planar_velocity(car,p);
    if(vel[2]>0.3f && car->kind!=HTA_VK_FIGHTER){
        /* Off the ground: gravity has it now. */
        car->fall_speed=fmaxf(car->fall_speed,0)+vel[2];
        car->rise_speed=car->fall_speed;
        car->grounded=false;car->traction=false;
    } else car->fall_speed+=vel[2];
    /* Tumble about the axis across the push. */
    float hx=vel[0],hy=vel[1],hl=hypotf(hx,hy);
    if(hl>1e-4f && spin>0){
        float fwd=(hx*cosf(car->yaw)+hy*sinf(car->yaw))/hl;
        float side=(hx*sinf(car->yaw)-hy*cosf(car->yaw))/hl;
        car->tumble[0]+=-fwd*spin;car->tumble[1]+=side*spin;
    }
    if(hl>1e-4f && kick!=0){
        float fwd=(hx*cosf(car->yaw)+hy*sinf(car->yaw))/hl;
        float side=(hx*sinf(car->yaw)-hy*cosf(car->yaw))/hl;
        car->sway_vel[0]+=fwd*kick*HTA_VEHICLE_SWAY_SPRING*0.25f;
        car->sway_vel[1]+=side*kick*HTA_VEHICLE_SWAY_SPRING*0.25f;
    }
    car->rest_time=0;
}

static bool asleep(const hta_vehicle *car)
{
    return !car->ctl.driven && car->grounded && car->speed==0 &&
        car->lateral_vel[0]==0 && car->lateral_vel[1]==0 && car->yaw_rate==0 &&
        car->barrel_speed==0 && car->sway[0]==0 && car->sway[1]==0 &&
        car->rest_time>=HTA_VEHICLE_SETTLE_TIME;
}

void hta_vehicles_update(hta_vehicles *v,const hta_collision *world,float gravity,float dt)
{
    if(!v || !v->loaded || !world || !world->built || !(dt>0))return;
    hta_collision terrain=*world;terrain.extra=NULL;terrain.instances=NULL;terrain.instance_count=0;
    dt=fminf(dt,0.1f);
    unsigned steps=(unsigned)ceilf(dt/HTA_VEHICLE_STEP);float h=dt/steps;
    for(unsigned step=0;step<steps;step++)for(uint32_t i=0;i<v->count;i++) {
        hta_vehicle *car=&v->cars[i],old=*car;
        if(!car->active || car->kind==HTA_VK_TURRET)continue;
        if(asleep(car))continue;
        bool driven=car->ctl.driven;
        bool flying=car->kind==HTA_VK_FIGHTER && driven;
        switch(car->kind){
        case HTA_VK_TANK: drive_tank(car,h); break;
        case HTA_VK_SCOUT: drive_scout(car,h); break;
        case HTA_VK_FIGHTER: drive_fighter(car,h); break;
        default: drive_jeep(car,h); break;
        }
        if(car->kind==HTA_VK_FIGHTER && !driven){
            /* An empty Banshee settles: level out and come down. */
            car->bank=approach(car->bank,0,h);
            float lateral=hypotf(car->lateral_vel[0],car->lateral_vel[1]);
            if(lateral>0){float left=fmaxf(0,lateral-car->decel*h);
                car->lateral_vel[0]*=left/lateral;car->lateral_vel[1]*=left/lateral;}
        }
        car->yaw+=car->yaw_rate*h;
        car->pos[0]+=(cosf(car->yaw)*car->speed+car->lateral_vel[0])*h;
        car->pos[1]+=(sinf(car->yaw)*car->speed+car->lateral_vel[1])*h;
        if(!car->grounded && (car->tumble[0]!=0 || car->tumble[1]!=0)){
            /* Thrown: it turns over in the air until it lands. */
            car->pitch=clamp(car->pitch+car->tumble[0]*h,-1.3f,1.3f);
            car->roll=clamp(car->roll+car->tumble[1]*h,-1.3f,1.3f);
        }
        if(flying)fly_support(car,&terrain,h);
        else support(car,&terrain,gravity,h);
        if(car->grounded){car->tumble[0]=car->tumble[1]=0;}
        sway(car,&old,h);
        hta_vehicle_hit hit={.other=-1};
        if(blocked(v,i,car,&old,&terrain,&hit)) {
            hta_vehicle attempted=*car;
            *car=old;
            if(driven)car->blocked+=h;
            car->steering=attempted.steering;
            car->speed=attempted.speed;
            memcpy(car->lateral_vel,attempted.lateral_vel,sizeof(car->lateral_vel));
            car->yaw_rate=attempted.yaw_rate;
            car->fall_speed=attempted.fall_speed;
            /* The blocked horizontal move still advances suspension/gravity. */
            if(flying){car->fall_speed=0;fly_support(car,&terrain,h);}
            else support(car,&terrain,gravity,h);
            impact(v,i,&hit);
            /* Engine force at a turned front axle produces torque even when
             * translation is blocked. This lets the tires work the jeep out
             * of a shallow wedge, subject to the same collision check next
             * step. Mass/inertia and drive acceleration come from the tags. */
            float gas=driven?clamp(car->ctl.throttle,-1,1):0;
            if(car->kind==HTA_VK_JEEP && driven && (car->grounded || car->traction) &&
               !car->ctl.brake && fabsf(gas)>.01f && fabsf(car->steering)>.01f){
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
            /* A tank or Ghost turning in place against a wall: let the turn
             * happen if the turned hull fits. */
            if(car->kind!=HTA_VK_JEEP && old.yaw!=attempted.yaw){
                hta_vehicle turned=*car;turned.yaw=attempted.yaw;
                if(!blocked(v,i,&turned,car,&terrain,NULL))car->yaw=attempted.yaw;
            }
        }
        car->yaw=hta_angle_wrap(car->yaw);
        car->rest_time=(!driven && car->grounded && car->speed==0 &&
                        car->lateral_vel[0]==0 && car->lateral_vel[1]==0 &&
                        car->yaw_rate==0)
            ? fminf(HTA_VEHICLE_SETTLE_TIME,car->rest_time+h) : 0;
        if(!driven && car->grounded && fabsf(car->speed)<1e-3f)car->speed=0;
        if(!driven && car->grounded && hypotf(car->lateral_vel[0],car->lateral_vel[1])<1e-3f)
            car->lateral_vel[0]=car->lateral_vel[1]=0;
        if(car->circumference>0 && car->kind==HTA_VK_JEEP){
            float gas=driven?clamp(car->ctl.throttle,-1,1):0;bool brake=car->ctl.brake;
            float wheel_target=driven && !brake && fabsf(gas)>.01f
                ? (gas>=0?gas*car->forward:gas*car->reverse) : car->speed;
            float wheel_rate=driven && !brake && fabsf(gas)>.01f &&
                             car->wheel_speed*wheel_target>=0 ? car->accel : car->decel;
            car->wheel_speed=approach(car->wheel_speed,wheel_target,wheel_rate*h);
            car->wheel_spin+=car->wheel_speed*h/car->circumference*6.2831853f;
            if(fabsf(car->wheel_spin)>6.2831853f)
                car->wheel_spin=remainderf(car->wheel_spin,6.2831853f);
        }
        if(car->barrel_speed!=0){
            car->barrel_spin=remainderf(car->barrel_spin+car->barrel_speed*h,6.2831853f);
            car->barrel_speed=approach(car->barrel_speed,0,HTA_VEHICLE_BARREL_SPIN*h);
        }
    }
    /* Abandoned vehicles go home. */
    for(uint32_t i=0;i<v->count;i++){
        hta_vehicle *car=&v->cars[i];bool empty=true;
        if(!car->active)continue;
        for(uint32_t s=0;s<HTA_VEHICLE_SEATS;s++)if(car->occupant[s]>=0)empty=false;
        float away=hypotf(car->pos[0]-car->home_pos[0],car->pos[1]-car->home_pos[1]);
        if(!empty || away<1.0f){car->idle=0;continue;}
        car->idle+=dt;
        if(car->idle>=HTA_VEHICLE_RESPAWN)hta_vehicles_reset(v,i);
    }
    hta_vehicles_sync(v);
}

/* ------------------------------------------------------------ cameras */

bool hta_vehicles_third_person(const hta_vehicles *v, uint32_t car, uint32_t seat)
{
    const hta_vehicle_seat *s=hta_vehicles_seat(v,car,seat);
    if(!s)return true;
    if(s->flags&HTA_SEAT_THIRD_PERSON)return true;
    return s->camera_node<0;
}

void hta_vehicles_camera(const hta_vehicles *v,const hta_collision *world,uint32_t ci,
                         uint32_t seat,float yaw,float pitch,hta_camera *cam)
{
    if(!v || ci>=v->count || !cam)return;
    const hta_vehicle *car=&v->cars[ci];
    const hta_vehicle_seat *s=hta_vehicles_seat(v,ci,seat);
    const hta_vehicle_type *t=has_type(v,car) ? &v->types[car->type] : NULL;
    cam->yaw=yaw;
    cam->pitch=pitch;
    if(!hta_vehicles_third_person(v,ci,seat) && t){
        /* A first-person seat looks out of its own camera marker. */
        hta_transform nodes[HTA_VEHICLE_NODES],world_xf,nw,posed;
        hta_vehicles_nodes(v,ci,nodes);hta_vehicles_world(v,ci,&world_xf);
        if(s->camera_node>=0 && (uint32_t)s->camera_node<t->node_count){
            hta_xf_mul(&posed,&nodes[s->camera_node],&t->node_rest[s->camera_node]);
            hta_xf_mul(&nw,&world_xf,&posed);
        } else nw=world_xf;
        hta_xf_point(cam->pos,&nw,s->camera);
        return;
    }
    /* Chase: behind and above the hull, pulled in by terrain. Scaled to
     * the vehicle so a tank is not filmed from inside its turret. */
    float scale=fmaxf(1.0f,car->body_radius/1.1f);
    float origin[3]={car->pos[0],car->pos[1],car->pos[2]+HTA_VEHICLE_CAMERA_UP*scale};
    float fwd[3];hta_camera_forward(cam,fwd);
    float d[3]={-fwd[0],-fwd[1],-fwd[2]},distance=HTA_VEHICLE_CAMERA_BACK*scale,tt;
    hta_collision terrain={0};
    if(world){terrain=*world;terrain.extra=NULL;terrain.instances=NULL;terrain.instance_count=0;}
    if(world && hta_collision_ray(&terrain,origin,d,distance,&tt,NULL,NULL))distance=fmaxf(0,tt-0.15f);
    for(int k=0;k<3;k++)cam->pos[k]=origin[k]+d[k]*distance;
}

/* ------------------------------------------------------------ drawing */

static void xf_matrix(const hta_transform *x, float m[16])
{
    float ex[3]={1,0,0},ey[3]={0,1,0},ez[3]={0,0,1},a[3],b[3],c[3];
    hta_xf_vector(a,x,ex);hta_xf_vector(b,x,ey);hta_xf_vector(c,x,ez);
    for(int k=0;k<3;k++){m[k]=a[k];m[4+k]=b[k];m[8+k]=c[k];m[12+k]=x->t[k];}
    m[3]=m[7]=m[11]=0;m[15]=1;
}

uint32_t hta_vehicles_parts(const hta_vehicles *v, hta_vehicle_part *out, uint32_t max)
{
    uint32_t n=0;
    if(!v || !v->loaded || !out)return 0;
    for(uint32_t i=0;i<v->count;i++){
        const hta_vehicle *car=&v->cars[i];
        if(!car->active || !has_type(v,car))continue;
        const hta_vehicle_type *t=&v->types[car->type];
        hta_transform nodes[HTA_VEHICLE_NODES],world;
        hta_vehicles_nodes(v,i,nodes);hta_vehicles_world(v,i,&world);
        for(uint32_t p=0;p<t->part_count && n<max;p++){
            hta_transform m;
            int16_t node=t->part_node[p];
            if(node>=0 && (uint32_t)node<t->node_count)hta_xf_mul(&m,&world,&nodes[node]);
            else m=world;
            out[n].type=car->type;
            out[n].first_submesh=t->part_first[p];
            out[n].submesh_count=t->part_submeshes[p];
            xf_matrix(&m,out[n].model);
            n++;
        }
    }
    return n;
}
