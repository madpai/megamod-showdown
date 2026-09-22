#ifndef HTA_NET_SESSION_H
#define HTA_NET_SESSION_H
#include "protocol.h"
#include "udp.h"

typedef struct {
    uint64_t packets_in, packets_out, bytes_in, bytes_out;
    uint64_t invalid, dropped, snapshots_in, snapshots_out, events_in, events_out;
    double ping_ms;
} hta_net_stats;

typedef struct {
    bool active;
    bool has_state;
    hta_udp_addr addr;
    uint32_t token, nonce, last_sequence;
    double last_seen;
    hta_net_player player;
} hta_net_peer;

typedef struct {
    hta_udp udp;
    hta_net_peer peers[HTA_NET_MAX_PLAYERS];
    uint32_t sequence, tick;
    double last_snapshot;
    hta_net_stats stats;
} hta_net_server;

typedef struct {
    hta_udp udp;
    hta_udp_addr server;
    uint32_t sequence, nonce, token;
    uint32_t last_snapshot_tick;
    uint8_t id;
    bool connected;
    double last_hello, last_ping, ping_sent;
    uint32_t ping_nonce;
    hta_net_player players[HTA_NET_MAX_PLAYERS];
    bool present[HTA_NET_MAX_PLAYERS];
    hta_net_event events[32];
    unsigned event_count;
    hta_net_stats stats;
} hta_net_client;

bool hta_net_server_open(hta_net_server *s, uint16_t port);
void hta_net_server_close(hta_net_server *s);
/* Nonblocking bounded drain; now is monotonic seconds supplied by caller. */
void hta_net_server_pump(hta_net_server *s, double now);
unsigned hta_net_server_count(const hta_net_server *s);

bool hta_net_client_open(hta_net_client *c, const char *ip, uint16_t port);
void hta_net_client_close(hta_net_client *c);
void hta_net_client_pump(hta_net_client *c, double now);
bool hta_net_client_state(hta_net_client *c, const hta_net_player *p);
bool hta_net_client_event(hta_net_client *c, const hta_net_event *e);
/* First queued event is removed; snapshots remain in players[]. */
bool hta_net_client_pop_event(hta_net_client *c, hta_net_event *e);
#endif
