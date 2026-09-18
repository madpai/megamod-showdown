#include "weapon.h"
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float rdf(const hta_cache *c, uint32_t off)
{
    float f = 0.0f; hta_rd_f32(c, off, &f); return f;
}

bool hta_weapon_load_default(const hta_cache *c, const hta_resource_map *bitmaps,
                             hta_weapon_def *def, hta_bsp_mesh *fp,
                             char *err, size_t errlen)
{
    if (!c || !def) return false;
    memset(def, 0, sizeof(*def));
    def->rof = 3.5f;
    def->cooldown = 1.0f / 3.5f;
    def->fp_offset[0] = 0.18f;
    def->fp_offset[1] = 0.08f;
    def->fp_offset[2] = -0.12f;

    int32_t best = -1, pistol = -1, any = -1;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.primary_class != HTA_TAG_WEAP) continue;
        char path[96];
        hta_cache_tag_path(c, &t, path, sizeof(path));
        any = (int32_t)i;
        if (strstr(path, "assault rifle") && !strstr(path, "ammo")) best = (int32_t)i;
        if (strstr(path, "weapons\\pistol\\pistol") || strstr(path, "weapons/pistol/pistol"))
            pistol = (int32_t)i;
    }
    if (best < 0) best = pistol;
    if (best < 0) best = any;
    if (best < 0) { if (err) snprintf(err, errlen, "no weap tags"); return false; }

    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)best, &t)) return false;
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return false;
    hta_cache_tag_path(c, &t, def->path, sizeof(def->path));
    hta_rd_u32(c, off + HTA_WEAP_FP_MODEL + 12, &def->fp_model_id);

    uint32_t n = 0, ptr = 0;
    if (hta_read_reflexive(c, off + HTA_WEAP_TRIGGERS, &n, &ptr) && n >= 1) {
        uint32_t arr;
        if (hta_cache_ptr_to_offset(c, ptr, &arr)) {
            float a = rdf(c, arr + HTA_TRIG_ROF);
            float b = rdf(c, arr + HTA_TRIG_ROF + 4);
            float rof = b > 0.1f ? b : a;
            if (rof > 0.1f && rof < 40.0f) {
                def->rof = rof;
                def->cooldown = 1.0f / rof;
            }
        }
    }

    if (fp) {
        memset(fp, 0, sizeof(*fp));
        fp->textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
        if (fp->textures && def->fp_model_id) {
            float z[3] = {0,0,0};
            if (!hta_model_instance(fp, c, bitmaps, def->fp_model_id, z, z, err, errlen)) {
                hta_bsp_free(fp);
                memset(fp, 0, sizeof(*fp));
            }
        }
    }
    return true;
}
