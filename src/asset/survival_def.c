#include "survival_def.h"
#include <math.h>
#include <string.h>
static bool bound(float f,float lo,float hi) { return isfinite(f) && f>=lo && f<=hi; }
static bool vec(const float v[3]) { return bound(v[0],-4096,4096)&&bound(v[1],-4096,4096)&&bound(v[2],-4096,4096); }
bool mm_survival_valid(const mm_survival_def *d)
{
    if (!d || !vec(d->shop) || !d->gate_count || d->gate_count>MM_GATES ||
        !d->enemy_count || d->enemy_count>MM_ENEMIES || !d->item_count || d->item_count>MM_SHOP_ITEMS ||
        !bound(d->rest_seconds,5,300) || !bound(d->spawn_interval,.1f,10) ||
        !d->base_enemies || d->base_enemies>1000 || !d->wave_enemies || d->wave_enemies>1000) return false;
    for(unsigned i=0;i<d->gate_count;i++) if(!vec(d->gates[i])) return false;
    for(unsigned i=0;i<d->enemy_count;i++) {
        const mm_enemy_def *e=&d->enemy[i];
        if(!memchr(e->name,0,64)||!e->name[0]||!memchr(e->character,0,64)||!memchr(e->weapon,0,64)||
           !bound(e->health,.01f,100)||!bound(e->speed,.1f,5)||!bound(e->damage,.01f,100)||!e->gold) return false;
    }
    for(unsigned i=0;i<d->item_count;i++) {
        const mm_shop_item *s=&d->item[i];
        if(!mm_item_id_valid(s->id)||!memchr(s->name,0,64)||!s->name[0]||!memchr(s->asset,0,64)||
           s->kind>MM_ITEM_MANA||s->upgrade>=MM_UPGRADES||s->effect>MM_EFFECT_WARD||
           !bound(s->power,0,10000)||!bound(s->range,0,200)||!bound(s->radius,0,50)||
           !bound(s->duration,0,60)||!bound(s->cost,0,1000)||
           (s->kind==MM_ITEM_ARMOR && s->power>.75f)) return false;
        for(unsigned j=0;j<i;j++) if(!strcmp(s->id,d->item[j].id)) return false;
    }
    return true;
}
