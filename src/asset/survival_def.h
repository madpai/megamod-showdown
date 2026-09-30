#ifndef MM_SURVIVAL_DEF_H
#define MM_SURVIVAL_DEF_H
#include <stdbool.h>
#include <stdint.h>
#include "../engine/progression.h"
#define MM_GATES 8
#define MM_ENEMIES 8
#define MM_SHOP_ITEMS 32
enum { MM_ITEM_UPGRADE, MM_ITEM_WEAPON, MM_ITEM_ARMOR, MM_ITEM_SPELL, MM_ITEM_HEALTH, MM_ITEM_MANA };
enum { MM_EFFECT_DAMAGE, MM_EFFECT_FROST, MM_EFFECT_CHAIN, MM_EFFECT_HEAL, MM_EFFECT_WARD };
typedef struct {
    char id[MM_ITEM_ID],name[64],asset[64];
    uint32_t price;
    uint8_t kind,upgrade,effect;
    float power,range,radius,duration,cost;
} mm_shop_item;
typedef struct {
    char name[64],character[64],weapon[64];
    float health,speed,damage;
    uint32_t gold;
} mm_enemy_def;
typedef struct {
    float shop[3],gates[MM_GATES][3];
    float rest_seconds,spawn_interval;
    uint32_t base_enemies,wave_enemies;
    uint8_t gate_count,enemy_count,item_count;
    mm_enemy_def enemy[MM_ENEMIES];
    mm_shop_item item[MM_SHOP_ITEMS];
} mm_survival_def;
bool mm_survival_valid(const mm_survival_def *d);
#endif
