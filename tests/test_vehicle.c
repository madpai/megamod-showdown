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
static void crest_mesh(hta_bsp_mesh *m)
{
    static const float x[]={-20,0,2,4,20};
    static const float z[]={0,0,.8f,.8f,-2.4f};
    memset(m,0,sizeof(*m));m->vertex_count=10;m->index_count=24;
    m->vertices=calloc(m->vertex_count,sizeof(*m->vertices));
    m->indices=calloc(m->index_count,sizeof(*m->indices));
    m->tri_material=calloc(m->index_count/3,1);
    for(uint32_t i=0;i<5;i++)for(uint32_t side=0;side<2;side++){
        hta_vertex *v=&m->vertices[i*2+side];v->pos[0]=x[i];
        v->pos[1]=side?10:-10;v->pos[2]=z[i];
    }
    for(uint32_t i=0;i<4;i++){
        uint32_t a=i*2,b=a+2,t=i*6;
        m->indices[t+0]=a;m->indices[t+1]=b;m->indices[t+2]=b+1;
        m->indices[t+3]=a;m->indices[t+4]=b+1;m->indices[t+5]=a+1;
    }
    m->bounds_min[0]=-20;m->bounds_max[0]=20;
    m->bounds_min[1]=-10;m->bounds_max[1]=10;
    m->bounds_min[2]=-2.4f;m->bounds_max[2]=.8f;
}
static void corridor_mesh(hta_bsp_mesh *m)
{
    floor_mesh(m,false);
    m->vertices=realloc(m->vertices,12*sizeof(*m->vertices));
    m->indices=realloc(m->indices,18*sizeof(*m->indices));
    m->tri_material=realloc(m->tri_material,6);
    memset(m->tri_material+2,0,4);
    const float wall[8][3]={
        {-10,-.85f,0},{10,-.85f,0},{10,-.85f,5},{-10,-.85f,5},
        {-10,.85f,0},{10,.85f,0},{10,.85f,5},{-10,.85f,5}};
    for(int i=0;i<8;i++)memcpy(m->vertices[4+i].pos,wall[i],sizeof(wall[i]));
    const uint32_t ix[12]={4,5,6,4,6,7,8,9,10,8,10,11};
    memcpy(m->indices+6,ix,sizeof(ix));m->vertex_count=12;m->index_count=18;
}
static void rig(hta_vehicles *v)
{
    memset(v,0,sizeof(*v));v->loaded=true;v->count=1;v->driver=0;
    hta_vehicle *c=&v->cars[0];c->forward=8.25f;c->reverse=3.6f;c->accel=2.52f;c->decel=9.9f;
    c->turn_left=0.5235988f;c->turn_right=-c->turn_left;c->turn_rate=3.14159265f;
    c->gravity_scale=1;c->wheelbase=1.3f;c->circumference=1;c->grounded=true;c->pos[2]=0.07f;
    c->mass=5000;c->yaw_inertia=1990;c->ground_friction=.23f;c->ground_depth=.15f;
    c->body_radius=1.1f;
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
    CHECK(v.cars[0].pos[0]<9.2f && fabsf(v.cars[0].speed)<1,
        "wheel/body volume stops before a wall");
    CHECK(v.cars[0].wheel_speed>5,
        "powered tires keep spinning against a wall while the chassis barely moves");
    rig(&v);v.cars[0].speed=8.25f;v.cars[0].pos[0]=8.8f;
    bool bounced=false;
    for(int q=0;q<8;q++){
        run(&v,&col,1,0,false,1,1.f/120);
        if(v.cars[0].speed<-.5f)bounced=true;
    }
    CHECK(bounced,"a fast wall hit produces a short rebound instead of zeroing speed");
    run(&v,&col,1,0,false,1,1.f); /* long stall is bounded */
    CHECK(v.cars[0].pos[0]<9.2f,"long frame does not tunnel through wall");
    rig(&v);v.cars[0].pos[0]=9.07f;
    run(&v,&col,1,1,false,180,1.f/60);
    CHECK(v.cars[0].yaw<-.1f && v.cars[0].pos[1]>.08f && v.cars[0].wheel_speed>5,
        "powered steering pivots a blocked jeep around its contact");
    rig(&v);v.cars[0].pos[0]=8.5f;v.cars[0].pos[1]=-3;v.cars[0].yaw=.7854f;
    v.cars[0].speed=6;v.cars[0].wheel_speed=6;
    run(&v,&col,1,0,false,120,1.f/60);
    CHECK(v.cars[0].pos[0]<9.2f && v.cars[0].pos[1]>0 && fabsf(v.cars[0].speed)>3,
        "a glancing wall impact deflects the jeep without killing its momentum");
    hta_collision_free(&col);hta_bsp_free(&m);corridor_mesh(&m);hta_collision_build(&col,&m);
    rig(&v);v.cars[0].yaw=.35f;
    run(&v,&col,1,1,false,180,1.f/60);
    CHECK(v.cars[0].pos[0]>1 && fabsf(v.cars[0].pos[1])<.5f,
        "powered steering works a canted jeep through a narrow corridor");
    hta_collision_free(&col);hta_bsp_free(&m);crest_mesh(&m);hta_collision_build(&col,&m);
    rig(&v);v.cars[0].pos[0]=-3;v.cars[0].speed=8.25f;v.cars[0].ground_depth=.23f;
    int airborne=0;float furthest=-3;
    for(int i=0;i<100;i++){
        run(&v,&col,1,0,false,1,1.f/60);
        if(v.cars[0].pos[0]>4 && !v.cars[0].grounded)airborne++;
        furthest=fmaxf(furthest,v.cars[0].pos[0]);
    }
    CHECK(furthest>6,"jeep drives across a climb and crest");
    CHECK(airborne>5,"fast jeep leaves the ground over a falling crest");
    hta_collision_free(&col);hta_bsp_free(&m);floor_mesh(&m,false);hta_collision_build(&col,&m);
    rig(&v);v.cars[0].pos[2]=.2f;v.cars[0].grounded=false;
    v.cars[0].speed=2;v.cars[0].ground_depth=.23f;
    run(&v,&col,1,1,false,2,1.f/60);
    CHECK(!v.cars[0].grounded && v.cars[0].traction && v.cars[0].yaw<0,
        "suspension contact can steer while chassis follows a free arc");
    rig(&v);v.count=2;v.cars[1]=v.cars[0];v.cars[1].pos[0]=2.15f;
    run(&v,&col,-1,0,false,60,1.f/60);
    CHECK(v.cars[0].pos[0]<-.1f && v.cars[0].speed<0,
        "reverse frees a jeep whose collision proxies already overlap another jeep");
    rig(&v);v.count=2;v.cars[1]=v.cars[0];v.cars[1].pos[0]=2.5f;
    run(&v,&col,1,0,false,120,1.f/60);
    CHECK(v.cars[0].pos[0]<1 && v.cars[1].pos[0]>2.52f,
        "vehicle impulse transfers motion without letting jeeps pass through");
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
    CHECK(fabsf(v.cars[0].ground_depth-.15f)<.001f &&
          fabsf(v.cars[0].ground_friction-.23f)<.001f &&
          fabsf(v.cars[0].mass-5000)<1 && fabsf(v.cars[0].yaw_inertia-1990.22f)<.1f,
        "real suspension depth, friction, mass and yaw inertia use distinct phys fields");
    CHECK(v.collision.built && v.coll_mesh.index_count>0,"moving collider grid builds");
    CHECK(v.cars[0].seat[1]>0,"driver marker resolves on left side");
    hta_bsp_mesh flat;floor_mesh(&flat,false);hta_collision flat_col={0};
    hta_collision_build(&flat_col,&flat);
    hta_vehicles pair={0};pair.loaded=true;pair.count=2;pair.driver=0;
    pair.cars[0]=v.cars[1];pair.cars[1]=v.cars[1];
    for(int i=0;i<2;i++){
        hta_vehicle *p=&pair.cars[i];p->pos[0]=i?2.15f:0;p->pos[1]=0;p->pos[2]=.07f;
        p->yaw=p->pitch=p->roll=p->speed=p->fall_speed=p->rise_speed=0;
        p->grounded=true;p->traction=true;p->rest_time=0;
        p->first_vertex=p->vertex_count=p->first_coll=p->coll_count=0;
    }
    run(&pair,&flat_col,-1,0,false,60,1.f/60);
    CHECK(pair.cars[0].pos[0]<-.1f && pair.cars[0].speed<0,
        "real Warthog mass points can back out of an existing vehicle overlap");
    hta_collision_free(&flat_col);hta_bsp_free(&flat);
    int wheel=-1;
    for(uint32_t i=0;i<v.cars[1].point_count;i++)if(v.cars[1].points[i].wheel){wheel=(int)i;break;}
    CHECK(wheel>=0 && fabsf(v.cars[1].points[wheel].visual_radius-.20f)<.03f,
        "wheel art radius is measured from its model node, not the larger physics radius");
    hta_bsp_mesh mesh={0};hta_collision col={0};
    ok=hta_bsp_load_collision(&c,&mesh,err,sizeof(err));CHECK(ok,"map collision loads");
    if(ok){
        hta_scenario_add_collision_excluding(&mesh,&c,v.skip,sizeof(v.skip),err,sizeof(err));
        hta_collision_build(&col,&mesh);col.extra=&v.collision;
        run(&v,&col,0,0,false,60,1.f/60);
        CHECK(v.cars[1].grounded,"spawn-side Warthog settles on real terrain");
        hta_vehicle *car=&v.cars[1];uint32_t vi=car->first_vertex;
        CHECK(car->rest_time>=HTA_VEHICLE_SETTLE_TIME && car->pitch<-.08f,
            "parked Warthog keeps settling its body angle before sleeping");
        bool tires_touch=true;
        bool tires_near_body=true;
        for(uint32_t wi=0;wi<car->point_count;wi++)if(car->points[wi].wheel){float low=INFINITY,gz=0;float bx=0,by=0;
            for(uint32_t j=car->first_vertex;j<car->first_vertex+car->vertex_count;j++)if(v.render_nodes[j]==car->points[wi].node && v.mesh.vertices[j].pos[2]<low){low=v.mesh.vertices[j].pos[2];bx=v.mesh.vertices[j].pos[0];by=v.mesh.vertices[j].pos[1];}
            hta_collision terrain=col;terrain.extra=NULL;
            if(!hta_collision_ground(&terrain,bx,by,low+1,&gz) || fabsf(low-gz)>.025f)
                tires_touch=false;
            if(fabsf(car->points[wi].travel)>.12f)tires_near_body=false;
        }
        CHECK(tires_touch,"resting Warthog tires visually reach real terrain");
        CHECK(tires_near_body,"parked chassis settles near its wheels");
        while(vi<car->first_vertex+car->vertex_count &&
              v.render_nodes[vi]!=car->points[wheel].node)vi++;
        CHECK(vi<car->first_vertex+car->vertex_count,"rendered tire vertices retain their model node");
        if(vi<car->first_vertex+car->vertex_count){
            float before[3];memcpy(before,v.mesh.vertices[vi].pos,sizeof(before));
            car->wheel_spin+=1.0f;hta_vehicles_pose(&v);
            CHECK(hypotf(v.mesh.vertices[vi].pos[0]-before[0],
                        v.mesh.vertices[vi].pos[2]-before[2])>.02f,
                "wheel spin moves the tire geometry around its tagged mass point");
            car->wheel_spin-=1.0f;hta_vehicles_pose(&v);
        }
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
