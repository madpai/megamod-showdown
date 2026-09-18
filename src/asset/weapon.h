/* First-person weapon from Trial `weap` tags. */
#ifndef HTA_WEAPON_H
#define HTA_WEAPON_H

#include "cache.h"
#include "bsp.h"
#include "bitmap.h"

#define HTA_WEAP_FP_MODEL  0x45Cu  /* TagDependency to fp mod2 */
#define HTA_WEAP_TRIGGERS  0x4FCu
#define HTA_TRIG_SIZE      276u
#define HTA_TRIG_ROF       4u      /* two floats: initial, final shots/sec */

typedef struct {
    uint32_t fp_model_id;
    float    rof;          /* shots per second (final) */
    float    cooldown;     /* 1/rof */
    float    fp_offset[3]; /* X forward, Y left, Z up; may be zero */
    char     path[96];
} hta_weapon_def;

/* Prefers assault rifle, then pistol. Loads FP mesh into `fp` if bitmaps given. */
bool hta_weapon_load_default(const hta_cache *c, const hta_resource_map *bitmaps,
                             hta_weapon_def *def, hta_bsp_mesh *fp,
                             char *err, size_t errlen);

#endif
