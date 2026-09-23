/* A multiplayer game: who is in it, what they carry, who killed whom, and
 * when it is over.
 *
 * Everything that has to be the same for every player lives here, so that
 * the same code can run inside a phone, inside a headless server, and inside
 * a host test with no renderer. A unit is anybody with a body in the game:
 * the player holding this phone, a bot, or a player on another device. The
 * local player keeps its rich first-person path in the platform layer and
 * is mirrored in here every frame; bots and remote players are driven
 * entirely from here, by an input record the brain or the network fills.
 *
 * Damage is attributed. Every hurt names who did it, so a death can be a
 * kill, a suicide or a fall, and Slayer can score it the way Halo does --
 * with the Trial's own kill-feed phrasing and announcer lines.
 *
 * Portable: no renderer, no audio, no platform. The caller drains the event
 * queue for the sounds, words and effects.
 */
#ifndef HTA_GAME_H
#define HTA_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "../engine/player.h"
#include "../engine/vitals.h"
#include "../engine/ammo.h"
#include "../engine/projectile.h"
#include "../engine/pickup.h"
#include "../engine/gun.h"
#include "../asset/weapon.h"
#include "../engine/vehicle.h"
#include "nav.h"
#include "brain.h"

#define HTA_GAME_MAX_UNITS    16
#define HTA_GAME_MAX_WEAPONS  32
#define HTA_GAME_MAX_EVENTS   96
#define HTA_GAME_MAX_POOLS    12
#define HTA_GAME_NAME         24
#define HTA_GAME_NONE         (-1)

/* ---- Slayer's numbers ------------------------------------------------ */
/* INVENTED: a gametype is a saved file of the player's, not part of any map,
 * so nothing in the Trial says these. Halo CE's stock Slayer is 25 kills,
 * no time limit, five seconds to respawn; its multikill window is four
 * seconds; the Trial's announcer has lines for a spree of five and a riot
 * of ten. All ledgered in HANDOFF.md. */
#define HTA_SLAYER_SCORE_LIMIT   25
#define HTA_SLAYER_RESPAWN        5.0f
#define HTA_MULTIKILL_WINDOW      4.0f
#define HTA_SPREE_KILLS           5
#define HTA_RIOT_KILLS           10
/* How long after a hit it still counts as the kill, for a victim who then
 * falls or blows himself up. Ours. */
#define HTA_CREDIT_WINDOW         5.0f
/* A swing at someone's back. Halo CE's melee from behind kills outright;
 * the `jpt!` says 56 and the rest is engine logic. */
#define HTA_BACKSMACK_MULT        10.0f
/* How far a swing reaches -- the platform's own HTA_MELEE_REACH. */
#define HTA_GAME_MELEE_REACH      0.5f
/* Throw speed, the platform's HTA_GRENADE_THROW. Invented there. */
#define HTA_GAME_GRENADE_THROW    9.0f
/* A vehicle that runs into somebody: below this closing speed it only
 * shoves; by the second it deals the whole of `globals\vehicle_collision`
 * (1000, a splatter). The tag has the damage, not the speeds. Ours. */
#define HTA_SPLATTER_MIN_SPEED    1.0f
#define HTA_SPLATTER_FULL_SPEED   3.0f
/* Weapons on the ground: dropped from a swap or by the dead. How many can
 * lie about, and how long one stays before it is cleared away -- Halo
 * garbage-collects them; the gametype says when, the map does not. Ours. */
#define HTA_GAME_MAX_DROPS       32
#define HTA_DROP_LIFE            30.0f
/* How close you must stand to take one: the map's own pickup reach. */
#define HTA_DROP_REACH            0.5f

/* ---- Game types ------------------------------------------------------ */
typedef enum {
    HTA_MODE_SLAYER = 0,      /* free for all */
    HTA_MODE_TEAM_SLAYER,
    HTA_MODE_CTF,
    HTA_MODE_COUNT
} hta_game_mode;

/* Team 0 is red, team 1 blue: the scenario's spawns and flags are numbered
 * that way, and red's are the ones at red's base. Pass this to
 * hta_game_add to have the unit put on the smaller team. */
#define HTA_TEAM_RED     0
#define HTA_TEAM_BLUE    1
#define HTA_TEAM_AUTO  255

