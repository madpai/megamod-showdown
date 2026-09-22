#include "udp.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

bool hta_udp_open(hta_udp *s, uint16_t port)
{
    if (!s) return false;
    s->fd=-1;
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if (fd<0) return false;
    int flags=fcntl(fd,F_GETFL,0);
    struct sockaddr_in bind_addr={0};
    bind_addr.sin_family=AF_INET; bind_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    bind_addr.sin_port=htons(port);
    if (flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0 ||
        bind(fd,(struct sockaddr *)&bind_addr,sizeof(bind_addr))<0) {
        close(fd); return false;
    }
    s->fd=fd; return true;
}
bool hta_udp_resolve(hta_udp_addr *a, const char *host, uint16_t port)
{
    if (!a || !host || !port) return false;
    /* Numeric IPv4 only for now: no DNS lookup can stall the render thread. */
    struct sockaddr_in v4={0};
    v4.sin_family=AF_INET; v4.sin_port=htons(port);
    if (inet_pton(AF_INET,host,&v4.sin_addr)!=1) return false;
    memset(a,0,sizeof(*a)); memcpy(&a->addr,&v4,sizeof(v4)); a->len=sizeof(v4);
    return true;
}
void hta_udp_close(hta_udp *s) { if (s && s->fd>=0) { close(s->fd); s->fd=-1; } }
int hta_udp_recv(hta_udp *s, uint8_t *buf, size_t cap, hta_udp_addr *from)
{
    if (!s || s->fd<0 || !buf || !from || cap==0) return -1;
    from->len=sizeof(from->addr);
    ssize_t n=recvfrom(s->fd,buf,cap,0,(struct sockaddr *)&from->addr,&from->len);
    return n<0 ? -1 : (int)n;
}
bool hta_udp_send(hta_udp *s, const hta_udp_addr *to, const uint8_t *buf, size_t len)
{ return s && s->fd>=0 && to && buf && len &&
    sendto(s->fd,buf,len,0,(const struct sockaddr *)&to->addr,to->len)==(ssize_t)len; }
bool hta_udp_addr_equal(const hta_udp_addr *a, const hta_udp_addr *b)
{
    if (!a || !b || a->addr.ss_family!=AF_INET || b->addr.ss_family!=AF_INET) return false;
    const struct sockaddr_in *x=(const struct sockaddr_in *)&a->addr;
    const struct sockaddr_in *y=(const struct sockaddr_in *)&b->addr;
    return x->sin_port==y->sin_port && x->sin_addr.s_addr==y->sin_addr.s_addr;
}
uint16_t hta_udp_port(const hta_udp *s)
{
    if (!s || s->fd<0) return 0;
    struct sockaddr_in a; socklen_t n=sizeof(a);
    return getsockname(s->fd,(struct sockaddr *)&a,&n)<0 ? 0 : ntohs(a.sin_port);
}
bool hta_udp_broadcast(hta_udp *s)
{
    int on=1;
    return s && s->fd>=0 && setsockopt(s->fd,SOL_SOCKET,SO_BROADCAST,&on,sizeof(on))==0;
}
bool hta_udp_addr_ip(const hta_udp_addr *a, char out[16], uint16_t *port)
{
    if (!a || !out || a->addr.ss_family!=AF_INET) return false;
    const struct sockaddr_in *v4=(const struct sockaddr_in *)&a->addr;
    if (!inet_ntop(AF_INET,&v4->sin_addr,out,16)) return false;
    if (port) *port=ntohs(v4->sin_port);
    return true;
}
