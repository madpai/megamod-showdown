#include "progression.h"
#include <math.h>
#include <string.h>
#include <limits.h>

static uint64_t add(uint64_t a, uint64_t b) { return UINT64_MAX-a<b ? UINT64_MAX : a+b; }
static uint64_t mul(uint64_t a, uint64_t b) { return b && a>UINT64_MAX/b ? UINT64_MAX : a*b; }
/* Integer square root: rank boundaries are identical on every platform,
 * including XP beyond floating point's exact integer range. */
static uint32_t root(uint64_t n)
{
    uint64_t r=0, bit=UINT64_C(1)<<62;
    while (bit>n) bit>>=2;
    while (bit) {
        if (n>=r+bit) { n-=r+bit; r=(r>>1)+bit; }
        else r>>=1;
        bit>>=2;
    }
    return (uint32_t)r;
}
const char *mm_skill_name(unsigned skill)
{
    static const char *const names[]={"Athletics","Acrobatics","Unarmed","Blade",
        "Marksmanship","Destruction","Restoration","Defense"};
    return skill<MM_SKILLS ? names[skill] : "Unknown";
}
void mm_progression_init(mm_progression *p)
{
    if (!p) return;
    memset(p,0,sizeof(*p)); p->gold=100;
}
uint32_t mm_skill_rank(uint64_t experience) { return root(experience/100); }
uint32_t mm_character_level(const mm_progression *p)
{ return p ? root(p->experience/1000)+1u : 1u; }
void mm_practice(mm_progression *p, unsigned skill, uint64_t amount)
{
    if (!p || skill>=MM_SKILLS || !amount) return;
    /* Prestige improves XP with diminishing returns. Skill XP remains
     * integer and unlimited up to the explicit uint64 storage range. */
    uint64_t bonus=mul(amount/20,root(p->prestige));
    amount=add(amount,bonus);
    p->skill[skill]=add(p->skill[skill],amount);
    p->experience=add(p->experience,amount);
}
void mm_award_gold(mm_progression *p, uint64_t amount)
{
    if (!p) return;
    p->gold=add(p->gold,amount); p->earned_gold=add(p->earned_gold,amount);
}
bool mm_spend_gold(mm_progression *p, uint64_t amount)
{ if (!p || amount>p->gold) return false; p->gold-=amount; return true; }
bool mm_item_id_valid(const char *id)
{
    if (!id || !id[0]) return false;
    for (unsigned n=0;n<MM_ITEM_ID;n++) {
        unsigned char c=(unsigned char)id[n];
        if (!c) return n>0;
        if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c==':'||c=='/'||c=='.')) return false;
    }
    return false;
}
static int item(const mm_progression *p, const char *id)
{
    if (!p || !mm_item_id_valid(id) || p->count>MM_INVENTORY) return -1;
    for (unsigned i=0;i<p->count;i++) if (!strcmp(p->inventory[i].id,id)) return (int)i;
    return -1;
}
uint32_t mm_item_quantity(const mm_progression *p, const char *id)
{ int i=item(p,id); return i<0 ? 0 : p->inventory[i].quantity; }
bool mm_item_add(mm_progression *p, const char *id, uint32_t quantity)
{
    if (!p || !mm_item_id_valid(id) || !quantity || p->count>MM_INVENTORY) return false;
    int i=item(p,id);
    if (i<0) {
        if (p->count==MM_INVENTORY) return false;
        i=p->count++; strcpy(p->inventory[i].id,id); p->inventory[i].quantity=0;
    }
    if (UINT32_MAX-p->inventory[i].quantity<quantity) return false;
    p->inventory[i].quantity+=quantity; return true;
}
bool mm_item_take(mm_progression *p, const char *id, uint32_t quantity)
{
    int i=item(p,id);
    if (i<0 || !quantity || p->inventory[i].quantity<quantity) return false;
    p->inventory[i].quantity-=quantity;
    if (!p->inventory[i].quantity) {
        memmove(p->inventory+i,p->inventory+i+1,(p->count-(unsigned)i-1)*sizeof(p->inventory[0]));
        memset(&p->inventory[--p->count],0,sizeof(p->inventory[0]));
    }
    return true;
}
uint64_t mm_upgrade_price(unsigned upgrade, uint32_t tier)
{
    static const uint32_t base[]={150,100,125,200,250};
    if (upgrade>=MM_UPGRADES || tier==UINT32_MAX) return UINT64_MAX;
    uint64_t t=(uint64_t)tier+1;
    return mul(base[upgrade],mul(t,t));
}
bool mm_buy_upgrade(mm_progression *p, unsigned upgrade)
{
    if (!p || upgrade>=MM_UPGRADES || p->upgrade[upgrade]==UINT32_MAX) return false;
    uint64_t price=mm_upgrade_price(upgrade,p->upgrade[upgrade]);
    if (price==UINT64_MAX || !mm_spend_gold(p,price)) return false;
    p->upgrade[upgrade]++; return true;
}
bool mm_prestige(mm_progression *p)
{
    if (!p || mm_character_level(p)<50 || p->prestige==UINT32_MAX) return false;
    p->prestige++; p->experience=0; return true; /* keeps skills, items, gold */
}
float mm_skill_bonus(const mm_progression *p, unsigned skill)
{
    float r=p && skill<MM_SKILLS ? (float)mm_skill_rank(p->skill[skill]) : 0;
    return r/(r+100.0f);
}
float mm_health_bonus(const mm_progression *p)
{ return p ? 1.0f+0.08f*sqrtf((float)p->upgrade[MM_HEALTH])+0.025f*sqrtf((float)(mm_character_level(p)-1)) : 1; }
float mm_power_bonus(const mm_progression *p)
{ return p ? 1.0f+0.08f*sqrtf((float)p->upgrade[MM_POWER]) : 1; }
