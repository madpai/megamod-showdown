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

bool hta_weapon_load_id(const hta_cache *c, const hta_resource_map *bitmaps,
                        uint32_t weap_tag_id,
                        hta_weapon_def *def, hta_bsp_mesh *fp,
                        char *err, size_t errlen)
{
    if (!c || !def) return false;
    memset(def, 0, sizeof(*def));
    def->rof = 3.5f;
    def->cooldown = 1.0f / 3.5f;

    int32_t best = hta_cache_find_tag_by_id(c, weap_tag_id);
    if (best < 0) { if (err) snprintf(err, errlen, "weap 0x%08X not found", weap_tag_id); return false; }

    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)best, &t)) return false;
    if (t.primary_class != HTA_TAG_WEAP) {
        if (err) snprintf(err, errlen, "0x%08X is not a weapon", weap_tag_id);
        return false;
    }
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return false;
    hta_cache_tag_path(c, &t, def->path, sizeof(def->path));

    def->hud_interface_id = rddep(c, off + HTA_WEAP_HUD_INTERFACE);
    def->zoom_levels = rdi16(c, off + HTA_WEAP_ZOOM_LEVELS);
    if (def->zoom_levels < 0 || def->zoom_levels > 8) def->zoom_levels = 0;
    def->zoom_mag[0] = rdf(c, off + HTA_WEAP_ZOOM_MAG);
    def->zoom_mag[1] = rdf(c, off + HTA_WEAP_ZOOM_MAG + 4u);
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

bool hta_weapon_load_default(const hta_cache *c, const hta_resource_map *bitmaps,
                             hta_weapon_def *def, hta_bsp_mesh *fp,
                             char *err, size_t errlen)
{
    if (!c || !def) return false;
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
    return hta_weapon_load_id(c, bitmaps, t.tag_id, def, fp, err, errlen);
}

/* Every weapon in the cache a player could actually hold: it must have a
 * first-person model AND first-person animations. That rules out the
 * vehicle turrets, the flag and the ball, which have neither. */
uint32_t hta_weapon_list_playable(const hta_cache *c, uint32_t *out, uint32_t max)
{
    if (!c || !out || !max) return 0;
    uint32_t n = 0;
    /* The first-person model and animations of everything kept so far, so a
     * weapon that looks and handles exactly like one already in the list is
     * not offered twice. */
    uint32_t seen_model[64], seen_anim[64];
    uint32_t seen = 0;
    for (uint32_t i = 0; i < c->tag_count && n < max; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_TAG_WEAP) continue;
        uint32_t off;
        if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) continue;
        uint32_t fpm = rddep(c, off + HTA_WEAP_FP_MODEL);
        uint32_t fpa = rddep(c, off + HTA_WEAP_FP_ANIM);
        if (!fpm || !fpa) continue;
        /* And it must shoot. The ball and the flag are held in first person
         * with their own animations, but they are carried, not fired: no
         * projectile, no HUD interface, no crosshair. */
        uint32_t hud = rddep(c, off + HTA_WEAP_HUD_INTERFACE);
        if (!hud) continue;

        /* `needler` and `mp_needler` are separate tags with the same
         * first-person model, animation graph, HUD, magazine and rate of
         * fire -- the multiplayer variant differs only in numbers the
         * player never sees. Offering both just gives you the same gun
         * twice in the swap order. */
        bool dup = false;
        for (uint32_t k = 0; k < seen; k++)
            if (seen_model[k] == fpm && seen_anim[k] == fpa) { dup = true; break; }
        if (dup) continue;

        /* And it must have been built on the standard first-person rig.
         *
         * Every carried Trial weapon's fp model is the GUN alone -- 3 to 7
         * nodes -- and wears the globals hands. `weapons\plasma_cannon` (the
         * fuel rod gun) instead ships a self-contained 41-node model with
         * its own arms, and its idle holds the gun against the camera. It
         * is an unfinished asset: Halo never gives it to the player, and in
         * the hands it fills half the screen. Nothing in the weapon flags
         * marks it, but the shape of its model does. */
        if (hta_model_has_node(c, fpm, "frame l wriste")) continue;

        if (seen < 64) { seen_model[seen] = fpm; seen_anim[seen] = fpa; seen++; }

        out[n++] = t.tag_id;
    }
    return n;
}
