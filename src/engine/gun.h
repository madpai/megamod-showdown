/* Offline single-player gun: hitscan + impact marks. Not Halo's weapon system. */
#ifndef HTA_GUN_H
#define HTA_GUN_H

#include "player.h"
#include "camera.h"
#include "../asset/bsp.h"

#define HTA_GUN_MAX_HITS 48
#define HTA_GUN_COOLDOWN 0.14f
#define HTA_GUN_RANGE    180.0f

typedef struct {
    float pos[3];
    float nrm[3];
} hta_hitmark;

typedef struct {
    hta_hitmark hits[HTA_GUN_MAX_HITS];
    uint32_t    n, next;
    float       cooldown;
    int         dirty;
    hta_bsp_mesh mesh;
} hta_gun;

void hta_gun_init(hta_gun *g);
void hta_gun_free(hta_gun *g);
/* Returns 1 if a shot fired (and possibly hit). */
int  hta_gun_fire(hta_gun *g, const hta_collision *col, const hta_camera *cam);
void hta_gun_update(hta_gun *g, float dt);
/* Rebuilds g->mesh from impact marks. Call when dirty before GPU upload. */
void hta_gun_build_mesh(hta_gun *g);

#endif
