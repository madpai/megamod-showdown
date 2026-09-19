#include "biped.h"
#include "bsp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void hta_player_physics_defaults(hta_player_physics *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    /* Pre-tag fallbacks so tests without a map still run. */
    p->run_forward = 1.05f;
    p->run_back = 1.05f;
    p->run_side = 1.05f;
    p->run_accel = 8.0f;
    p->sneak_forward = 0.9f;
    p->sneak_back = 0.65f;
    p->sneak_side = 0.6f;
    p->sneak_accel = 4.8f;
    p->air_accel = 1.05f;
    p->walk_speed = 0.512f;
    p->jump_speed = 1.25f;
    p->gravity = 4.0f;
    p->cam_stand = 0.60f;
    p->cam_crouch = 0.35f;
    p->crouch_time = 0.2f;
    p->coll_stand = 0.7f;
    p->coll_crouch = 0.5f;
    p->radius = 0.12f;
    p->max_slope = 1.047f; /* 60 deg fallback */
    p->downhill_scale = 1.0f;
    p->uphill_scale = 1.0f;
    p->fov_y = 1.2217305f;
    p->loaded = 0;
}

static float rd_f(const hta_cache *c, uint32_t off)
{
    float f = 0.0f;
    hta_rd_f32(c, off, &f);
    return f;
}

/* HEK labels accel as wu/s^2, but the Trial stores 0.32 / 0.16 / 0.035 —
 * treating those as per-tick (×30 Hz) matches jump's per-tick unit and
 * produces Halo-like 0.2s time-to-max-run. */
static float accel_wu_s2(float tagged)
{
    if (tagged <= 0.0f) return 8.0f;
    if (tagged < 1.0f) return tagged * HTA_TICK_HZ;
    return tagged;
}

bool hta_player_physics_load(hta_player_physics *p, const hta_cache *c,
                             char *err, size_t errlen)
{
    if (!p) return false;
    hta_player_physics_defaults(p);
    if (!c) { if (err) snprintf(err, errlen, "no cache"); return false; }

    int32_t gi = hta_cache_find_tag_by_class(c, HTA_TAG_MATG);
    if (gi >= 0) {
        hta_tag_entry t;
        uint32_t goff, n = 0, ptr = 0, arr;
        if (hta_cache_tag(c, (uint32_t)gi, &t) &&
            hta_cache_ptr_to_offset(c, t.tag_data_ptr, &goff) &&
            hta_read_reflexive(c, goff + HTA_MATG_PLAYER_INFO, &n, &ptr) &&
            n >= 1 && hta_cache_ptr_to_offset(c, ptr, &arr)) {
            p->walk_speed    = rd_f(c, arr + HTA_PINFO_WALK);
            p->run_forward   = rd_f(c, arr + HTA_PINFO_WALK + 8);
            p->run_back      = rd_f(c, arr + HTA_PINFO_WALK + 12);
            p->run_side      = rd_f(c, arr + HTA_PINFO_WALK + 16);
            p->run_accel     = accel_wu_s2(rd_f(c, arr + HTA_PINFO_WALK + 20));
            p->sneak_forward = rd_f(c, arr + HTA_PINFO_WALK + 24);
            p->sneak_back    = rd_f(c, arr + HTA_PINFO_WALK + 28);
            p->sneak_side    = rd_f(c, arr + HTA_PINFO_WALK + 32);
            p->sneak_accel   = accel_wu_s2(rd_f(c, arr + HTA_PINFO_WALK + 36));
            p->air_accel     = accel_wu_s2(rd_f(c, arr + HTA_PINFO_WALK + 40));
            p->loaded = 1;
        }
    }

    int32_t best = -1, any = -1;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.primary_class != HTA_TAG_BIPD) continue;
        char path[192];
        hta_cache_tag_path(c, &t, path, sizeof(path));
        any = (int32_t)i;
        if (strstr(path, "cyborg_mp")) { best = (int32_t)i; break; }
        if (best < 0 && strstr(path, "cyborg")) best = (int32_t)i;
    }
    if (best < 0) best = any;
    if (best >= 0) {
        hta_tag_entry t;
        uint32_t off;
        if (hta_cache_tag(c, (uint32_t)best, &t) &&
            hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) {
            float fov = rd_f(c, off + HTA_BIPD_UNIT + HTA_UNIT_FOV);
            if (fov > 0.4f && fov < 2.5f) p->fov_y = fov;
            /* Biped inherits Unit inherits Object, so its own fields start
             * at HTA_BIPD_BODY; footsteps is a dependency 156 in. */
            uint32_t foot = 0;
            if (hta_rd_u32(c, off + HTA_BIPD_BODY + HTA_BIPD_FOOTSTEPS + 12u, &foot) &&
                foot && foot != 0xFFFFFFFFu)
                p->footsteps_id = foot;
            float slope = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_SLOPE);
            if (slope > 0.1f && slope < 1.6f) p->max_slope = slope;
            float ds = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_DOWN_SCALE);
            float us = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_UP_SCALE);
            if (ds > 0.1f && ds < 4.0f) p->downhill_scale = ds;
            if (us > 0.1f && us < 4.0f) p->uphill_scale = us;
            float jtick = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_JUMP);
            if (jtick > 0.0f && jtick < 2.0f)
                p->jump_speed = jtick * HTA_TICK_HZ;
            float cs = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_CAM_STAND);
            float cc = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_CAM_CROUCH);
            float ct = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_CROUCH_TIME);
            if (cs > 0.2f && cs < 2.0f) p->cam_stand = cs;
            if (cc > 0.1f && cc < 2.0f) p->cam_crouch = cc;
            if (ct > 0.0f && ct < 2.0f) p->crouch_time = ct;
            float hs = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_COLL_STAND);
            float hc = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_COLL_CROUCH);
            float r  = rd_f(c, off + HTA_BIPD_BODY + HTA_BIPD_COLL_RADIUS);
            if (hs > 0.2f && hs < 3.0f) p->coll_stand = hs;
            if (hc > 0.1f && hc < 3.0f) p->coll_crouch = hc;
            if (r  > 0.05f && r  < 1.0f) p->radius = r;
            p->loaded = 1;
        }
    }

    if (!p->loaded && err)
        snprintf(err, errlen, "no globals/biped player physics in cache");
    return p->loaded != 0;
}
