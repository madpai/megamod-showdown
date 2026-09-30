#define _POSIX_C_SOURCE 200809L
#include "progression_store.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#define SAVE_MAX 8192u
static bool error(char *err,size_t n,const char *message)
{ if (err && n) snprintf(err,n,"%s",message); return false; }
static bool key_valid(const char *id)
{
    if (!id) return false;
    if (!strcmp(id,"solo")) return true;
    if (strlen(id)!=32) return false;
    for (unsigned i=0;i<32;i++) if (!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f'))) return false;
    return true;
}
static bool path(char out[1024],const char *dir,const char *key)
{ return dir && *dir && key_valid(key) && snprintf(out,1024,"%s/%s.mrp",dir,key)<1024; }
static bool directory(const char *dir)
{
    if (!dir || !*dir || strlen(dir)>=1024) return false;
    char copy[1024]; strcpy(copy,dir);
    for (char *p=copy+1;;p++) if (*p=='/' || !*p) {
        char c=*p; *p=0;
        if (mkdir(copy,0700) && errno!=EEXIST) return false;
        struct stat st; if (stat(copy,&st) || !S_ISDIR(st.st_mode)) return false;
        *p=c; if (!c) break;
    }
    return true;
}
static uint32_t crc(const uint8_t *s,size_t n)
{
    uint32_t v=0xffffffff;
    for (size_t i=0;i<n;i++) {
        v^=s[i]; for (unsigned k=0;k<8;k++) v=(v>>1)^((0u-(v&1u))&0xedb88320u);
    }
    return v^0xffffffff;
}
static void put(uint8_t *d,uint64_t v,unsigned n)
{ for (unsigned k=0;k<n;k++) { d[k]=(uint8_t)v; v>>=8; } }
static uint64_t get(const uint8_t *d,unsigned n)
{ uint64_t v=0; for (unsigned k=0;k<n;k++) v|=(uint64_t)d[k]<<(8*k); return v; }
static bool valid(const mm_progression *p)
{
    if (!p || p->count>MM_INVENTORY) return false;
    for (unsigned i=0;i<p->count;i++) {
        if (!mm_item_id_valid(p->inventory[i].id) || !p->inventory[i].quantity) return false;
        for (unsigned j=0;j<i;j++) if (!strcmp(p->inventory[i].id,p->inventory[j].id)) return false;
    }
    for (unsigned i=0;i<4;i++) if (p->equipment[i][0] &&
        (!mm_item_id_valid(p->equipment[i]) || !mm_item_quantity(p,p->equipment[i]))) return false;
    return true;
}
static size_t encode(const mm_progression *p,uint8_t out[SAVE_MAX])
{
    if (!valid(p)) return 0;
    memset(out,0,SAVE_MAX); memcpy(out,"MPRG",4); put(out+4,1,4);
    size_t at=16;
    const uint64_t numbers[]={p->gold,p->experience,p->earned_gold,p->kills,p->waves};
    for (unsigned i=0;i<5;i++,at+=8) put(out+at,numbers[i],8);
    for (unsigned i=0;i<MM_SKILLS;i++,at+=8) put(out+at,p->skill[i],8);
    for (unsigned i=0;i<MM_UPGRADES;i++,at+=4) put(out+at,p->upgrade[i],4);
    put(out+at,p->prestige,4); at+=4; out[at++]=p->count;
    for (unsigned i=0;i<4;i++,at+=MM_ITEM_ID) memcpy(out+at,p->equipment[i],strlen(p->equipment[i]));
    for (unsigned i=0;i<p->count;i++) {
        memcpy(out+at,p->inventory[i].id,strlen(p->inventory[i].id)); at+=MM_ITEM_ID;
        put(out+at,p->inventory[i].quantity,4); at+=4;
    }
    put(out+8,at,4); put(out+12,crc(out+16,at-16),4); return at;
}
static bool decode(const uint8_t *data,size_t size,mm_progression *p)
{
    const size_t base=16+40+MM_SKILLS*8+MM_UPGRADES*4+4+1+4*MM_ITEM_ID;
    if (size<base || size>SAVE_MAX || memcmp(data,"MPRG",4) || get(data+4,4)!=1 ||
        get(data+8,4)!=size || get(data+12,4)!=crc(data+16,size-16)) return false;
    mm_progression q={0}; size_t at=16;
    uint64_t *numbers[]={&q.gold,&q.experience,&q.earned_gold,&q.kills,&q.waves};
    for (unsigned i=0;i<5;i++,at+=8) *numbers[i]=get(data+at,8);
    for (unsigned i=0;i<MM_SKILLS;i++,at+=8) q.skill[i]=get(data+at,8);
    for (unsigned i=0;i<MM_UPGRADES;i++,at+=4) q.upgrade[i]=(uint32_t)get(data+at,4);
    q.prestige=(uint32_t)get(data+at,4); at+=4; q.count=data[at++];
    if (q.count>MM_INVENTORY || size!=base+q.count*(MM_ITEM_ID+4)) return false;
    for (unsigned i=0;i<4;i++,at+=MM_ITEM_ID) {
        memcpy(q.equipment[i],data+at,MM_ITEM_ID);
        if (!memchr(q.equipment[i],0,MM_ITEM_ID)) return false;
    }
    for (unsigned i=0;i<q.count;i++) {
        memcpy(q.inventory[i].id,data+at,MM_ITEM_ID); at+=MM_ITEM_ID;
        if (!memchr(q.inventory[i].id,0,MM_ITEM_ID)) return false;
        q.inventory[i].quantity=(uint32_t)get(data+at,4); at+=4;
    }
    if (!valid(&q)) return false;
    *p=q; return true;
}
mm_save_result mm_profile_load(const char *dir,const char *identity,mm_progression *p,char *err,size_t n)
{
    char file[1024];
    if (!p || !path(file,dir,identity)) { error(err,n,"invalid profile location"); return MM_SAVE_IO; }
    FILE *f=fopen(file,"rb");
    if (!f) {
        if (errno==ENOENT) return MM_SAVE_MISSING;
        error(err,n,"could not open progression save"); return MM_SAVE_IO;
    }
    uint8_t data[SAVE_MAX+1]; size_t size=fread(data,1,sizeof(data),f); bool ok=!ferror(f);
    fclose(f);
    if (!ok) { error(err,n,"could not read progression save"); return MM_SAVE_IO; }
    if (!decode(data,size,p)) {
        error(err,n,"progression save is corrupt or unsupported; original preserved"); return MM_SAVE_CORRUPT;
    }
    return MM_SAVE_OK;
}
static bool atomic_write(const char *dir,const char *file,const uint8_t *data,size_t size,bool exclusive)
{
    if (!directory(dir)) return false;
    char tmp[1040]; if (snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",file)>=(int)sizeof(tmp)) return false;
    int fd=mkstemp(tmp); if (fd<0) return false;
    size_t done=0;
    while (done<size) {
        ssize_t w=write(fd,data+done,size-done);
        if (w<0 && errno==EINTR) continue;
        if (w<=0) break;
        done+=(size_t)w;
    }
    bool ok=done==size && fsync(fd)==0;
    if (close(fd)) ok=false;
    if(ok && exclusive) {ok=link(tmp,file)==0;int saved_errno=errno;unlink(tmp);errno=saved_errno;}
    else if (ok) ok=rename(tmp,file)==0;
    if (!ok) {int saved_errno=errno;unlink(tmp);errno=saved_errno;}
    if (ok) {
        int d=open(dir,O_RDONLY|O_DIRECTORY);
        if (d>=0) { (void)fsync(d); close(d); }
    }
    return ok;
}
bool mm_profile_save(const char *dir,const char *identity,const mm_progression *p,char *err,size_t n)
{
    char file[1024]; uint8_t data[SAVE_MAX]; size_t size=encode(p,data);
    if (!path(file,dir,identity) || !size) return error(err,n,"invalid progression save");
    if (!atomic_write(dir,file,data,size,false)) return error(err,n,"could not commit progression save");
    return true;
}
void mm_identity_hex(const uint8_t identity[16],char out[33])
{
    static const char hex[]="0123456789abcdef";
    for (unsigned i=0;i<16;i++) { out[2*i]=hex[identity[i]>>4]; out[2*i+1]=hex[identity[i]&15]; }
    out[32]=0;
}
bool mm_profile_identity(const char *dir,uint8_t identity[16],char *err,size_t n)
{
    if (!dir || !*dir || !identity) return error(err,n,"no writable identity directory");
    char file[1024]; if (snprintf(file,sizeof(file),"%s/identity",dir)>=(int)sizeof(file)) return false;
    FILE *f=fopen(file,"rb");
    if (f) {
        uint8_t data[17]; size_t got=fread(data,1,sizeof(data),f); bool ok=!ferror(f); fclose(f);
        if (!ok || got!=16) return error(err,n,"invalid player identity; original preserved");
        memcpy(identity,data,16); return true;
    }
    if (errno!=ENOENT) return error(err,n,"could not read player identity");
    int random=open("/dev/urandom",O_RDONLY); if (random<0) return error(err,n,"no player identity entropy");
    size_t got=0;
    while (got<16) {
        ssize_t read_n=read(random,identity+got,16-got);
        if (read_n<0 && errno==EINTR) continue;
        if (read_n<=0) break;
        got+=(size_t)read_n;
    }
    close(random);
    if(got!=16)return error(err,n,"could not create persistent player identity");
    if(!atomic_write(dir,file,identity,16,true)) {
        if(errno==EEXIST)return mm_profile_identity(dir,identity,err,n);
        return error(err,n,"could not create persistent player identity");
    }
    return true;
}
