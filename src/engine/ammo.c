#include "ammo.h"

#include <string.h>

void hta_ammo_init(hta_ammo *a, const hta_weapon_def *w)
{
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->mag_max     = 60;
    a->reserve_max = 180;
    a->per_shot    = 1;
    a->per_reload  = 60;
    a->reload_time = 2.0f;

    if (w) {
        if (w->rounds_loaded_max > 0)  a->mag_max = w->rounds_loaded_max;
        if (w->rounds_reserve_max > 0) a->reserve_max = w->rounds_reserve_max;
        if (w->rounds_per_shot > 0)    a->per_shot = w->rounds_per_shot;
        if (w->reload_time > 0.0f)     a->reload_time = w->reload_time;
        a->chamber_time = w->chamber_time;
        /* A magazine weapon reloads the whole magazine; a tag that says 0
         * (or something silly) would otherwise make reloading a no-op. */
        a->per_reload = (w->rounds_reloaded > 0) ? w->rounds_reloaded : a->mag_max;

        /* "rounds total initial" counts the loaded magazine too. */
        int initial = w->rounds_initial > 0 ? w->rounds_initial : a->mag_max;
        a->loaded = initial < a->mag_max ? initial : a->mag_max;
        a->reserve = initial - a->loaded;
    } else {
        a->loaded = a->mag_max;
        a->reserve = a->reserve_max;
    }
    if (a->reserve > a->reserve_max) a->reserve = a->reserve_max;
    a->phase = HTA_AMMO_READY;
}

bool hta_ammo_shoot(hta_ammo *a)
{
    if (!a) return false;
    a->spent = false;
    a->dry = false;
    /* A shell-at-a-time weapon fires the moment it has one round in it:
     * pulling the trigger abandons the rest of the reload rather than being
     * refused by it. That is what makes the shotgun playable. A weapon that
     * reloads a whole magazine is not interruptible this way. */
    if (a->phase == HTA_AMMO_RELOADING && a->chaining &&
        a->loaded >= a->per_shot) {
        a->chaining = false;
        a->phase = HTA_AMMO_READY;
        a->timer = 0.0f;
    }
    if (a->phase != HTA_AMMO_READY) return false;
    if (a->loaded < a->per_shot) {
        a->dry = true;
        return false;
    }
    a->loaded -= a->per_shot;
    a->spent = true;
    a->chaining = false;
    return true;
}

bool hta_ammo_reload(hta_ammo *a)
{
    if (!a) return false;
    a->reload_began = false;
    if (a->phase == HTA_AMMO_RELOADING) return false;
    if (a->loaded >= a->mag_max) return false;
    if (a->reserve <= 0) return false;
    a->phase = HTA_AMMO_RELOADING;
    a->timer = a->reload_time;
    a->reload_began = true;
    /* Only worth chaining when one go does not fill the magazine. */
    a->chaining = (a->per_reload < a->mag_max);
    return true;
}

void hta_ammo_cancel_reload(hta_ammo *a)
{
    if (!a) return;
    a->chaining = false;
}

void hta_ammo_update(hta_ammo *a, float dt)
{
    if (!a) return;
    a->reload_done = false;
    a->reload_began = false;
    if (a->phase != HTA_AMMO_RELOADING) return;

    a->timer -= dt;
    if (a->timer > 0.0f) return;

    int room = a->mag_max - a->loaded;
    int want = a->per_reload < room ? a->per_reload : room;
    int take = want < a->reserve ? want : a->reserve;
    if (take < 0) take = 0;
    a->loaded += take;
    a->reserve -= take;
    a->timer = 0.0f;
    a->phase = HTA_AMMO_READY;
    a->reload_done = true;

    /* Keep going until the magazine is full or the reserve is out. */
    if (a->chaining && a->loaded < a->mag_max && a->reserve > 0) {
        a->phase = HTA_AMMO_RELOADING;
        a->timer = a->reload_time;
        a->reload_began = true;
    } else {
        a->chaining = false;
    }
}

float hta_ammo_reload_progress(const hta_ammo *a)
{
    if (!a || a->phase != HTA_AMMO_RELOADING) return 1.0f;
    if (a->reload_time <= 0.0f) return 1.0f;
    float p = 1.0f - (a->timer / a->reload_time);
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}
