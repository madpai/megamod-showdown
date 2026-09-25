/* Every LAN decoder against garbage: random bytes, random lengths, and
 * valid packets with bits flipped. LAN packets come from other devices,
 * so a decoder must refuse anything malformed without reading past what
 * it was given. Run it under -fsanitize=address to mean it (build-asan).
 * Whatever a decoder accepts must re-encode to the same bytes: one wire
 * form per value, so nothing smuggles state through "don't care" bits. */
#include "net/protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng = 0xC0FFEEu;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* Each case gets its own heap buffer of exactly `len` bytes, so reading
 * one byte past the end is an ASan error, not a silent read. */
static uint8_t *exact(const uint8_t *src, size_t len)
{
    uint8_t *p = malloc(len ? len : 1);
    if (len) memcpy(p, src, len);
    return p;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    static uint8_t buf[HTA_NET_MAX_PACKET + 64], again[HTA_NET_MAX_PACKET + 64];
    static hta_net_world w; static hta_net_game g; static hta_net_kill k; static hta_net_fx fx;
    static hta_net_projectiles pr; static hta_net_vehicles v; static hta_net_drops d;
    static hta_net_player pl; static hta_net_event ev; static hta_net_info in; static hta_net_control ct;
    hta_net_packet pk;
    const size_t fixed[] = { HTA_NET_KILL_BYTES, HTA_NET_FX_BYTES, HTA_NET_GAME_BYTES, HTA_NET_CONTROL_BYTES,
                             HTA_NET_PLAYER_BYTES };
    unsigned accepted[16] = { 0 };
    for (int iter = 0; iter < 400000; iter++) {
        size_t len;
        int mode = iter % 3;
        if (mode == 0) len = rnd() % (HTA_NET_MAX_PACKET + 32);
        else len = (size_t)((long)fixed[rnd() % 5] + (mode == 2 ? (long)(rnd() % 3) - 1 : 0));
        /* Mostly small values: garbage that is close to plausible reaches
         * deeper into a decoder than uniform noise. */
        for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(rnd() % 7 == 0 ? rnd() : rnd() % 4);
        uint8_t *p = exact(buf, len);
        if (hta_net_unpack(p, len, &pk)) accepted[0]++;
        if (hta_net_world_unpack(p, len, &w)) accepted[1]++;
        if (hta_net_game_unpack(p, len, &g)) {
            accepted[2]++;
            assert(hta_net_game_pack(again, sizeof(again), &g) && !memcmp(again, p, len));
        }
        if (hta_net_kill_unpack(p, len, &k)) {
            accepted[3]++;
            assert(hta_net_kill_pack(again, sizeof(again), &k) && !memcmp(again, p, len));
        }
        if (hta_net_fx_unpack(p, len, &fx)) {
            accepted[4]++;
            assert(hta_net_fx_pack(again, sizeof(again), &fx) && !memcmp(again, p, len));
        }
        if (hta_net_projectiles_unpack(p, len, &pr)) accepted[5]++;
        if (hta_net_vehicles_unpack(p, len, &v)) accepted[6]++;
        if (hta_net_drops_unpack(p, len, &d)) accepted[7]++;
        if (hta_net_player_unpack(p, len, &pl)) accepted[8]++;
        if (hta_net_event_unpack(p, len, &ev)) accepted[9]++;
        if (hta_net_info_unpack(p, len, &in)) accepted[10]++;
        if (hta_net_control_unpack(p, len, &ct)) accepted[11]++;
        free(p);
    }
    /* Valid packets, one bit flipped: decoders either refuse them or accept
     * a canonical one. */
    hta_net_game gm;
    memset(&gm, 0, sizeof(gm));
    gm.mode = 2; gm.prop_count = 300; gm.prop_broken[3] = 0x5A; gm.winner_team = 255;
    gm.flag[0].carrier = gm.flag[1].carrier = 255;
    uint8_t good[HTA_NET_GAME_BYTES];
    assert(hta_net_game_pack(good, sizeof(good), &gm));
    unsigned flips = 0, kept = 0;
    for (size_t bit = 0; bit < sizeof(good) * 8; bit++) {
        uint8_t *p = exact(good, sizeof(good));
        p[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        if (hta_net_game_unpack(p, sizeof(good), &g)) {
            kept++;
            assert(hta_net_game_pack(again, sizeof(again), &g) && !memcmp(again, p, sizeof(good)));
        }
        flips++;
        free(p);
    }
    printf("net fuzz: 400000 random cases; accepted: header %u, world %u, game %u, kill %u, fx %u, "
           "projectiles %u, vehicles %u, drops %u, player %u, event %u, info %u, control %u; "
           "%u/%u single-bit flips of a GAME still valid (and canonical)\n",
           accepted[0], accepted[1], accepted[2], accepted[3], accepted[4], accepted[5], accepted[6],
           accepted[7], accepted[8], accepted[9], accepted[10], accepted[11], kept, flips);
    puts("net fuzz OK");
    return 0;
}
