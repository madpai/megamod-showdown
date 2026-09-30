/* Host-owned ordered checkpoint/lap rules for one authored route. */
#ifndef HTA_GAME_RACE_H
#define HTA_GAME_RACE_H
#include <stdbool.h>
#include <stdint.h>
#include "../asset/racing_def.h"

typedef enum { HTA_RACE_READY, HTA_RACE_COUNTDOWN, HTA_RACE_GO,
               HTA_RACE_RESULTS } hta_race_phase;

typedef struct {
    bool active, finished;
    uint8_t lap, next_gate, finish_order;
    float finish_time, progress;
    float wrong_way_time, last_sample_time;
    bool wrong_way;
    float last_pos[3];
} hta_race_entry;

typedef struct {
    hta_race_track track;
    hta_race_entry racer[HTA_RACE_MAX_RACERS];
    uint8_t count, finished_count, finish_order_next;
    hta_race_phase phase;
    float countdown, elapsed, result_wait;
} hta_race;

bool hta_race_track_valid(const hta_race_track *track);
bool hta_race_init(hta_race *race, const hta_race_track *track, uint8_t racers);
void hta_race_start(hta_race *race);
bool hta_race_join(hta_race *race, uint8_t slot);
void hta_race_leave(hta_race *race, uint8_t slot);
void hta_race_step(hta_race *race, float dt);
bool hta_race_advance(hta_race *race, uint8_t racer, const float new_pos[3]);
unsigned hta_race_position(const hta_race *race, uint8_t racer);
bool hta_race_recovery(const hta_race *race, uint8_t racer, float pos[3], float *yaw);

#endif
