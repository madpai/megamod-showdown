/* Open Asset Lab .oalmap v1 runtime adapter. Caller owns mesh and spawn array. */
#ifndef HTA_EXTERNAL_MAP_H
#define HTA_EXTERNAL_MAP_H
#include "bsp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    hta_bsp_mesh mesh;
    hta_spawn_point *spawns;
    uint32_t spawn_count;
} hta_external_map;

bool hta_external_map_load(const char *path, hta_external_map *out, char *err, size_t errlen);
void hta_external_map_free(hta_external_map *m);
#endif