/* INVENTED: Halo CE's stock CTF plays to three captures. A gametype says
 * how long an untouched flag lies before it goes home; the map does not.
 * The reaches are ours too: a flag is taken like any weapon on the ground,
 * and a capture counts from anywhere on the stand. All ledgered. */
#define HTA_CTF_SCORE_LIMIT       3
#define HTA_FLAG_RESET           30.0f
#define HTA_FLAG_REACH            HTA_DROP_REACH
#define HTA_CAPTURE_REACH         0.75f
/* Put a flag down and you cannot take it straight back. Ours. */
#define HTA_FLAG_REGRAB           1.0f

typedef enum {
    HTA_FLAG_HOME = 0,
    HTA_FLAG_CARRIED,
    HTA_FLAG_DROPPED
} hta_flag_state;

/* One team's flag: its stand, from the scenario's netgame flags, and where
 * it is now. */
typedef struct {
    bool     present;         /* the map has a stand for this team */
    float    home[3], home_yaw;
    float    pos[3], vel[3], yaw;
    uint8_t  state;           /* hta_flag_state */
    int32_t  carrier;         /* unit, while carried */
    float    idle;            /* seconds on the ground, while dropped */
    bool     rest;
} hta_game_flag;
/* How far ahead a vehicle gun looks for what the crosshair is on. Ours. */
#define HTA_VEHICLE_AIM_RANGE   200.0f

/* ---- The weapon roster, read once ----------------------------------- */
typedef struct {
    uint32_t tag;
    hta_weapon_def def;
    uint32_t impact_jpt;      /* per projectile, 0 for none */
    uint32_t melee_jpt;       /* Weapon +916 `player melee damage` */
    float    melee_damage;    /* what that does to armour */
    bool     travels;         /* its round is an object with a model */
    int32_t  pool;            /* which projectile pool flies it, or -1 */
    float    speed;           /* wu/s of that round, 0 for hitscan */
    float    blast_damage, blast_radius, blast_core;
    uint32_t model;           /* third-person `mod2`, 0 if none */
    char     label[8];        /* Weapon +780, "ar" */
    char     anim_class[12];  /* the cyborg's stance word for it: "rifle" */
    bool     z_prefix;        /* the flamethrower and cannon's "zstand" */
    float    autoaim_angle, autoaim_range;   /* +996, +1000 */
    float    magnet_angle, magnet_range;     /* +1004, +1008 */
    /* A vehicle's gun: one roster entry per trigger, never carried. */
    bool     vehicle;
    int8_t   trigger;
} hta_game_weapon;

/* A weapon lying where it fell, with what was left in it. */
typedef struct {
    bool     live;
    int32_t  weapon;          /* roster index */
    hta_ammo ammo;
    float    pos[3], vel[3], yaw;
    float    age;
    bool     rest;
} hta_game_drop;

/* The state of one vehicle's gun, per trigger. */
typedef struct {
    float   cooldown[2];      /* seconds until it may fire again */
    float   held[2];          /* seconds the trigger has been down: spin-up */
    float   error[2];         /* 0 settled .. 1 bloomed */
    int     loaded[2];        /* rounds in the magazine; -1 bottomless */
    float   chamber[2];       /* seconds until the magazine is full again */
    bool    was_down[2];      /* for a gun that fires once per pull */
    int32_t last_driver;      /* for the kill when an empty car rolls on */
    float   since_driven;
} hta_game_vgun;

/* ---- A body in the game --------------------------------------------- */
typedef enum {
    HTA_UNIT_NONE = 0,
    HTA_UNIT_LOCAL,     /* this device's player: mirrored, not simulated */
    HTA_UNIT_BOT,
    HTA_UNIT_REMOTE     /* another device's player */
} hta_unit_kind;

/* What drives a unit this update. Look deltas are radians, like the
 * player's own input. One-shot buttons are consumed by the update. */
typedef struct {
    hta_player_input move;
    bool melee, grenade, reload, swap, pickup;
    bool action;     /* get in, or out: one-shot */
    bool fire2;      /* a vehicle gun's second trigger (the grenade button) */
} hta_unit_input;

typedef struct {
    int32_t  weapon;          /* roster index, -1 for an empty hand */
    hta_ammo ammo;
} hta_carried;

