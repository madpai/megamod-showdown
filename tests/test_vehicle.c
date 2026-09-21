#include "engine/vehicle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks, failures;
#define CHECK(c,msg) do { checks++; if(!(c)){failures++;printf("FAIL: %s\n",msg);} }while(0)
static void floor_mesh(hta_bsp_mesh *m, bool wall)
{
    memset(m,0,sizeof(*m));m->vertex_count=wall?8:4;m->index_count=wall?12:6;
    m->vertices=calloc(m->vertex_count,sizeof(*m->vertices));m->indices=calloc(m->index_count,sizeof(*m->indices));
    m->tri_material=calloc(m->index_count/3,1);
    const float pos[8][3]={{-50,-50,0},{50,-50,0},{50,50,0},{-50,50,0},
        {10,-50,0},{10,50,0},{10,50,5},{10,-50,5}};
    const uint32_t ix[12]={0,1,2,0,2,3,4,5,6,4,6,7};
    for(uint32_t i=0;i<m->vertex_count;i++)memcpy(m->vertices[i].pos,pos[i],sizeof(pos[i]));
    memcpy(m->indices,ix,m->index_count*sizeof(*ix));
    for(int k=0;k<2;k++){m->bounds_min[k]=-50;m->bounds_max[k]=50;}
}
static void rig(hta_vehicles *v)
{
    memset(v,0,sizeof(*v));v->loaded=true;v->count=1;v->driver=0;
    hta_vehicle *c=&v->cars[0];c->forward=8.25f;c->reverse=3.6f;c->accel=2.52f;c->decel=9.9f;
    c->turn_left=0.5235988f;c->turn_right=-c->turn_left;c->turn_rate=3.14159265f;
    c->gravity_scale=1;c->wheelbase=1.3f;c->circumference=1;c->grounded=true;c->pos[2]=0.07f;
    c->point_count=4;
    for(int i=0;i<4;i++){
        c->points[i].wheel=true;c->points[i].pos[0]=i<2?0.67f:-0.63f;
        c->points[i].pos[1]=i%2?0.38f:-0.38f;c->points[i].pos[2]=0.19f;c->points[i].radius=0.26f;
    }
}
static void run(hta_vehicles *v,const hta_collision *col,float gas,float steer,bool brake,int steps,float dt)
{ for(int i=0;i<steps;i++)hta_vehicles_update(v,col,gas,steer,brake,3.57f,dt); }
static void synthetic(void)
{
    hta_bsp_mesh m;floor_mesh(&m,false);hta_collision col={0};
    CHECK(hta_collision_build(&col,&m),"flat test terrain builds");
    hta_vehicles v;rig(&v);run(&v,&col,1,0,false,120,1.f/60);
    CHECK(fabsf(v.cars[0].speed-5.04f)<0.03f,"acceleration uses seconds consistently");
    CHECK(v.cars[0].pos[0]>4.9f && v.cars[0].pos[0]<5.2f,"accelerating jeep moves expected distance");
    CHECK(v.cars[0].grounded && fabsf(v.cars[0].pos[2]-.07f)<.001f,"wheel radius supports chassis above ground");
    float x=v.cars[0].pos[0];run(&v,&col,1,0,true,60,1.f/60);
    CHECK(v.cars[0].speed==0 && v.cars[0].pos[0]>x,"brake stops without reversing");
    run(&v,&col,-1,0,false,180,1.f/60);
    CHECK(fabsf(v.cars[0].speed+3.6f)<.01f,"reverse obeys tagged speed limit");
    rig(&v);run(&v,&col,1,0,false,300,1.f/60);
    CHECK(fabsf(v.cars[0].speed-8.25f)<.01f,"forward obeys speed limit");
    rig(&v);run(&v,&col,1,1,false,90,1.f/60);
    CHECK(v.cars[0].yaw<-.1f && v.cars[0].pos[1]<0,"right input steers right");
    rig(&v);run(&v,&col,0,1,false,90,1.f/60);
    CHECK(v.cars[0].yaw==0,"stationary jeep cannot rotate in place");
    hta_vehicles other;rig(&v);rig(&other);
    run(&v,&col,1,-.2f,false,60,1.f/30);run(&other,&col,1,-.2f,false,240,1.f/120);
    CHECK(hypotf(v.cars[0].pos[0]-other.cars[0].pos[0],v.cars[0].pos[1]-other.cars[0].pos[1])<.02f,
        "driving is stable across frame rates");
    rig(&v);v.cars[0].pos[2]=2;v.cars[0].grounded=false;
    run(&v,&col,0,0,false,90,1.f/60);
    CHECK(v.cars[0].grounded && fabsf(v.cars[0].pos[2]-.07f)<.01f,"airborne chassis lands on wheels");
    rig(&v);hta_player p;hta_player_init(&p);hta_camera cam;hta_camera_init(&cam);
    v.cars[0].speed=1;
    CHECK(!hta_vehicles_exit(&v,&col,&p,&cam),"moving exit refused");
    v.cars[0].speed=0;
    CHECK(hta_vehicles_exit(&v,&col,&p,&cam) && v.driver==-1,"stopped exit places player beside jeep");
    CHECK(p.on_ground && fabsf(p.pos[2])<.01f,"exit starts on ground");
    hta_collision_free(&col);hta_bsp_free(&m);floor_mesh(&m,true);hta_collision_build(&col,&m);
    rig(&v);run(&v,&col,1,0,false,600,1.f/60);
    CHECK(v.cars[0].pos[0]<9.2f && v.cars[0].speed==0,"wheel/body volume stops before a wall");
    rig(&v);v.cars[0].speed=8.25f;v.cars[0].pos[0]=8.8f;
    run(&v,&col,1,0,false,1,1.f); /* long stall is bounded */
    CHECK(v.cars[0].pos[0]<9.2f,"long frame does not tunnel through wall");
    /* Composite grids must choose nearest hits and retain material when the
     * extra grid misses; this guards moving vehicles and all existing shots. */
    hta_bsp_mesh extra;floor_mesh(&extra,false);
    for(uint32_t i=0;i<extra.vertex_count;i++)extra.vertices[i].pos[2]=1;
    extra.tri_material[0]=extra.tri_material[1]=HTA_MATERIAL_NONE;
    hta_collision ec={0};hta_collision_build(&ec,&extra);col.extra=&ec;
    float z=0,t=0,origin[3]={0,0,3},down[3]={0,0,-1};uint8_t mat=0;
    CHECK(hta_collision_ground(&col,0,0,3,&z) && fabsf(z-1)<.001f,"ground selects higher dynamic surface");
    CHECK(hta_collision_ray_material(&col,origin,down,4,&t,NULL,NULL,&mat) && fabsf(t-2)<.001f,
        "ray selects nearest dynamic surface");
    origin[2]=.5f;mat=99;
    CHECK(hta_collision_ray_material(&col,origin,down,2,&t,NULL,NULL,&mat) && mat==0,
        "extra-grid miss preserves static hit material");
    hta_collision_free(&ec);hta_bsp_free(&extra);hta_collision_free(&col);hta_bsp_free(&m);
}
static unsigned char *slurp(const char *path,size_t *n)
{
    FILE*f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<=0){fclose(f);return NULL;}unsigned char*d=malloc((size_t)size);
    if(!d){fclose(f);return NULL;}if(fread(d,1,(size_t)size,f)!=(size_t)size){free(d);fclose(f);return NULL;}
    fclose(f);*n=(size_t)size;return d;
}
static void real_map(const char *path)
{
    size_t n;unsigned char*d=slurp(path,&n);CHECK(d,"map readable");if(!d)return;
    hta_cache c;char err[HTA_ERRLEN];bool ok=hta_cache_open(&c,d,n,err,sizeof(err));
    CHECK(ok,"map opens");if(!ok){free(d);return;}
    hta_vehicles v;ok=hta_vehicles_load(&v,&c,NULL,err,sizeof(err));
    CHECK(ok,"Trial human jeeps and their collision meshes load");if(!ok){free(d);return;}
    CHECK(v.count==12,"all twelve standard and rocket Warthog placements move");
    CHECK(fabsf(v.cars[0].forward-8.25f)<.001f && fabsf(v.cars[0].accel-2.52f)<.001f,
        "real per-tick vehicle values converted to seconds");
    CHECK(v.collision.built && v.coll_mesh.index_count>0,"moving collider grid builds");
    CHECK(v.cars[0].seat[1]>0,"driver marker resolves on left side");
    hta_bsp_mesh mesh={0};hta_collision col={0};
    ok=hta_bsp_load_collision(&c,&mesh,err,sizeof(err));CHECK(ok,"map collision loads");
    if(ok){
        hta_scenario_add_collision_excluding(&mesh,&c,v.skip,sizeof(v.skip),err,sizeof(err));
        hta_collision_build(&col,&mesh);col.extra=&v.collision;
        run(&v,&col,0,0,false,60,1.f/60);
        CHECK(v.cars[1].grounded,"spawn-side Warthog settles on real terrain");
        hta_player p;hta_player_init(&p);hta_camera cam;hta_camera_init(&cam);
        p.pos[0]=100.83f;p.pos[1]=-144.69f;p.pos[2]=.53f;
        /* Feet from the owner's screenshot, then approach the door. */
        p.pos[0]=101.75f;p.pos[1]=-144.8f;
        int32_t near=hta_vehicles_near(&v,&col,p.pos);
        CHECK(near>=0,"driver seat is reachable beside actual parked Warthog");
        CHECK(hta_vehicles_enter(&v,1),"driver can enter");
        float old[3];memcpy(old,v.cars[1].pos,sizeof(old));
        run(&v,&col,1,0,false,60,1.f/60);
        printf("drive displacement %.3f speed %.3f ground %d\n",hypotf(v.cars[1].pos[0]-old[0],v.cars[1].pos[1]-old[1]),v.cars[1].speed,v.cars[1].grounded);
        CHECK(hypotf(v.cars[1].pos[0]-old[0],v.cars[1].pos[1]-old[1])>1,
            "real Warthog leaves original placement");
        run(&v,&col,0,0,true,60,1.f/60);
        CHECK(hta_vehicles_exit(&v,&col,&p,&cam),"real Warthog exits safely after braking");
        /* Move far enough that the old body's ray cannot hit its new mesh. */
        float ray[3]={old[0],old[1]-2,old[2]+.35f},dir[3]={0,1,0};
        v.cars[1].pos[0]+=10;hta_vehicles_pose(&v);
        CHECK(!hta_collision_ray(&v.collision,ray,dir,4,NULL,NULL,NULL),"no ghost collider at former vehicle position");
        CHECK(hta_collision_ray(&col,ray,(float[3]){0,0,-1},5,NULL,NULL,NULL),"world queries still find terrain after vehicle moves");
    }
    hta_collision_free(&col);hta_bsp_free(&mesh);hta_vehicles_free(&v);free(d);
}
int main(int argc,char **argv)
{synthetic();if(argc>1)real_map(argv[1]);printf("%d checks, %d failures\n",checks,failures);return failures?1:0;}
