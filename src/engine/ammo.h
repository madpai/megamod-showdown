/* Magazine state, driven entirely by the weapon's own tag values.
 *
 * Halo's `WeaponMagazine` gives us everything: how big the magazine is, how
 * much ammo you spawn with in total, how long a reload takes, and how many
 * rounds a reload puts back. Nothing here is invented -- the AR's 60-round
 * magazine, 180 spare and 3.4 s reload all come from the Trial's own tag.
 *
 * "rounds total initial" is the TOTAL you spawn with, magazine included, so
 * the AR's 240 becomes 60 loaded + 180 in reserve.
 *
 * Portable: no platform, no renderer, no audio. The caller turns the one-shot
 * event flags into sounds and animations.
 */
#ifndef HTA_AMMO_H
#define HTA_AMMO_H

#include <stdbool.h>
#include "../asset/weapon.h"

typedef enum {
    HTA_AMMO_READY = 0,
    HTA_AMMO_RELOADING
} hta_ammo_phase;

typedef struct {
    int   loaded;
    int   reserve;
    int   mag_max;
    int   reserve_max;
    int   per_shot;
    int   per_reload;      /* rounds a single reload puts back */
    float reload_time;
    float chamber_time;

    hta_ammo_phase phase;
    float timer;           /* seconds left in the current reload */
    /* A weapon that reloads one round at a time keeps going until it is
     * full. Halo's shotgun loads a single shell per 0.40 s, and making the
     * player ask for each one is not how it plays. Firing cancels it. */
    bool  chaining;

    /* One-shot events, true for the update in which they happened. The
     * caller clears nothing: each is recomputed every call. */
    bool  spent;           /* a round left the magazine this call */
    bool  dry;             /* the trigger was pulled on an empty magazine */
    bool  reload_began;
    bool  reload_done;
} hta_ammo;

void hta_ammo_init(hta_ammo *a, const hta_weapon_def *w);

/* Pull the trigger. True if a round was actually spent -- that is the only
 * case in which the caller should fire a shot. Sets `dry` instead when the
 * magazine is empty, and never fires while a reload is running. */
bool hta_ammo_shoot(hta_ammo *a);

/* Start a reload. False if one is already running, the magazine is full, or
 * there is nothing in reserve. */
bool hta_ammo_reload(hta_ammo *a);

/* Advances a running reload, and starts the next round of a partial reload
 * when the weapon loads fewer rounds than the magazine holds. */
void hta_ammo_update(hta_ammo *a, float dt);

/* Stop a chained reload where it is, keeping what has been loaded. Firing
 * does this in Halo: one shell in the gun is enough to shoot with. */
void hta_ammo_cancel_reload(hta_ammo *a);

/* Fraction of the current reload completed, 0..1 (1 when not reloading). */
float hta_ammo_reload_progress(const hta_ammo *a);

#endif
