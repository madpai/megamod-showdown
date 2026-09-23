#ifndef HTA_NET_PROTOCOL_H
#define HTA_NET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_NET_MAGIC 0x31415448u /* "HTA1" on the wire */
#define HTA_NET_VERSION 3u
#define HTA_NET_HEADER 20u
#define HTA_NET_MAX_PACKET 1200u
#define HTA_NET_MAX_PLAYERS 8u
#define HTA_NET_PLAYER_BYTES 35u
#define HTA_NET_MAX_ENTITIES 16u
#define HTA_NET_ENTITY_NAME 12u
#define HTA_NET_ENTITY_BYTES 68u
#define HTA_NET_WORLD_HEADER 86u
#define HTA_NET_CONTROL_BYTES 29u
#define HTA_NET_KILL_BYTES 102u
#define HTA_NET_FX_BYTES 28u
#define HTA_NET_PROJECTILE_BYTES 30u
#define HTA_NET_MAX_PROJECTILES 32u
/* Projectile pools: the match's own first, then the host's first-person
 * weapon and its grenades. */
#define HTA_NET_MAX_POOLS 16u
#define HTA_NET_POOL_HOST_WEAPON 12u
#define HTA_NET_POOL_HOST_GRENADES 13u
/* Roster entries an FX may name: carried weapons and vehicle triggers. */
#define HTA_NET_MAX_WEAPONS 32u
#define HTA_NET_MAX_VEHICLES 32u
#define HTA_NET_VEHICLE_SEATS 6u
#define HTA_NET_VEHICLE_BYTES 36u

typedef enum {
    HTA_NET_HELLO = 1, HTA_NET_WELCOME, HTA_NET_DISCONNECT,
    HTA_NET_PING, HTA_NET_PONG, HTA_NET_INPUT, HTA_NET_SNAPSHOT,
    HTA_NET_EVENT,
    /* Anyone may ask a server what it is; the answer needs no session.
     * This is how a LAN lobby finds games without typing an address. */
    HTA_NET_DISCOVER, HTA_NET_INFO, HTA_NET_WORLD, HTA_NET_CONTROL,
    HTA_NET_KILL, HTA_NET_ACK, HTA_NET_FX, HTA_NET_PROJECTILES,
    HTA_NET_REJECT, HTA_NET_VEHICLES, HTA_NET_DROPS
} hta_net_type;

typedef struct {
    uint8_t type;
    uint32_t sequence, tick;
    const uint8_t *payload;
    uint16_t length;
} hta_net_packet;

typedef struct {
    uint8_t id, weapon, flags;
    float pos[3], velocity[3], yaw, pitch;
} hta_net_player;

enum { HTA_NET_GROUNDED = 1, HTA_NET_CROUCH = 2,
       HTA_NET_FIRE = 4, HTA_NET_MELEE = 8, HTA_NET_GRENADE = 16 };
enum { HTA_NET_EVENT_FIRE = 1, HTA_NET_EVENT_MELEE = 2,
       HTA_NET_EVENT_GRENADE = 3, HTA_NET_EVENT_WEAPON = 4 };

typedef struct {
    uint8_t actor, kind, weapon;
    uint32_t event_id;
} hta_net_event;

/* What a server says about itself in answer to DISCOVER. */
#define HTA_NET_NAME 24u
#define HTA_NET_INFO_BYTES (8u + HTA_NET_NAME)
typedef struct {
    uint32_t nonce;           /* echoes the DISCOVER's */
    uint8_t players, max_players;
    uint8_t score_limit;      /* kills to win */
    uint8_t time_limit;       /* minutes, 0 for none */
    char name[HTA_NET_NAME];  /* printable ASCII, NUL-terminated */
} hta_net_info;

/* The host's match state. Slots are stable for a round, including dead
 * players. Every number that affects combat or the scoreboard comes from
 * the host; clients never send these fields. */
enum { HTA_NET_ENTITY_NONE, HTA_NET_ENTITY_PLAYER, HTA_NET_ENTITY_BOT };
enum { HTA_NET_ENTITY_ALIVE=1, HTA_NET_ENTITY_GROUNDED=2,
       HTA_NET_ENTITY_CROUCH=4, HTA_NET_ENTITY_FIRE=8,
       HTA_NET_ENTITY_MELEE=16, HTA_NET_ENTITY_GRENADE=32 };
typedef struct {
    uint8_t id, kind, flags, weapon, peer_id; /* peer_id 0 for bots */
    float pos[3], velocity[2], yaw, pitch, health, shield;
    int16_t score, kills, deaths;
    char name[HTA_NET_ENTITY_NAME];
    uint8_t carry[2], slot, grenades, powerup; /* carry 255 means empty */
    uint16_t ammo_loaded, ammo_reserve; /* held weapon */
} hta_net_entity;
typedef struct {
    float time;
    uint16_t round;
    uint8_t count, bot_count, over, winner; /* winner 255 means none */
    uint8_t score_limit, time_limit, respawn_time;
    uint8_t item_count, item_present[8], item_choice[64];
    hta_net_entity entities[HTA_NET_MAX_ENTITIES];
} hta_net_world;

/* A player's requested controls, sampled repeatedly. The host applies
 * movement and fire; counters make one-shot actions survive packet loss. */
