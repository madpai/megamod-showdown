/* Reusable use-based progression. No game tags, roster indices or file IO. */
#ifndef MEGAMOD_PROGRESSION_H
#define MEGAMOD_PROGRESSION_H
#include <stdbool.h>
#include <stdint.h>
#define MM_SKILLS 8u
#define MM_UPGRADES 5u
#define MM_INVENTORY 64u
#define MM_ITEM_ID 64u
typedef enum { MM_ATHLETICS, MM_ACROBATICS, MM_UNARMED, MM_BLADE,
               MM_MARKSMANSHIP, MM_DESTRUCTION, MM_RESTORATION, MM_DEFENSE } mm_skill;
typedef enum { MM_HEALTH, MM_STAMINA, MM_MAGICKA, MM_POWER, MM_FORTUNE } mm_upgrade;
typedef struct { char id[MM_ITEM_ID]; uint32_t quantity; } mm_owned_item;
typedef struct {
    uint64_t gold, experience, earned_gold, kills, waves;
    uint64_t skill[MM_SKILLS];
    uint32_t upgrade[MM_UPGRADES], prestige;
    mm_owned_item inventory[MM_INVENTORY];
    char equipment[4][MM_ITEM_ID]; /* two weapons, armor, active spell */
    uint8_t count;
} mm_progression;
const char *mm_skill_name(unsigned skill);
void mm_progression_init(mm_progression *p);
uint32_t mm_skill_rank(uint64_t experience);
uint32_t mm_character_level(const mm_progression *p);
void mm_practice(mm_progression *p, unsigned skill, uint64_t amount);
void mm_award_gold(mm_progression *p, uint64_t amount);
bool mm_spend_gold(mm_progression *p, uint64_t amount);
bool mm_item_id_valid(const char *id);
uint32_t mm_item_quantity(const mm_progression *p, const char *id);
bool mm_item_add(mm_progression *p, const char *id, uint32_t quantity);
bool mm_item_take(mm_progression *p, const char *id, uint32_t quantity);
uint64_t mm_upgrade_price(unsigned upgrade, uint32_t tier);
bool mm_buy_upgrade(mm_progression *p, unsigned upgrade);
bool mm_prestige(mm_progression *p);
float mm_skill_bonus(const mm_progression *p, unsigned skill);
float mm_health_bonus(const mm_progression *p);
float mm_power_bonus(const mm_progression *p);
#endif