typedef struct {
    hta_unit_kind kind;
    char     name[HTA_GAME_NAME];
    uint8_t  team;            /* 0 red, 1 blue; free-for-all ignores it */

    hta_player  body;
    hta_camera  eye;          /* position and look; fov unused */
    hta_vitals  vitals;
    hta_unit_input in;

    bool     alive;
    float    respawn;         /* seconds until back, while dead */
    float    dead_for;        /* seconds since death, for the corpse */
    float    death_yaw;

    /* In a vehicle: which car and seat, -1 on foot. */
    int16_t  vehicle;
    int8_t   seat;

    hta_carried carry[2];
    uint32_t slot;            /* which of the two is in hand */
    int8_t   flag;            /* team of the flag in hand instead, or -1 */
    float    flag_wait;       /* seconds before it may take a flag again */
    int      grenades;
    float    cooldown;        /* seconds until the trigger can fire again */
    float    error;           /* 0 settled .. 1 bloomed, per the trigger */
    float    since_shot;
    float    swing;           /* seconds left of a melee swing */
    float    throwing;        /* seconds left of a grenade throw */

    /* Score. */
    int      kills, deaths, suicides, betrayals, assists;
    int      score;
    int      spree;           /* kills since the last death */
    int      multi;           /* kills inside the multikill window */
    float    multi_timer;
    int32_t  last_attacker;
    float    since_attacked;
    int32_t  attackers[HTA_GAME_MAX_UNITS]; /* for assists: a hit this life */

    /* Cosmetic state for the renderer and the network. */
    bool     fired;           /* one-shot: a round left this update */
    bool     meleed, threw, hurt, reloading;
    bool     splattered;      /* the last hurt was a vehicle running into it */
    float    powerup_timer;
    uint8_t  powerup;         /* hta_item_kind */

    uint32_t rng;
} hta_unit;

/* ---- Things the caller turns into sound, words and effects ---------- */
typedef enum {
    HTA_EV_NONE = 0,
    HTA_EV_KILL,          /* a: victim, b: killer (-1 none), weapon */
    HTA_EV_SPAWN,         /* a */
    HTA_EV_FIRE,          /* a, weapon, pos (muzzle), dir */
    HTA_EV_HIT_WORLD,     /* a (shooter), weapon, pos, dir (normal), material */
    HTA_EV_HIT_UNIT,      /* a: victim, b: attacker, pos, amount */
    HTA_EV_MELEE,         /* a, b (-1 miss) */
    HTA_EV_GRENADE,       /* a */
    HTA_EV_DETONATE,      /* a (owner), pool, pos, dir (normal), material */
    HTA_EV_PICKUP,        /* a, item tag in `tag` */
    HTA_EV_ANNOUNCE,      /* text + announcer line id in `line` */
    HTA_EV_GAME_OVER,     /* a: winner */
    HTA_EV_RELOAD,        /* a */
    HTA_EV_SWAP,          /* a, weapon */
    HTA_EV_ENTER,         /* a: unit, b: car, pool: seat */
    HTA_EV_EXIT,          /* a: unit, b: car, pool: seat */
    HTA_EV_FLAG           /* a: unit (-1 none), b: flag's team, pool: hta_flag_event */
} hta_event_kind;

/* The announcer, by line. The Trial's `sound\dialog\multiplayer1\...`. */
typedef enum {
    HTA_LINE_NONE = 0,
    HTA_LINE_SLAYER,
    HTA_LINE_DOUBLE_KILL,
    HTA_LINE_TRIPLE_KILL,
    HTA_LINE_KILLTACULAR,
    HTA_LINE_KILLING_SPREE,
    HTA_LINE_RUNNING_RIOT,
    HTA_LINE_GAME_OVER,
    HTA_LINE_TEAM_SLAYER,
    HTA_LINE_CTF,
    HTA_LINE_RED_HAS_FLAG,    /* red has taken blue's flag */
    HTA_LINE_BLUE_HAS_FLAG,
    HTA_LINE_RED_RETURNED,    /* red's own flag is home again */
    HTA_LINE_BLUE_RETURNED,
    HTA_LINE_RED_SCORE,
    HTA_LINE_BLUE_SCORE,
    HTA_LINE_COUNT
} hta_line;

/* What happened to a flag, in HTA_EV_FLAG's `pool`. */
typedef enum {
    HTA_FLAG_TAKEN = 0,
    HTA_FLAG_DROP,
    HTA_FLAG_RETURN,          /* a teammate touched it (a) or it timed out (-1) */
    HTA_FLAG_CAPTURE
} hta_flag_event;

