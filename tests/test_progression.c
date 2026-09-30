#define _POSIX_C_SOURCE 200809L
#include "engine/progression.h"
#include "app/progression_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
int main(void) {
    mm_progression p;mm_progression_init(&p);assert(p.gold==100 && mm_character_level(&p)==1);
    assert(mm_skill_rank(99)==0 && mm_skill_rank(100)==1 && mm_skill_rank(399)==1 && mm_skill_rank(400)==2);
    assert(mm_skill_rank(UINT64_MAX)==429496729);
    mm_practice(&p,MM_BLADE,2500);assert(mm_skill_rank(p.skill[MM_BLADE])==5);
    p.prestige=UINT32_MAX;mm_practice(&p,MM_BLADE,UINT64_MAX);assert(p.skill[MM_BLADE]==UINT64_MAX && p.experience==UINT64_MAX);
    assert(mm_upgrade_price(MM_HEALTH,UINT32_MAX-1)==UINT64_MAX);
    assert(mm_item_add(&p,"test:item/blade",UINT32_MAX));assert(!mm_item_add(&p,"test:item/blade",1));
    assert(mm_item_quantity(&p,"test:item/blade")==UINT32_MAX);
    assert(!mm_spend_gold(&p,101));assert(p.gold==100);
    p.prestige=0;p.experience=UINT64_C(49)*49*1000;p.gold=500;strcpy(p.equipment[0],"test:item/blade");
    assert(mm_prestige(&p) && p.experience==0 && p.prestige==1 && p.gold==500 && p.skill[MM_BLADE]==UINT64_MAX);
    assert(!mm_prestige(&p));assert(mm_buy_upgrade(&p,MM_HEALTH));assert(p.gold==350 && p.upgrade[MM_HEALTH]==1);
    char dir[]="/tmp/megamod-progression-XXXXXX",err[160],file[1024];assert(mkdtemp(dir));
    mm_progression q={0};assert(mm_profile_load(dir,"solo",&q,err,sizeof(err))==MM_SAVE_MISSING);
    assert(mm_profile_save(dir,"solo",&p,err,sizeof(err)));assert(mm_profile_load(dir,"solo",&q,err,sizeof(err))==MM_SAVE_OK);
    assert(!memcmp(&p,&q,sizeof(p)));assert(!mm_profile_save(dir,"../bad",&p,err,sizeof(err)));
    uint8_t identity[16],again[16];assert(mm_profile_identity(dir,identity,err,sizeof(err)));assert(mm_profile_identity(dir,again,err,sizeof(err)));assert(!memcmp(identity,again,16));
    snprintf(file,sizeof(file),"%s/solo.mrp",dir);FILE *f=fopen(file,"r+b");assert(f);assert(fseek(f,18,SEEK_SET)==0);int v=fgetc(f);assert(v!=EOF);assert(fseek(f,18,SEEK_SET)==0);fputc(v^1,f);fclose(f);
    mm_progression untouched=q;assert(mm_profile_load(dir,"solo",&q,err,sizeof(err))==MM_SAVE_CORRUPT);assert(!memcmp(&q,&untouched,sizeof(q)));
    unlink(file);snprintf(file,sizeof(file),"%s/identity",dir);unlink(file);rmdir(dir);
    puts("progression: integer ranks, overflow, prestige, inventory, atomic save roundtrip and corruption passed");
}
