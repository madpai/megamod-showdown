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
    uint32_t key;          /* FNV-1a of the manifest: tells two packages apart */
} hta_external_map;

/* Spawn team_index: Source terrorists play red, counter-terrorists blue;
 * any other start is shared by both teams. */
#define HTA_EXTERNAL_TEAM_ANY 0xFFFFu

bool hta_external_map_load(const char *path, hta_external_map *out, char *err, size_t errlen);
/* The same from bytes already in memory (an mmapped APK asset). Copies
 * what it keeps; `data` may be unmapped afterwards. */
bool hta_external_map_load_memory(const uint8_t *data, size_t size, hta_external_map *out,
                                  char *err, size_t errlen);
void hta_external_map_free(hta_external_map *m);
#endif
