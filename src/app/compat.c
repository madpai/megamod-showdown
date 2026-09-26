/* The imported rosters' gameplay fingerprint (app/compat.h).
 *
 * Encoding, fixed (HTA_CONTENT_SCHEMA 1):
 * - the stream starts with the schema number, then the character roster,
 *   then the weapon roster, each as a u32 count followed by its entries in
 *   roster order (the order the network indexes);
 * - an entry is its fields in the order below, each tagged with a one-byte
 *   field number so a moved or missing field cannot alias another;
 * - integers and booleans: little-endian u32 (booleans 0/1);
 * - floats: rounded to 1/10000 and written as a little-endian i32
 *   (values beyond +-200000 are clamped; NaN is written as INT32_MIN), so
 *   ARM and x86 agree however the manifest's decimals parsed;
 * - strings: bytes up to the first NUL, length-prefixed (u32), as stored --
 *   no case folding: a renamed label is a different label, and loadouts
 *   match labels exactly.
 * FNV-1a 64 over that stream; 0 is replaced by 1. */
#include "app/compat.h"
#include <math.h>
#include <string.h>

typedef struct { uint64_t h; } fnv;

static void bytes(fnv *f, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { f->h ^= b[i]; f->h *= 1099511628211ull; }
}

static void u32(fnv *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    bytes(f, b, 4);
}

static void tag(fnv *f, uint8_t t) { bytes(f, &t, 1); }

static void num(fnv *f, uint8_t t, float x)
{
    int32_t q;
    if (isnan(x)) q = INT32_MIN;
    else {
        double d = (double)x * 10000.0;
        if (d > 2e9) d = 2e9;
        if (d < -2e9) d = -2e9;
        q = (int32_t)lround(d);
    }
    tag(f, t); u32(f, (uint32_t)q);
}

static void integer(fnv *f, uint8_t t, int32_t v) { tag(f, t); u32(f, (uint32_t)v); }
static void flag(fnv *f, uint8_t t, bool v) { tag(f, t); u32(f, v ? 1u : 0u); }

static void text(fnv *f, uint8_t t, const char *s, size_t cap)
{
    size_t n = 0;
    while (n < cap && s[n]) n++;
    tag(f, t); u32(f, (uint32_t)n); bytes(f, s, n);
}

#define TEXT(t, field) text(f, t, a->field, sizeof(a->field))

/* Every field the game plays by. Left out on purpose (cosmetic): models,
 * sounds, view_mirrored, mount_offset/yaw/pitch, hold_type, crosshair,
 * crosshair_size, ability_color. */
static void asset(fnv *f, const hta_oal_asset *a)
{
    TEXT(1, kind); TEXT(2, name); TEXT(3, display); TEXT(4, base);
    num(f, 5, a->rounds_per_second); num(f, 6, a->damage_scale); num(f, 7, a->spread_scale);
    integer(f, 8, a->magazine); integer(f, 9, a->reserve);
    flag(f, 10, a->melee); flag(f, 11, a->mount); num(f, 12, a->fly_speed);
    integer(f, 13, a->reload_rounds); num(f, 14, a->reload_seconds); num(f, 15, a->recharge);
    TEXT(16, loadout[0]); TEXT(17, loadout[1]);
    num(f, 18, a->body_health); num(f, 19, a->body_shield); num(f, 20, a->body_damage);
    num(f, 21, a->body_speed); flag(f, 22, a->can_fly); num(f, 23, a->fly_damage);
    TEXT(24, hero_group); integer(f, 25, a->unique_limit);
    TEXT(26, ability_name); TEXT(27, ability_base);
    num(f, 28, a->ability_damage); num(f, 29, a->ability_cooldown); num(f, 30, a->ability_duration);
    num(f, 31, a->ability_interval); num(f, 32, a->ability_radius); num(f, 33, a->ability_force);
    num(f, 34, a->ability_cone); flag(f, 35, a->ability_beam); num(f, 36, a->knockback);
    tag(f, 0xFF);
}

uint64_t hta_content_fingerprint(const hta_oal_asset *chars, uint32_t char_count,
                                 const hta_oal_asset *weaps, uint32_t weap_count)
{
    fnv f = { 14695981039346656037ull };
    u32(&f, HTA_CONTENT_SCHEMA);
    u32(&f, char_count);
    for (uint32_t i = 0; chars && i < char_count; i++) asset(&f, &chars[i]);
    u32(&f, weap_count);
    for (uint32_t i = 0; weaps && i < weap_count; i++) asset(&f, &weaps[i]);
    return f.h ? f.h : 1u;
}
