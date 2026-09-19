#include "weapon.h"
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float rdf(const hta_cache *c, uint32_t off)
{
    float f = 0.0f; hta_rd_f32(c, off, &f); return f;
}

static int16_t rdi16(const hta_cache *c, uint32_t off)
{
    uint16_t v = 0; hta_rd_u16(c, off, &v); return (int16_t)v;
}

/* TagDependency.tag_id sits 12 bytes into the 16-byte dependency. */
static uint32_t rddep(const hta_cache *c, uint32_t off)
{
    uint32_t id = 0;
    hta_rd_u32(c, off + 12u, &id);
    return id == 0xFFFFFFFFu ? 0u : id;
}

uint32_t hta_globals_fp_hands(const hta_cache *c)
{
    if (!c) return 0;
    int32_t gi = hta_cache_find_tag_by_class(c, HTA_TAG_MATG);
    if (gi < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)gi, &t)) return 0;
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;
    uint32_t n = 0, ptr = 0, arr = 0;
    if (!hta_read_reflexive(c, off + HTA_MATG_FP_INTERFACE, &n, &ptr) || n == 0) return 0;
    if (!hta_cache_ptr_to_offset(c, ptr, &arr)) return 0;
    return rddep(c, arr + HTA_FPI_HANDS);
}

static void load_magazine(const hta_cache *c, uint32_t weap, hta_weapon_def *def)
{
    uint32_t n = 0, ptr = 0, arr = 0;
    if (!hta_read_reflexive(c, weap + HTA_WEAP_MAGAZINES, &n, &ptr) || n == 0) return;
    if (!hta_cache_ptr_to_offset(c, ptr, &arr)) return;
    def->rounds_initial     = rdi16(c, arr + HTA_MAG_ROUNDS_INITIAL);
    def->rounds_reserve_max = rdi16(c, arr + HTA_MAG_ROUNDS_RESERVE);
    def->rounds_loaded_max  = rdi16(c, arr + HTA_MAG_ROUNDS_LOADED);
    def->rounds_reloaded    = rdi16(c, arr + HTA_MAG_ROUNDS_RELOADED);
    def->reload_time        = rdf(c, arr + HTA_MAG_RELOAD_TIME);
    def->chamber_time       = rdf(c, arr + HTA_MAG_CHAMBER_TIME);
    def->reloading_fx_id    = rddep(c, arr + HTA_MAG_RELOADING_FX);
}

static void load_trigger(const hta_cache *c, uint32_t weap, hta_weapon_def *def)
{
    uint32_t n = 0, ptr = 0, arr = 0;
    if (!hta_read_reflexive(c, weap + HTA_WEAP_TRIGGERS, &n, &ptr) || n == 0) return;
    if (!hta_cache_ptr_to_offset(c, ptr, &arr)) return;

    float a = rdf(c, arr + HTA_TRIG_ROF);
    float b = rdf(c, arr + HTA_TRIG_ROF + 4u);
    float rof = b > 0.1f ? b : a;
    if (rof > 0.1f && rof < 40.0f) {
        def->rof = rof;
        def->cooldown = 1.0f / rof;
    }
    def->rounds_per_shot      = rdi16(c, arr + HTA_TRIG_ROUNDS_SHOT);
    def->projectiles_per_shot = rdi16(c, arr + HTA_TRIG_PROJ_SHOT);
    def->error_angle[0] = rdf(c, arr + HTA_TRIG_ERROR_ANGLE);
    def->error_angle[1] = rdf(c, arr + HTA_TRIG_ERROR_ANGLE + 4u);
    def->error_accel    = rdf(c, arr + HTA_TRIG_ERROR_ACCEL);
    def->error_decel    = rdf(c, arr + HTA_TRIG_ERROR_DECEL);
    def->min_error      = rdf(c, arr + HTA_TRIG_MIN_ERROR);
    for (int k = 0; k < 3; k++)
        def->fp_offset[k] = rdf(c, arr + HTA_TRIG_FP_OFFSET + (uint32_t)k*4u);
    def->projectile_id = rddep(c, arr + HTA_TRIG_PROJECTILE);

    uint32_t fn = 0, fptr = 0, farr = 0;
    if (hta_read_reflexive(c, arr + HTA_TRIG_FIRING_FX, &fn, &fptr) && fn &&
        hta_cache_ptr_to_offset(c, fptr, &farr)) {
        def->firing_fx_id      = rddep(c, farr + HTA_FIREFX_FIRING);
        def->empty_fx_id       = rddep(c, farr + HTA_FIREFX_EMPTY);
        def->firing_damage_id  = rddep(c, farr + HTA_FIREFX_DAMAGE);
    }
}

bool hta_weapon_load_default(const hta_cache *c, const hta_resource_map *bitmaps,
                             hta_weapon_def *def, hta_bsp_mesh *fp,
                             char *err, size_t errlen)
{
    if (!c || !def) return false;
    memset(def, 0, sizeof(*def));
    def->rof = 3.5f;
    def->cooldown = 1.0f / 3.5f;

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

    def->fp_model_id     = rddep(c, off + HTA_WEAP_FP_MODEL);
    def->fp_anim_id      = rddep(c, off + HTA_WEAP_FP_ANIM);
    def->pickup_snd_id   = rddep(c, off + HTA_WEAP_PICKUP_SND);
    def->zoom_in_snd_id  = rddep(c, off + HTA_WEAP_ZOOM_IN_SND);
    def->zoom_out_snd_id = rddep(c, off + HTA_WEAP_ZOOM_OUT_SND);
    load_magazine(c, off, def);
    load_trigger(c, off, def);

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
