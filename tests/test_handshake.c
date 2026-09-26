/* The v10 join check over localhost: a joiner with the host's map and
 * content gets in; a different map, a different content fingerprint, or a
 * missing map check (0, which slipped through before v10) is refused
 * before it has a player slot; a harness host with no map takes anyone;
 * and malformed HELLOs and REJECTs are dropped without harm. */
#include "net/protocol.h"
#include "net/session.h"
#include "net/udp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hta_net_server srv;
static double now = 1.0;

/* One joiner against the server: connected, or the reason it was refused. */
static int attempt(uint32_t map_crc, uint64_t content)
{
    static hta_net_client c;
    memset(&c, 0, sizeof(c));
    assert(hta_net_client_open(&c, "127.0.0.1", hta_udp_port(&srv.udp)));
    c.map_crc = map_crc;
    c.content = content;
    for (int i = 0; i < 20 && !c.connected && !c.reject_reason; i++, now += 0.3) {
        hta_net_client_pump(&c, now);
        hta_net_server_pump(&srv, now);
        hta_net_client_pump(&c, now + 0.0001);
    }
    int r = c.connected ? 0 : c.reject_reason ? c.reject_reason : -1;
    if (c.connected) {
        /* Leave, so the next joiner has a slot and the count is honest. */
        for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++) srv.peers[i].active = false;
    }
    hta_net_client_close(&c);
    return r;
}

/* Let packets still in flight from earlier joiners (a last ping from a
 * closed client) arrive and be counted, so a measurement sees only its own. */
static void drain(void)
{
    for (int quiet = 0, i = 0; quiet < 20000 && i < 2000000; i++) {
        uint64_t before = srv.stats.packets_in;
        now += 0.00001;
        hta_net_server_pump(&srv, now);
        quiet = srv.stats.packets_in == before ? quiet + 1 : 0;
    }
}