enum { HTA_NET_JUMP=1, HTA_NET_TRIGGER=2, HTA_NET_DUCK=4, HTA_NET_ALT=8 };
enum { HTA_NET_REJECT_FULL=1, HTA_NET_REJECT_MAP=2 };
typedef struct {
    uint8_t id, flags, weapon_slot;
    float forward, right, yaw, pitch;
    uint16_t melee_count, grenade_count, reload_count, pickup_count;
    uint16_t action_count;    /* get in or out of a vehicle */
} hta_net_control;

/* Every vehicle the host runs, whole, at snapshot rate. Positions are
 * hundredths of a world unit and angles ten-thousandths of a radian on the
 * wire, which keeps 32 of them under the packet cap. */
enum { HTA_NET_VEHICLE_ACTIVE=1, HTA_NET_VEHICLE_GROUNDED=2, HTA_NET_VEHICLE_DRIVEN=4 };
typedef struct {
    uint8_t index, flags;
    float pos[3];
    float yaw, pitch, roll;
    float aim_yaw, aim_pitch;
    float steering, wheel_spin, barrel_spin;
    float speed;                          /* signed, along the hull */
    uint8_t occupant[HTA_NET_VEHICLE_SEATS]; /* unit id, 255 empty */
    float travel[4];                      /* the first four wheels' travel */
} hta_net_vehicle;
typedef struct {
    uint8_t count;
    hta_net_vehicle cars[HTA_NET_MAX_VEHICLES];
} hta_net_vehicles;

typedef struct {
    uint32_t id;
    uint8_t victim, killer; /* killer 255 means environment/suicide */
    char text[96];
} hta_net_kill;

/* HTA_NET_FX_WRECK: a vehicle blew up; `weapon` is the car. */
enum { HTA_NET_FX_FIRE=1, HTA_NET_FX_IMPACT, HTA_NET_FX_DETONATE, HTA_NET_FX_WRECK };
typedef struct {
    uint8_t kind, entity, weapon, material; /* weapon: roster or pool index */
    float pos[3], dir[3];
} hta_net_fx;
typedef struct {
    uint8_t pool, slot;
    float pos[3], dir[3], speed;
} hta_net_projectile;
typedef struct {
    uint8_t count;
    hta_net_projectile live[HTA_NET_MAX_PROJECTILES];
} hta_net_projectiles;

/* All integers and IEEE-754 floats are encoded little-endian; no C struct
 * layout crosses the wire. A decoder rejects nonfinite floats and extra data. */
bool hta_net_pack(uint8_t *dst, size_t cap, uint8_t type, uint32_t seq,
                  uint32_t tick, const uint8_t *payload, uint16_t len,
                  size_t *written);
bool hta_net_unpack(const uint8_t *src, size_t len, hta_net_packet *out);
bool hta_net_player_pack(uint8_t *dst, size_t cap, const hta_net_player *p);
bool hta_net_player_unpack(const uint8_t *src, size_t len, hta_net_player *p);
bool hta_net_event_pack(uint8_t *dst, size_t cap, const hta_net_event *e);
bool hta_net_event_unpack(const uint8_t *src, size_t len, hta_net_event *e);
bool hta_net_info_pack(uint8_t *dst, size_t cap, const hta_net_info *i);
bool hta_net_info_unpack(const uint8_t *src, size_t len, hta_net_info *i);
bool hta_net_world_pack(uint8_t *dst, size_t cap, const hta_net_world *w, size_t *written);
bool hta_net_world_unpack(const uint8_t *src, size_t len, hta_net_world *w);
bool hta_net_control_pack(uint8_t *dst, size_t cap, const hta_net_control *c);
bool hta_net_control_unpack(const uint8_t *src, size_t len, hta_net_control *c);
bool hta_net_kill_pack(uint8_t *dst, size_t cap, const hta_net_kill *k);
bool hta_net_kill_unpack(const uint8_t *src, size_t len, hta_net_kill *k);
bool hta_net_fx_pack(uint8_t *dst, size_t cap, const hta_net_fx *fx);
bool hta_net_fx_unpack(const uint8_t *src, size_t len, hta_net_fx *fx);
bool hta_net_projectiles_pack(uint8_t *dst, size_t cap, const hta_net_projectiles *p,
                              size_t *written);
bool hta_net_projectiles_unpack(const uint8_t *src, size_t len, hta_net_projectiles *p);
/* Weapons lying on the ground: which, where, which way. */
#define HTA_NET_MAX_DROPS 32u
#define HTA_NET_DROP_BYTES 9u
typedef struct {
    uint8_t count;
    struct { uint8_t weapon; float pos[3], yaw; } drop[HTA_NET_MAX_DROPS];
} hta_net_drops;
bool hta_net_drops_pack(uint8_t *dst, size_t cap, const hta_net_drops *d, size_t *written);
bool hta_net_drops_unpack(const uint8_t *src, size_t len, hta_net_drops *d);

bool hta_net_vehicles_pack(uint8_t *dst, size_t cap, const hta_net_vehicles *v,
                           size_t *written);
bool hta_net_vehicles_unpack(const uint8_t *src, size_t len, hta_net_vehicles *v);
void hta_net_u32_write(uint8_t *p, uint32_t v);
uint32_t hta_net_u32_read(const uint8_t *p);

#endif
