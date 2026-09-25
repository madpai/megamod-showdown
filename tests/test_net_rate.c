/* Rate limiting per source address: a flood from one address is dropped
 * before decoding, past its burst; the budget refills with time; a player
 * sending at a real client's pace is never limited; and 0 turns it off. */
#include "net/session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned drain(hta_net_server *s, double now)
{
    uint64_t before = s->stats.packets_in;
    for (int i = 0; i < 64; i++) {
        uint64_t b = s->stats.packets_in;
        hta_net_server_pump(s, now);
        if (s->stats.packets_in == b) break;
    }
    return (unsigned)(s->stats.packets_in - before);
}

static void flood(hta_udp *u, const hta_udp_addr *to, unsigned n)
{
    const uint8_t junk[8] = { 'n', 'o', 'p', 'e', 1, 2, 3, 4 };
    for (unsigned i = 0; i < n; i++) (void)hta_udp_send(u, to, junk, sizeof(junk));
}

int main(void)
{
    static hta_net_server s;
    assert(hta_net_server_open(&s, 0));
    assert(s.rate_per_s == HTA_NET_RATE_PER_S && s.rate_burst == HTA_NET_RATE_BURST);
    /* Small numbers, so the kernel's receive buffer never drops any. */
    s.rate_per_s = 40.0f; s.rate_burst = 60.0f;
    hta_udp_addr to;
    assert(hta_udp_resolve(&to, "127.0.0.1", hta_udp_port(&s.udp)));
    hta_udp attacker;
    assert(hta_udp_open(&attacker, 0));

    /* 150 junk packets at once: the first 60 are read (and are invalid),
     * the other 90 dropped unread. */
    double t = 10.0;
    flood(&attacker, &to, 150);
    unsigned got = drain(&s, t);
    printf("flood: %u read, %llu invalid, %llu limited\n", got,
           (unsigned long long)s.stats.invalid, (unsigned long long)s.stats.limited);
    assert(got == 150);
    assert(s.stats.invalid == 60 && s.stats.limited == 90);

    /* Half a second later, 20 more tokens: 20 through, the rest dropped. */
    flood(&attacker, &to, 50);
    drain(&s, t + 0.5);
    assert(s.stats.invalid == 80 && s.stats.limited == 120);

    /* Refilled, never past the burst. */
    flood(&attacker, &to, 100);
    drain(&s, t + 100.0);
    assert(s.stats.invalid == 140 && s.stats.limited == 160);

    /* A real client at its pace (2 packets every 50 ms) through a full
     * minute, from a fresh source: never limited, and it joins. */
    hta_net_client c;
    assert(hta_net_client_open(&c, "127.0.0.1", hta_udp_port(&s.udp)));
    uint64_t limited = s.stats.limited;
    double now = 200.0;
    for (int f = 0; f < 1200; f++, now += 0.05) {
        hta_net_client_pump(&c, now);
        if (c.connected) {
            hta_net_control ctl;
            memset(&ctl, 0, sizeof(ctl));
            ctl.id = c.id; ctl.loadout[0] = ctl.loadout[1] = 255;
            assert(hta_net_client_control(&c, &ctl));
        }
        hta_net_server_pump(&s, now);
    }
    assert(c.connected);
    assert(s.stats.limited == limited);
    printf("client: joined, %llu packets in over a minute, none limited\n",
           (unsigned long long)s.stats.packets_in);

    /* Off. */
    s.rate_per_s = 0.0f;
    uint64_t inv = s.stats.invalid;
    flood(&attacker, &to, 150);
    drain(&s, now);
    assert(s.stats.limited == limited && s.stats.invalid == inv + 150);

    hta_net_client_close(&c);
    hta_udp_close(&attacker);
    hta_net_server_close(&s);
    printf("net_rate: ok\n");
    return 0;
}