/* A raw packet at the server, as a hostile or broken joiner would send. */
static void raw(uint8_t type, const uint8_t *payload, uint16_t len)
{
    drain();
    hta_udp u;
    assert(hta_udp_open(&u, 0));
    hta_udp_addr to;
    assert(hta_udp_resolve(&to, "127.0.0.1", hta_udp_port(&srv.udp)));
    uint8_t wire[HTA_NET_MAX_PACKET];
    size_t n = 0;
    assert(hta_net_pack(wire, sizeof(wire), type, 1, 0, payload, len, &n));
    uint64_t before = srv.stats.packets_in;
    assert(hta_udp_send(&u, &to, wire, n));
    /* Localhost delivers soon, not instantly: pump until it is read. */
    for (int i = 0; i < 100000 && srv.stats.packets_in == before; i++) {
        now += 0.00001;
        hta_net_server_pump(&srv, now);
    }
    assert(srv.stats.packets_in == before + 1);
    hta_udp_close(&u);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(hta_net_server_open(&srv, 0));
    const uint32_t MAP = 0x1ca8bd70u;
    const uint64_t CONTENT = 0x1e5ae3cc3d6343c2ull;
    srv.map_crc = MAP;
    srv.content = CONTENT;

    /* 1. Same map, same characters and weapons: in. */
    assert(attempt(MAP, CONTENT) == 0);
    /* 2-5. Other characters or weapons (a different set, order or stats all
     * give another fingerprint -- tests/test_compat.c): refused, no slot. */
    assert(attempt(MAP, CONTENT ^ 1u) == HTA_NET_REJECT_CONTENT);
    assert(hta_net_server_count(&srv) == 0);
    assert(attempt(MAP, 0) == HTA_NET_REJECT_CONTENT);
    /* 7. Another map: refused as before; and no map check at all is now
     * refused too (it used to skip the check). */
    assert(attempt(MAP ^ 1u, CONTENT) == HTA_NET_REJECT_MAP);
    assert(attempt(0, CONTENT) == HTA_NET_REJECT_MAP);
    assert(srv.stats.refused == 4);
    /* A harness host that knows no map or content takes anyone. */
    srv.map_crc = 0; srv.content = 0;
    assert(attempt(0x1234u, 0x55ull) == 0);
    srv.map_crc = MAP; srv.content = CONTENT;

    /* 8. Malformed HELLOs: the old 8-byte form, a short one, a long one,
     * and a zero nonce -- all invalid, nobody let in, nothing refused. */
    drain();
    uint64_t invalid = srv.stats.invalid, refused = srv.stats.refused;
    uint8_t hello[17] = { 7, 0, 0, 0 };
    raw(HTA_NET_HELLO, hello, 8);
    raw(HTA_NET_HELLO, hello, 15);
    raw(HTA_NET_HELLO, hello, 17);
    memset(hello, 0, sizeof(hello));
    raw(HTA_NET_HELLO, hello, 16);
    printf("malformed: invalid %llu -> %llu, refused %llu -> %llu, limited %llu\n",
           (unsigned long long)invalid, (unsigned long long)srv.stats.invalid,
           (unsigned long long)refused, (unsigned long long)srv.stats.refused,
           (unsigned long long)srv.stats.limited);
    assert(srv.stats.invalid == invalid + 4 && srv.stats.refused == refused);
    assert(hta_net_server_count(&srv) == 0);

    /* A client ignores a REJECT with a reason it does not know. */
    {
        static hta_net_client c;
        hta_udp fake;
        assert(hta_udp_open(&fake, 0));
        assert(hta_net_client_open(&c, "127.0.0.1", hta_udp_port(&fake)));
        c.map_crc = MAP; c.content = CONTENT;
        hta_net_client_pump(&c, 5.0);                  /* sends its HELLO to `fake` */
        uint8_t buf[HTA_NET_MAX_PACKET]; hta_udp_addr from;
        int got = -1;
        for (int i = 0; i < 1000 && got <= 0; i++) got = hta_udp_recv(&fake, buf, sizeof(buf), &from);
        assert(got > 0);
        hta_net_packet hp;
        assert(hta_net_unpack(buf, (size_t)got, &hp) && hp.type == HTA_NET_HELLO && hp.length == 16);
        uint8_t rej[5];
        memcpy(rej, hp.payload, 4);                    /* its own nonce */
        rej[4] = 99;
        size_t n = 0;
        assert(hta_net_pack(buf, sizeof(buf), HTA_NET_REJECT, 1, 0, rej, 5, &n));
        assert(hta_udp_send(&fake, &from, buf, n));
        for (int i = 0; i < 1000; i++) hta_net_client_pump(&c, 5.01);
        assert(!c.connected && c.reject_reason == 0);
        rej[4] = HTA_NET_REJECT_CONTENT;               /* a known one is taken */
        assert(hta_net_pack(buf, sizeof(buf), HTA_NET_REJECT, 2, 0, rej, 5, &n));
        assert(hta_udp_send(&fake, &from, buf, n));
        for (int i = 0; i < 1000 && !c.reject_reason; i++) hta_net_client_pump(&c, 5.02);
        assert(c.reject_reason == HTA_NET_REJECT_CONTENT);
        hta_net_client_close(&c);
        hta_udp_close(&fake);
    }

    /* WORLD: an item choice past a spawn's 8 is neither sent nor taken. */
    {
        static hta_net_world w, back;
        memset(&w, 0, sizeof(w));
        w.winner = 255; w.item_count = 2; w.item_choice[1] = 7;
        uint8_t buf[HTA_NET_MAX_PACKET];
        size_t n = 0;
        assert(hta_net_world_pack(buf, sizeof(buf), &w, &n));
        assert(hta_net_world_unpack(buf, n, &back) && back.item_choice[1] == 7);
        buf[22 + 1] = 8;                              /* item_choice[1] on the wire */
        assert(!hta_net_world_unpack(buf, n, &back));
        buf[22 + 1] = 7; buf[13] = 65;                /* item_count past 64 */
        assert(!hta_net_world_unpack(buf, n, &back));
        w.item_choice[1] = 8;
        assert(!hta_net_world_pack(buf, sizeof(buf), &w, &n));
    }

    hta_net_server_close(&srv);
    printf("handshake: %llu refused, %llu invalid\n", (unsigned long long)srv.stats.refused,
           (unsigned long long)srv.stats.invalid);
    puts("handshake OK");
    return 0;
}
