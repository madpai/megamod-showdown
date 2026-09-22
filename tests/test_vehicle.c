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
    memset(v,0,sizeof(*v));v->loaded=true;v->count=1;
    v->type_count=1;v->types[0].loaded=true;v->types[0].kind=HTA_VK_JEEP;
    v->types[0].seat_count=1;v->types[0].seats[0].flags=HTA_SEAT_DRIVER|HTA_SEAT_THIRD_PERSON;
    v->types[0].seats[0].node=-1;v->types[0].seats[0].rot[3]=1;v->types[0].seats[0].camera_node=-1;
    v->types[0].seats[0].enter[1]=1.1f;v->types[0].seats[0].pos[2]=.3f;
    v->types[0].yaw_node=v->types[0].pitch_node=v->types[0].barrel_node=-1;
    hta_vehicle *c=&v->cars[0];c->kind=HTA_VK_JEEP;c->active=true;
    for(int s=0;s<HTA_VEHICLE_SEATS;s++)c->occupant[s]=-1;
    c->occupant[0]=0;c->ctl.driven=true;
    c->forward=8.25f;c->reverse=3.6f;c->accel=2.52f;c->decel=9.9f;
    c->turn_left=0.5235988f;c->turn_right=-c->turn_left;c->turn_rate=3.14159265f;
    c->gravity_scale=1;c->wheelbase=1.3f;c->circumference=1;c->grounded=true;c->pos[2]=0.07f;
    c->mass=5000;c->yaw_inertia=1990;c->ground_friction=.23f;c->ground_depth=.15f;
    c->body_radius=1.1f;
    c->point_count=4;
    for(int i=0;i<4;i++){
        c->points[i].wheel=true;c->points[i].pos[0]=i<2?0.67f:-0.63f;
        c->points[i].pos[1]=i%2?0.38f:-0.38f;c->points[i].pos[2]=0.19f;c->points[i].radius=0.26f;
        c->points[i].visual_radius=0.26f;
    }
}
static void drive(hta_vehicles *v,uint32_t car,float gas,float steer,bool brake)
{
    hta_vehicle *c=&v->cars[car];
    c->ctl.throttle=gas;c->ctl.strafe=steer;c->ctl.brake=brake;
}
static void run(hta_vehicles *v,const hta_collision *col,float gas,float steer,bool brake,int steps,float dt)
{
    for(int i=0;i<steps;i++){
        for(uint32_t k=0;k<v->count;k++)if(v->cars[k].ctl.driven)drive(v,k,gas,steer,brake);
        hta_vehicles_update(v,col,3.57f,dt);
    }
}
static void other_driven(hta_vehicles *v,uint32_t k,bool on)
{ v->cars[k].ctl.driven=on;v->cars[k].occupant[0]=on?(int8_t)k:-1; }
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
    rig(&v);float feet[3],fy;
    v.cars[0].speed=1;
    CHECK(!hta_vehicles_exit(&v,&col,0,0,.2f,.7f,feet,&fy),"moving exit refused");
    v.cars[0].speed=0;
    CHECK(hta_vehicles_exit(&v,&col,0,0,.2f,.7f,feet,&fy) && v.cars[0].occupant[0]==-1,
        "stopped exit places player beside jeep and frees the seat");
    CHECK(fabsf(feet[2])<.01f && fabsf(feet[1]-1.1f)<.05f,"exit starts on the ground at the door");
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
    rig(&v);v.count=2;v.cars[1]=v.cars[0];v.cars[1].pos[0]=2.15f;other_driven(&v,1,false);
    run(&v,&col,-1,0,false,60,1.f/60);
    CHECK(v.cars[0].pos[0]<-.1f && v.cars[0].speed<0,
        "reverse frees a jeep whose collision proxies already overlap another jeep");
    rig(&v);v.count=2;v.cars[1]=v.cars[0];v.cars[1].pos[0]=2.5f;other_driven(&v,1,false);
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

    /* A placed rigid grid is queried through its pose, not rebuilt. */
    floor_mesh(&m,false);hta_collision_build(&col,&m);
    hta_bsp_mesh box;floor_mesh(&box,false);
    for(uint32_t i=0;i<box.vertex_count;i++){box.vertices[i].pos[0]*=.01f;box.vertices[i].pos[1]*=.01f;box.vertices[i].pos[2]=.5f;}
    box.bounds_min[0]=box.bounds_min[1]=-.5f;box.bounds_max[0]=box.bounds_max[1]=.5f;
    hta_collision bc={0};CHECK(hta_collision_build(&bc,&box),"box grid builds");
    hta_collision_instance in={.grid=&bc,.rot={1,0,0,0,1,0,0,0,1},.pos={5,0,0},.radius=1,.active=true};
    col.instances=&in;col.instance_count=1;
    float gz=0;
    CHECK(hta_collision_ground(&col,5.1f,0,2,&gz) && fabsf(gz-.5f)<.001f,"instance roof is ground");
    CHECK(hta_collision_ground(&col,0,0,2,&gz) && fabsf(gz)<.001f,"away from instance the floor is ground");
    float o2[3]={5.1f,0,3},dn[3]={0,0,-1};
    CHECK(hta_collision_ray(&col,o2,dn,5,&t,NULL,NULL) && fabsf(t-2.5f)<.001f,"ray finds instance roof");
    in.pos[0]=20;
    CHECK(hta_collision_ray(&col,o2,dn,5,&t,NULL,NULL) && fabsf(t-3)<.001f,"moved instance leaves no ghost");
    /* a rotated instance: 90 degrees about z, offset */
    in.pos[0]=5;in.pos[2]=1;float r90[9]={0,-1,0,1,0,0,0,0,1};memcpy(in.rot,r90,sizeof(r90));
    CHECK(hta_collision_ground(&col,5.2f,.1f,5,&gz) && fabsf(gz-1.5f)<.001f,"raised rotated instance ground");
    col.instances=NULL;col.instance_count=0;
    hta_collision_free(&bc);hta_bsp_free(&box);

    /* A tank pivots in place on its treads. */
    rig(&v);v.cars[0].kind=HTA_VK_TANK;v.cars[0].forward=4.2f;v.cars[0].reverse=3.6f;
    v.cars[0].accel=1.8f;v.cars[0].decel=5.4f;v.cars[0].turn_rate=50*0.0174533f;
    run(&v,&col,0,1,false,60,1.f/60);
    CHECK(v.cars[0].yaw<-.5f && hypotf(v.cars[0].pos[0],v.cars[0].pos[1])<.05f,
        "tank pivots clockwise in place on right stick");
    rig(&v);v.cars[0].kind=HTA_VK_TANK;v.cars[0].forward=4.2f;v.cars[0].reverse=3.6f;
    v.cars[0].accel=1.8f;v.cars[0].decel=5.4f;v.cars[0].turn_rate=50*0.0174533f;
    run(&v,&col,1,0,false,240,1.f/60);
    CHECK(fabsf(v.cars[0].speed-4.2f)<.02f && v.cars[0].pos[0]>10,"tank reaches its tagged speed");

    /* A Ghost turns toward its driver's look and strafes. */
    rig(&v);v.cars[0].kind=HTA_VK_SCOUT;v.cars[0].forward=6.75f;v.cars[0].reverse=3.375f;
    v.cars[0].accel=4.5f;v.cars[0].decel=4.5f;v.cars[0].turn_rate=80*0.0174533f;
    v.cars[0].ctl.yaw=1.0f;
    run(&v,&col,0,0,false,60,1.f/60);
    CHECK(fabsf(v.cars[0].yaw-1.0f)<.01f,"ghost turns to the driver's look");
    run(&v,&col,0,1,false,120,1.f/60);
    { float right[2]={sinf(1.0f),-cosf(1.0f)};
      float moved=v.cars[0].pos[0]*right[0]+v.cars[0].pos[1]*right[1];
      CHECK(moved>3,"ghost strafes right"); }
    CHECK(v.cars[0].grounded,"ghost hovers on its points");

    /* A Banshee flies where its pilot looks, and hovers when let go. */
    rig(&v);v.cars[0].kind=HTA_VK_FIGHTER;v.cars[0].forward=6.6f;v.cars[0].reverse=.9f;
    v.cars[0].accel=7.2f;v.cars[0].decel=14.4f;v.cars[0].turn_rate=80*0.0174533f;
    v.cars[0].ctl.pitch=.5f;
    run(&v,&col,1,0,false,120,1.f/60);
    CHECK(v.cars[0].pos[2]>2 && v.cars[0].pos[0]>4,"banshee climbs along its nose");
    float alt=v.cars[0].pos[2];
    run(&v,&col,0,0,false,120,1.f/60);
    CHECK(fabsf(v.cars[0].pos[2]-alt)<1.5f && fabsf(v.cars[0].fall_speed)<.05f,"banshee hovers when the stick is let go");
    other_driven(&v,0,false);
    run(&v,&col,0,0,false,400,1.f/60);
    CHECK(v.cars[0].grounded && v.cars[0].pos[2]<.5f,"empty banshee comes down and lands");
    hta_collision_free(&col);hta_bsp_free(&m);
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
    static hta_vehicles v;ok=hta_vehicles_load(&v,&c,NULL,err,sizeof(err));
    printf("vehicles: %s\n",err);
    CHECK(ok,"Trial vehicles and their collision meshes load");if(!ok){free(d);return;}
    CHECK(v.count==28 && v.type_count==5,"all 28 Blood Gulch placements in five types");
    uint32_t kinds[8]={0};for(uint32_t i=0;i<v.count;i++)kinds[v.cars[i].kind]++;
    CHECK(kinds[HTA_VK_JEEP]==12 && kinds[HTA_VK_TANK]==6 && kinds[HTA_VK_SCOUT]==8 &&
          kinds[HTA_VK_FIGHTER]==2,"twelve Warthogs, six Scorpions, eight Ghosts, two Banshees");
    int jeep=-1,tank=-1,ghost=-1,banshee=-1;
    for(uint32_t i=0;i<v.count;i++){
        if(v.cars[i].kind==HTA_VK_JEEP && jeep<0 && v.types[v.cars[i].type].name[0]=='m')jeep=(int)i;
        if(v.cars[i].kind==HTA_VK_TANK && tank<0)tank=(int)i;
        if(v.cars[i].kind==HTA_VK_SCOUT && ghost<0)ghost=(int)i;
        if(v.cars[i].kind==HTA_VK_FIGHTER && banshee<0)banshee=(int)i;
    }
    CHECK(jeep>=0 && tank>=0 && ghost>=0 && banshee>=0,"one of each found");
    if(jeep<0 || tank<0 || ghost<0 || banshee<0){hta_vehicles_free(&v);free(d);return;}
    const hta_vehicle *wj=&v.cars[jeep];
    CHECK(fabsf(wj->forward-8.25f)<.001f && fabsf(wj->accel-2.52f)<.001f,
        "real per-tick vehicle values converted to seconds");
    CHECK(fabsf(wj->ground_depth-.15f)<.001f && fabsf(wj->ground_friction-.23f)<.001f &&
          fabsf(wj->mass-5000)<1 && fabsf(wj->yaw_inertia-1990.22f)<.1f,
        "real suspension depth, friction, mass and yaw inertia use distinct phys fields");
    CHECK(wj->seat[1]>0,"driver marker resolves on left side");
    const hta_vehicle_type *wt=&v.types[wj->type],*tt=&v.types[v.cars[tank].type];
    const hta_vehicle_type *gt=&v.types[v.cars[ghost].type],*bt=&v.types[v.cars[banshee].type];
    CHECK(wt->seat_count==3 && !strcmp(wt->seats[0].label,"W-driver") &&
          !strcmp(wt->seats[2].label,"W-gunner") && (wt->seats[2].flags&HTA_SEAT_GUNNER),
        "Warthog: driver, passenger, gunner from the tag");
    CHECK(tt->seat_count==5 && (tt->seats[0].flags&HTA_SEAT_GUNNER) &&
          (tt->seats[1].flags&HTA_SEAT_NEEDS_DRIVER),"Scorpion: gunning driver, four riders who need him");
    CHECK(gt->seat_count==1 && bt->seat_count==1,"Ghost and Banshee: one seat each");
    CHECK(wt->yaw_node==1 && wt->pitch_node==8 && tt->yaw_node==7 && tt->pitch_node==8,
        "turret yaw and pitch nodes found by name");
    CHECK(wt->weapon_tag && tt->weapon_tag && gt->weapon_tag && bt->weapon_tag,"every type carries a weapon");
    CHECK(wt->coll.built && tt->coll.built && gt->coll.built && bt->coll.built,"model-space grids build");
    CHECK(wt->part_count>=7,"Warthog splits into hull, four wheels, mount and gun");
    CHECK(fabsf(v.cars[tank].turn_rate-50*0.0174533f)<.001f &&
          fabsf(v.cars[ghost].forward-6.75f)<.01f && fabsf(v.cars[banshee].forward-6.6f)<.01f,
        "tank turn rate from its seat, Ghost and Banshee speeds from their tags");
    /* rosters */
    hta_vehicles_roster(&v,HTA_VROSTER_DEFAULT);
    uint32_t on=0;for(uint32_t i=0;i<v.count;i++)on+=v.cars[i].active;
    CHECK(on==4,"Slayer default spawns the map's four default Warthogs");
    hta_vehicles_roster(&v,HTA_VROSTER_BANSHEES);
    on=0;for(uint32_t i=0;i<v.count;i++)on+=v.cars[i].active;
    CHECK(on==2,"banshee roster spawns two");
    hta_vehicles_roster(&v,HTA_VROSTER_ALL);
    on=0;for(uint32_t i=0;i<v.count;i++)on+=v.cars[i].active;
    CHECK(on==28,"all spawns every placement");

    hta_bsp_mesh mesh={0};static hta_collision col;
    ok=hta_bsp_load_collision(&c,&mesh,err,sizeof(err));CHECK(ok,"map collision loads");
    if(ok){
        hta_scenario_add_collision_excluding(&mesh,&c,v.skip,sizeof(v.skip),err,sizeof(err));
        hta_collision_build(&col,&mesh);
        col.instances=v.inst;col.instance_count=v.count;
        hta_vehicles_update(&v,&col,3.57f,1.f/60);
        for(int i=0;i<60;i++)hta_vehicles_update(&v,&col,3.57f,1.f/60);
        uint32_t settled=0;for(uint32_t i=0;i<v.count;i++)settled+=v.cars[i].grounded;
        printf("settled %u of %u\n",settled,v.count);
        CHECK(settled>=26,"parked vehicles settle on real terrain");
        int j1=-1;for(uint32_t i=0;i<v.count;i++)if(v.cars[i].placement==1)j1=(int)i;
        CHECK(j1>=0,"placement 1 is a movable Warthog");
        if(j1>=0){
            hta_vehicle *car=&v.cars[j1];
            CHECK(car->rest_time>=HTA_VEHICLE_SETTLE_TIME && car->pitch<-.08f,
                "parked Warthog keeps settling its body angle before sleeping");
            /* Tires touch: the lowest vertex of each wheel part lies on terrain. */
            hta_vehicle_part parts[512];uint32_t np=hta_vehicles_parts(&v,parts,512);
            const hta_vehicle_type *t=&v.types[car->type];
            hta_transform nodes[HTA_VEHICLE_NODES],world;hta_vehicles_nodes(&v,(uint32_t)j1,nodes);
            hta_vehicles_world(&v,(uint32_t)j1,&world);
            bool tires_touch=true,tires_near_body=true;int wheels=0;
            for(uint32_t wi=0;wi<car->point_count;wi++){
                const hta_vehicle_point *pt=&car->points[wi];if(!pt->wheel)continue;
                for(uint32_t p=0;p<t->part_count;p++){
                    if(t->part_node[p]!=(int16_t)pt->node)continue;
                    hta_transform m;hta_xf_mul(&m,&world,&nodes[pt->node]);
                    float low=INFINITY,bx=0,by=0;
                    for(uint32_t sI=t->part_first[p];sI<t->part_first[p]+t->part_submeshes[p];sI++){
                        const hta_submesh *sm=&t->mesh.submeshes[sI];
                        for(uint32_t q=sm->first_index;q<sm->first_index+sm->index_count;q++){
                            float w[3];hta_xf_point(w,&m,t->mesh.vertices[t->mesh.indices[q]].pos);
                            if(w[2]<low){low=w[2];bx=w[0];by=w[1];}
                        }
                    }
                    float gz=0;hta_collision terrain=col;terrain.instances=NULL;terrain.instance_count=0;
                    if(!hta_collision_ground(&terrain,bx,by,low+1,&gz) || fabsf(low-gz)>.03f)tires_touch=false;
                    wheels++;
                }
                if(fabsf(pt->travel)>.12f)tires_near_body=false;
            }
            CHECK(wheels==4 && tires_touch,"resting Warthog tires visually reach real terrain");
            CHECK(tires_near_body,"parked chassis settles near its wheels");
            CHECK(np>=28,"every active vehicle draws at least its hull");
            /* Wheel spin moves the tire node around its mass point. */
            hta_transform before[HTA_VEHICLE_NODES],after[HTA_VEHICLE_NODES];
            uint16_t wn=0;for(uint32_t wi=0;wi<car->point_count;wi++)if(car->points[wi].wheel){wn=car->points[wi].node;break;}
            hta_vehicles_nodes(&v,(uint32_t)j1,before);car->wheel_spin+=1;hta_vehicles_nodes(&v,(uint32_t)j1,after);
            float probe[3]={car->points[0].pos[0],car->points[0].pos[1],0},a[3],b[3];
            for(uint32_t wi=0;wi<car->point_count;wi++)if(car->points[wi].node==wn){probe[0]=car->points[wi].pos[0]+.2f;probe[1]=car->points[wi].pos[1];probe[2]=car->points[wi].pos[2];}
            hta_xf_point(a,&before[wn],probe);hta_xf_point(b,&after[wn],probe);
            CHECK(hypotf(a[0]-b[0],a[2]-b[2])>.1f && fabsf(a[1]-b[1])<.01f,
                "wheel spin turns the tire about its axle");
            car->wheel_spin-=1;

            /* Enter from the owner's screenshot position, drive, brake, exit. */
            float feet[3]={101.75f,-144.8f,.53f};int32_t seat=-1;
            int32_t near=hta_vehicles_near(&v,&col,feet,&seat);
            CHECK(near==j1 && seat==0,"driver seat is reachable beside actual parked Warthog");
            CHECK(hta_vehicles_enter(&v,(uint32_t)j1,0,3) && car->ctl.driven,"driver can enter");
            CHECK(!hta_vehicles_enter(&v,(uint32_t)j1,0,4),"a taken seat refuses a second driver");
            CHECK(!hta_vehicles_enter(&v,(uint32_t)j1,1,3),"one unit cannot sit in two seats");
            float old[3];memcpy(old,car->pos,sizeof(old));
            for(int i=0;i<60;i++){car->ctl.throttle=1;hta_vehicles_update(&v,&col,3.57f,1.f/60);}
            printf("drive displacement %.3f speed %.3f ground %d\n",hypotf(car->pos[0]-old[0],car->pos[1]-old[1]),car->speed,car->grounded);
            CHECK(hypotf(car->pos[0]-old[0],car->pos[1]-old[1])>1,"real Warthog leaves original placement");
            float ray[3]={old[0],old[1],old[2]+2},down[3]={0,0,-1},tt2=0;
            hta_collision terrain=col;terrain.instances=NULL;terrain.instance_count=0;
            float ground_t=0;hta_collision_ray(&terrain,ray,down,5,&ground_t,NULL,NULL);
            CHECK(hta_collision_ray(&col,ray,down,5,&tt2,NULL,NULL) && fabsf(tt2-ground_t)<.05f,
                "no ghost collider at former vehicle position");
            float over[3]={car->pos[0],car->pos[1],car->pos[2]+3};
            CHECK(hta_collision_ray(&col,over,down,5,&tt2,NULL,NULL) && tt2<3.2f,
                "the moved Warthog is solid where it now is");
            for(int i=0;i<60;i++){car->ctl.throttle=0;car->ctl.brake=true;hta_vehicles_update(&v,&col,3.57f,1.f/60);}
            float out[3],oy;
            CHECK(hta_vehicles_exit(&v,&col,(uint32_t)j1,0,.2f,.7f,out,&oy) && car->occupant[0]<0 &&
                  !car->ctl.driven,"real Warthog exits safely after braking");
            /* The gunner turns with the gun. */
            CHECK(hta_vehicles_enter(&v,(uint32_t)j1,2,5),"gunner seat enterable");
            hta_transform g0,g1;hta_vehicles_seat_transform(&v,(uint32_t)j1,2,&g0);
            float t0[3],d0[3],t1[3],d1[3];hta_vehicles_trigger(&v,(uint32_t)j1,0,t0,d0);
            hta_vehicles_aim(&v,(uint32_t)j1,car->yaw+1.2f,0.2f,5.0f);
            hta_vehicles_seat_transform(&v,(uint32_t)j1,2,&g1);hta_vehicles_trigger(&v,(uint32_t)j1,0,t1,d1);
            printf("aim yaw %.3f pitch %.3f car pitch %.3f roll %.3f\n",car->aim_yaw,car->aim_pitch,car->pitch,car->roll);
            CHECK(fabsf(car->aim_yaw-1.2f)<.05f,"turret yaw follows the gunner's world aim");
            CHECK(hypotf(g0.t[0]-g1.t[0],g0.t[1]-g1.t[1])>.02f || fabsf(g0.q[2]-g1.q[2])>.1f,
                "gunner rotates with the mount");
            CHECK(fabsf(atan2f(d1[1],d1[0])-hta_angle_wrap(car->yaw+1.2f))<.05f && d1[2]>.05f,
                "fire direction leaves the barrel where it points");
            hta_vehicles_vacate(&v,5);
            CHECK(car->occupant[2]<0,"vacate frees the gunner");
        }
        /* The Scorpion's passengers need a driver. */
        { int32_t ts=-1;hta_vehicle *tk=&v.cars[tank];
          CHECK(!hta_vehicles_enter(&v,(uint32_t)tank,1,6),"no riding a tank without a driver");
          CHECK(hta_vehicles_enter(&v,(uint32_t)tank,0,7),"tank driver enters");
          CHECK(hta_vehicles_enter(&v,(uint32_t)tank,1,6),"then a rider may climb on");
          float y0=tk->yaw;
          for(int i=0;i<60;i++){tk->ctl.strafe=1;hta_vehicles_update(&v,&col,3.57f,1.f/60);}
          CHECK(fabsf(hta_angle_wrap(tk->yaw-y0))>.3f,"real Scorpion pivots");
          (void)ts; hta_vehicles_vacate(&v,6);hta_vehicles_vacate(&v,7); }
        /* A Ghost and a Banshee on the real map. */
        { hta_vehicle *gh=&v.cars[ghost];
          CHECK(hta_vehicles_enter(&v,(uint32_t)ghost,0,8),"ghost driver enters");
          float p0[3];memcpy(p0,gh->pos,sizeof(p0));
          for(int i=0;i<90;i++){gh->ctl.throttle=1;gh->ctl.yaw=gh->yaw;hta_vehicles_update(&v,&col,3.57f,1.f/60);}
          CHECK(hypotf(gh->pos[0]-p0[0],gh->pos[1]-p0[1])>2,"real Ghost moves off");
          hta_vehicles_vacate(&v,8);
          hta_vehicle *bn=&v.cars[banshee];
          CHECK(hta_vehicles_enter(&v,(uint32_t)banshee,0,9),"banshee pilot enters");
          float z0=bn->pos[2];
          for(int i=0;i<120;i++){bn->ctl.throttle=1;bn->ctl.yaw=bn->yaw;bn->ctl.pitch=.6f;hta_vehicles_update(&v,&col,3.57f,1.f/60);}
          printf("banshee climbed %.2f\n",bn->pos[2]-z0);
          CHECK(bn->pos[2]-z0>2,"real Banshee takes off and climbs");
          float out[3],oy;
          CHECK(hta_vehicles_exit(&v,&col,(uint32_t)banshee,0,.2f,.7f,out,&oy),"a pilot may bail out of a flying Banshee");
          CHECK(out[2]<bn->pos[2],"bailing out drops below the Banshee");
        }
    }
    hta_collision_free(&col);hta_bsp_free(&mesh);hta_vehicles_free(&v);free(d);
}
int main(int argc,char **argv)
{synthetic();if(argc>1)real_map(argv[1]);printf("%d checks, %d failures\n",checks,failures);return failures?1:0;}
