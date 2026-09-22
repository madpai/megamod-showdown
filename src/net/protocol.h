#ifndef HTA_NET_PROTOCOL_H
#define HTA_NET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_NET_MAGIC 0x31415448u /* "HTA1" on the wire */
#define HTA_NET_VERSION 1u
#define HTA_NET_HEADER 20u
#define HTA_NET_MAX_PACKET 1200u
#define HTA_NET_MAX_PLAYERS 8u
#define HTA_NET_PLAYER_BYTES 35u

typedef enum {
    HTA_NET_HELLO = 1, HTA_NET_WELCOME, HTA_NET_DISCONNECT,
    HTA_NET_PING, HTA_NET_PONG, HTA_NET_INPUT, HTA_NET_SNAPSHOT,
    HTA_NET_EVENT
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
void hta_net_u32_write(uint8_t *p, uint32_t v);
uint32_t hta_net_u32_read(const uint8_t *p);

#endif
