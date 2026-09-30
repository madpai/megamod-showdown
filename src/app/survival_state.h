#ifndef MM_SURVIVAL_STATE_H
#define MM_SURVIVAL_STATE_H
#include "../engine/progression.h"
#include <stdbool.h>
#include <stdint.h>
enum { MM_REST,MM_WAVE,MM_WIPE };
enum { MM_REQUEST_BUY=1,MM_REQUEST_EQUIP,MM_REQUEST_USE,MM_REQUEST_PRESTIGE };
enum { MM_RESULT_OK=1,MM_RESULT_UNAVAILABLE,MM_RESULT_GOLD,MM_RESULT_OWNED,MM_RESULT_SAVE };
typedef struct {
    bool active,bound[16],dirty[16],ground[16],jumped[16];
    char directory[512],key[16][33],error[160];
    mm_progression profile[16];
    float timer,spawn_timer,save_timer,pos[16][3],practice[16];
    uint32_t wave,queued,spawned;
    uint8_t phase,enemy_type[16],result[16];
    uint16_t request_seen[16];
} mm_survival;
#endif
