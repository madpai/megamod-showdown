#ifndef HTA_NET_UDP_H
#define HTA_NET_UDP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

typedef struct { int fd; } hta_udp;
typedef struct { struct sockaddr_storage addr; socklen_t len; } hta_udp_addr;
bool hta_udp_open(hta_udp *s, uint16_t port);
bool hta_udp_resolve(hta_udp_addr *a, const char *host, uint16_t port);
void hta_udp_close(hta_udp *s);
/* -1 means no packet or socket error; callers drain a bounded number/frame. */
int hta_udp_recv(hta_udp *s, uint8_t *buf, size_t cap, hta_udp_addr *from);
bool hta_udp_send(hta_udp *s, const hta_udp_addr *to, const uint8_t *buf, size_t len);
bool hta_udp_addr_equal(const hta_udp_addr *a, const hta_udp_addr *b);
uint16_t hta_udp_port(const hta_udp *s);
/* Let this socket send to a broadcast address. */
bool hta_udp_broadcast(hta_udp *s);
/* The dotted IPv4 and port of an address; false if it is not IPv4. */
bool hta_udp_addr_ip(const hta_udp_addr *a, char out[16], uint16_t *port);
#endif