typedef struct {
    hta_event_kind kind;
    int32_t  a, b;
    int32_t  weapon;
    int32_t  pool;
    float    pos[3], dir[3];
    float    amount;
    uint8_t  material;
    uint32_t tag;
    hta_line line;
    bool     for_local;       /* an announcement meant for this device's player */
    char     text[96];
} hta_game_event;

/* ---- The game ------------------------------------------------------- */
typedef struct hta_game {
    const hta_cache     *cache;
    const hta_collision *col;    /* static world plus whatever `extra` holds */
    hta_nav             *nav;    /* borrowed; bots cannot move without it */
    hta_pickups         *items;  /* borrowed; the platform draws them */
    hta_vehicles        *vehicles; /* borrowed; see hta_game_attach_vehicles */
    /* Roster entries of each vehicle type's gun, per trigger; -1 none. */
    int32_t         vweapon[HTA_VEHICLE_TYPES][2];
    hta_game_vgun   vgun[HTA_VEHICLE_MAX];
    uint32_t        splatter_jpt;   /* globals\vehicle_collision */
    float           gravity;        /* the biped's, for vehicle physics */
    /* True where this device runs vehicle physics (solo, a host). A
     * client copies the host's cars and only reads seats. */
    bool            simulate_vehicles;

    hta_game_weapon weapons[HTA_GAME_MAX_WEAPONS];
    uint32_t        weapon_count;
    int32_t         start_weapon[2];
    int             start_grenades, max_grenades;

    /* Rounds that fly, for units the game simulates: one pool per
     * projectile that has a model, plus the frag grenade. The platform
     * draws their meshes. */
    hta_projectiles pools[HTA_GAME_MAX_POOLS];
    int8_t          pool_owner[HTA_GAME_MAX_POOLS][HTA_PROJ_MAX];
    int32_t         pool_weapon[HTA_GAME_MAX_POOLS];  /* roster index, -1 grenade */
    uint32_t        pool_count;
    int32_t         grenade_pool;

    hta_player_physics phys;
    hta_vitals      vitals_template;
    hta_spawn_point spawns[64];
    uint32_t        spawn_count;

    hta_unit        units[HTA_GAME_MAX_UNITS];
    hta_brain       brains[HTA_GAME_MAX_UNITS];
    uint32_t        unit_count;
    int32_t         local;       /* the unit this device plays, or -1 */

    /* The rules. */
    hta_game_mode   mode;
    int             team_score[2];
    int32_t         winner_team;  /* when a team game is over; -1 a draw */
    hta_game_flag   flags[2];
    /* Each stand's way home from anywhere on the nav grid (see
     * hta_nav_field), built when a CTF game starts with a grid. */
    uint32_t       *stand_field[2];
    uint32_t        stand_node[2];
    int32_t         flag_weapon;  /* roster index of the flag; never offered */
    int             score_limit;
    float           time_limit;  /* seconds; 0 plays to the score */
    float           respawn_time;
    bool            teams;
    bool            over;
    int32_t         winner;
    float           time;        /* seconds played */
    int32_t         leader;      /* who is ahead, for "taken the lead" */

    hta_game_drop   drops[HTA_GAME_MAX_DROPS];
    /* True where this device runs the drops (solo, a host). */
    bool            simulate_drops;

    hta_game_event  events[HTA_GAME_MAX_EVENTS];
    uint32_t        event_count;

    /* The Trial's words. */
    uint32_t        text_tag;    /* ui\multiplayer_game_text */
    uint32_t        names_tag;   /* ui\random_player_names */
    uint32_t        rng;
    bool            loaded;
} hta_game;

/* Reads the roster, the grenade, the biped's physics and vitals, the
 * spawn points and the Trial's strings. `col` must outlive the game. The
 * projectile pools load their meshes from `bitmaps` (may be NULL). */
bool hta_game_load(hta_game *g, const hta_cache *c, const hta_resource_map *bitmaps,
                   const hta_collision *col, char *err, size_t errlen);
void hta_game_free(hta_game *g);

/* Choose the rules, and the mode's score limit with them. Call before
 * adding units: a team game puts each HTA_TEAM_AUTO unit on the smaller team. Returns false (and plays
 * Slayer) when the map lacks what the mode needs -- CTF without both flags. */
