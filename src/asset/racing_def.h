/* Bounded, original-world racing configuration. Parsed once from the
 * world_entities schema 8 member and included in the world key. */
#ifndef HTA_RACING_DEF_H
#define HTA_RACING_DEF_H
#include <stdint.h>
#include "../engine/arcade_racer.h"

#define HTA_RACE_MAX_RACERS 8u
#define HTA_RACE_MAX_CHECKPOINTS 32u
#define HTA_RACE_MAX_PADS 16u

typedef struct {
    char id[97];
    float pos[3], forward[2];
    float half_width, half_height;
    float recovery[3], recovery_yaw;
} hta_race_gate;

typedef struct {
    float pos[3], radius;
} hta_race_pad;

typedef struct {
    uint8_t gate_count, lap_count, grid_count, pad_count;
    uint16_t vehicle_model; /* package asset table index + 1 */
    hta_race_gate gate[HTA_RACE_MAX_CHECKPOINTS];
    hta_race_pad pad[HTA_RACE_MAX_PADS];
    float grid[HTA_RACE_MAX_RACERS][3], grid_yaw[HTA_RACE_MAX_RACERS];
    hta_arcade_tuning vehicle;
} hta_race_track;

#endif