bool hta_game_set_mode(hta_game *g, hta_game_mode mode);

/* Start the game: scores cleared, flags home, everyone respawned. */
void hta_game_start(hta_game *g);

/* Add a unit. Returns its index, or -1. A bot with no name takes one from
 * the Trial's `random_player_names`. */
int32_t hta_game_add(hta_game *g, hta_unit_kind kind, const char *name, uint8_t team);
void    hta_game_remove(hta_game *g, int32_t unit);

/* Roster lookups. */
int32_t hta_game_weapon_index(const hta_game *g, uint32_t weap_tag);
const hta_game_weapon *hta_game_held(const hta_game *g, int32_t unit);

/* Give a unit a weapon: into a free hand, or in place of the one held.
 * Its magazine comes full unless `ammo` says otherwise. */
void hta_game_give(hta_game *g, int32_t unit, int32_t weapon, const hta_ammo *ammo);

/* Where a unit should come back: away from its enemies. The platform
 * uses this for the local player's own respawn. */
void hta_game_pick_spawn(hta_game *g, int32_t unit, float out_pos[3], float *out_facing);

/* The platform has respawned the local player: count it as alive. */
void hta_game_revive(hta_game *g, int32_t unit);
/* Spawn a newly joined player at a safe scenario start. */
void hta_game_spawn(hta_game *g, int32_t unit);

/* Bot difficulty, 0..3, for bots added from now on and those already in. */
void hta_game_set_skill(hta_game *g, uint8_t skill);

/* The local player, mirrored in: where it is and where it looks. The
 * platform keeps simulating it; this is so everyone else can see it, aim at
 * it and be hit by it. Its vitals are the game's -- the platform reads and
 * writes g->units[local].vitals directly. */
void hta_game_sync_local(hta_game *g, const hta_player *body, const hta_camera *eye,
                         int32_t weapon);

/* One step: bots and remote players move and fight, rounds fly, the dead
 * come back, and the score is kept. */
void hta_game_update(hta_game *g, float dt);

/* ---- Vehicles ----------------------------------------------------- */
/* Hand the game the map's vehicles: their guns join the roster (one entry
 * per trigger), their seats take units. `bitmaps` loads projectile art. */
bool hta_game_attach_vehicles(hta_game *g, hta_vehicles *v,
                              const hta_resource_map *bitmaps);
/* The seat `unit` would get into from where it stands, or -1. */
int32_t hta_game_seat_near(const hta_game *g, int32_t unit, int32_t *out_seat);
/* Get in (on foot) or out (seated). True if it happened. */
bool hta_game_board(hta_game *g, int32_t unit);
/* Put a unit in a particular seat, or take it out without a safe exit
 * (death, disconnect). Clients mirror the host's seats this way. */
bool hta_game_seat(hta_game *g, int32_t unit, int32_t car, int32_t seat);
void hta_game_unseat(hta_game *g, int32_t unit);
/* Is the unit hidden inside its vehicle -- the Scorpion's driver, the
 * Banshee's pilot -- where bullets cannot reach it? */
bool hta_game_enclosed(const hta_game *g, int32_t unit);
/* Where a seated unit's body is: its root in the world. */
bool hta_game_seat_root(const hta_game *g, int32_t unit, hta_transform *out);

/* ---- The motion tracker -------------------------------------------- */
/* Retail Halo's tracker reaches 25 m; the Trial's `motion sensor range`
 * says 20 with no unit and its art is labelled 15m, so neither can be
 * taken at face value. 25 m in world units. Ours. */
#define HTA_MOTION_RANGE   (25.0f / 3.048f)
/* Slower than this is a crouch-walk and does not show. Ours. */
#define HTA_MOTION_SPEED    0.5f
/* A shot shows its shooter this long. Ours. */
#define HTA_MOTION_FIRE     1.0f
typedef struct {
    float x, y;      /* right, forward, as fractions of the range */
    bool  friendly;
    bool  vehicle;
} hta_game_contact;
/* Who `viewer` sees on its tracker. Returns how many were written. */
uint32_t hta_game_sensor(const hta_game *g, int32_t viewer, hta_game_contact *out, uint32_t max);

/* ---- Weapons on the ground ----------------------------------------- */
/* Put a weapon down: falls from `pos` with `vel`. Returns its slot. The
 * oldest one goes when the ground is full. */
int32_t hta_game_drop_weapon(hta_game *g, int32_t weapon, const hta_ammo *ammo,
                             const float pos[3], float yaw, const float vel[3]);
/* The nearest dropped weapon within reach of `feet`, or -1. */
int32_t hta_game_drop_near(const hta_game *g, const float feet[3], float reach);
/* Pick one up: its weapon and ammo out, the slot freed. */
bool hta_game_take_drop(hta_game *g, int32_t drop, int32_t *weapon, hta_ammo *ammo);

/* A team's colour, for the armour of everyone on it and for its flag.
 * INVENTED: Halo's player colours are in the executable, not the map, so
 * these are ours -- a strong red and blue that survive the cyborg's grey
 * base map. Ledgered. */
void hta_game_team_color(int team, float out[3]);

/* Where a flag is drawn this frame: upright on its stand, lying where it
 * fell, or nowhere (in a hand, which the held weapons draw). False for
 * nowhere. `model` is column-major, the flag's own space to the world. */
bool hta_game_flag_model(const hta_game *g, int team, float model[16]);

/* ---- Capture the flag ---------------------------------------------- */
/* Let go of a carried flag where the unit stands. True if it had one. */
bool hta_game_drop_flag(hta_game *g, int32_t unit);
/* Where a bot playing CTF should be heading, or false for "anywhere".
 * `stand` says whose flag stand that is (so the stand's field can lead
 * there), or -1 for somewhere that moves. */
bool hta_game_ctf_goal(const hta_game *g, int32_t unit, float out[3], int *stand);

/* ---- Damage, from anyone ------------------------------------------- */
/* A ray against every living unit but `ignore`. The nearest hit's unit, or
 * -1; `out_t` is the distance along the unit `dir`. */
int32_t hta_game_ray(const hta_game *g, const float orig[3], const float dir[3],
                     float max_t, int32_t ignore, float *out_t, float out_hit[3]);

/* Is anyone alive within `reach` of `pos`? For a projectile passing
 * through, or a swing. */
int32_t hta_game_near(const hta_game *g, const float pos[3], float reach, int32_t ignore);

/* Hurt a unit by a `jpt!` -- against its shield while it has one, else its
 * armour -- `count` times (a shotgun's pellets). Attributed to `attacker`. */
void hta_game_hurt_jpt(hta_game *g, int32_t victim, int32_t attacker,
                       uint32_t jpt, int count, const float at[3]);

/* Hurt a unit by a plain amount. */
void hta_game_hurt(hta_game *g, int32_t victim, int32_t attacker, float amount,
                   const float at[3]);

/* An explosion: everyone inside `radius` takes `damage`, full inside `core`
 * and tapering to nothing at the edge. */
void hta_game_blast(hta_game *g, int32_t attacker, const float centre[3],
                    float damage, float core, float radius);

/* A swing from `unit`'s eye along its look, with its held weapon's own
 * melee damage -- or everything, from behind. Returns who it hit, or -1.
 * The platform calls this for the local player; the game does for the rest. */
int32_t hta_game_melee(hta_game *g, int32_t unit);

/* Centre of a unit's chest. */
void hta_game_centre(const hta_game *g, int32_t unit, float out[3]);

/* ---- Events --------------------------------------------------------- */
/* Pop the oldest event. False when there are none. */
bool hta_game_pop(hta_game *g, hta_game_event *out);

/* ---- Scoreboard ----------------------------------------------------- */
/* Units ordered by score, best first. Returns how many. */
uint32_t hta_game_standings(const hta_game *g, int32_t *out, uint32_t max);

/* The line Halo's own HUD shows for the local player: "In first place with
 * 12 kills", or in a team game "Red leads Blue 2 to 1 Captures", in the
 * Trial's words. */
void hta_game_place_text(const hta_game *g, int32_t unit, char *out, size_t outlen);

/* The cyborg's animation for what a unit is doing, e.g. "stand rifle
 * move-front". The renderer plays it; `action` is set to an overlay name
 * (fire, melee, reload, throw) for the update in which one starts. */
void hta_game_anim(const hta_game *g, int32_t unit, char *base, size_t baselen,
                   char *action, size_t actlen);

#endif
