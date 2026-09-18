/* Cache parser tests. Uses only synthetic fixtures — no Halo data required. */
#include "asset/cache.h"
#include "fixture.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static bool open_fix(fixture *f, hta_cache *c, char *err, size_t errlen)
{
    return hta_cache_open(c, f->buf, f->size, err, errlen);
}

int main(void)
{
    static fixture f;
    hta_cache c;
    char err[HTA_ERRLEN];

    printf("cache parser tests\n");

    /* ---------- demo (Trial) layout ---------- */
    printf("\n[demo/Trial layout]\n");
    fixture_build(&f, true, 8);
    CHECK(open_fix(&f, &c, err, sizeof(err)), "opens a valid demo-layout cache");
    CHECK(c.is_demo_layout, "detects the demo header permutation");
    CHECK(c.engine == HTA_ENGINE_DEMO, "engine == 6 (CACHE_FILE_DEMO)");
    CHECK(c.base_address == HTA_BASE_GEARBOX_DEMO, "uses Trial base 0x4BF10000, not retail");
    CHECK(strcmp(c.name, "bloodgulch") == 0, "reads map name from 0x58C");
    CHECK(strcmp(c.build, "01.00.00.0576") == 0, "reads build string from 0x2C8");
    CHECK(c.crc32 == 0xDEADBEEFu, "reads crc32 from 0x5B0");
    CHECK(c.map_type == 1, "reads map_type from 0x002");
    CHECK(c.tag_count == 8, "reads tag count");
    CHECK(c.tags_literal == HTA_LIT_TAGS, "validates 'tags' literal");

    /* ---------- retail layout still works ---------- */
    printf("\n[retail layout]\n");
    static fixture rf;
    hta_cache rc;
    fixture_build(&rf, false, 5);
    CHECK(hta_cache_open(&rc, rf.buf, rf.size, err, sizeof(err)), "opens a valid retail-layout cache");
    CHECK(!rc.is_demo_layout, "detects retail header");
    CHECK(rc.engine == HTA_ENGINE_RETAIL, "engine == 7");
    CHECK(rc.base_address == HTA_BASE_GEARBOX_RETAIL, "uses retail base 0x40440000");

    /* ---------- tag access ---------- */
    printf("\n[tag access]\n");
    hta_tag_entry t0, t1;
    CHECK(hta_cache_tag(&c, 0, &t0), "reads tag 0");
    CHECK(t0.primary_class == HTA_TAG_SCNR, "tag 0 is a scenario ('scnr')");
    CHECK(hta_cache_tag(&c, 7, &t1), "reads last tag");
    CHECK(!hta_cache_tag(&c, 8, &t1), "rejects out-of-range tag index");
    CHECK(!hta_cache_tag(&c, 0xFFFFFFFFu, &t1), "rejects absurd tag index");

    char path[128];
    CHECK(hta_cache_tag_path(&c, &t0, path, sizeof(path)), "reads tag path");
    CHECK(strcmp(path, "levels\\test\\fixture_tag_0") == 0, "tag path content is exact");

    CHECK(hta_cache_find_tag_by_class(&c, HTA_TAG_SCNR) == 0, "finds scenario by class");
    CHECK(hta_cache_find_tag_by_class(&c, HTA_TAG_SBSP) >= 0, "finds sbsp by class");
    CHECK(hta_cache_find_tag_by_class(&c, HTA_FOURCC('z','z','z','z')) == -1, "missing class returns -1");
    CHECK(hta_cache_find_tag_by_id(&c, c.scenario_tag_id) == 0, "finds scenario by tag id");
    CHECK(hta_cache_find_tag_by_id(&c, 0x0BADF00Du) == -1, "unknown tag id returns -1");

    /* ---------- pointer translation ---------- */
    printf("\n[pointer translation]\n");
    uint32_t off = 0;
    CHECK(hta_cache_ptr_to_offset(&c, c.base_address, &off) && off == c.tag_data_offset,
          "base address maps to tag_data_offset");
    CHECK(hta_cache_ptr_to_offset(&c, c.base_address + 0x30, &off) && off == c.tag_data_offset + 0x30,
          "offset within region translates correctly");
    CHECK(!hta_cache_ptr_to_offset(&c, c.base_address - 1, &off), "pointer below base rejected");
    CHECK(!hta_cache_ptr_to_offset(&c, c.base_address + c.tag_data_size, &off),
          "pointer past region end rejected");
    CHECK(!hta_cache_ptr_to_offset(&c, 0, &off), "null pointer rejected");
    CHECK(!hta_cache_ptr_to_offset(&c, 0xFFFFFFFFu, &off), "0xFFFFFFFF rejected");
    /* the generic translator must not wrap */
    CHECK(!hta_translate(0xFFFFFFFFu, 0, 0xFFFFFFF0u, 0xFFFFFFFFu, &off),
          "translation cannot overflow to a small offset");

    /* ---------- bounds-checked readers ---------- */
    printf("\n[bounds checking]\n");
    uint32_t v32; uint16_t v16;
    CHECK(hta_rd_u32(&c, 0, &v32), "reads at offset 0");
    CHECK(!hta_rd_u32(&c, (uint32_t)c.size - 2, &v32), "4-byte read straddling EOF rejected");
    CHECK(!hta_rd_u16(&c, (uint32_t)c.size - 1, &v16), "2-byte read straddling EOF rejected");
    CHECK(!hta_rd_u32(&c, 0xFFFFFFFCu, &v32), "read at 0xFFFFFFFC cannot wrap");
    CHECK(!hta_rd_bytes(&c, 0, path, 0xFFFFFFFFu), "huge length rejected");

    /* ---------- rejection of malformed files ---------- */
    printf("\n[malformed input rejection]\n");
    static fixture b;

    CHECK(!hta_cache_open(&c, f.buf, 4, err, sizeof(err)), "rejects truncated file");
    CHECK(!hta_cache_open(&c, NULL, 100, err, sizeof(err)), "rejects NULL data");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x2C0, 0x11111111u);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects bad head literal");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x5F0, 0x22222222u);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects bad foot literal");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x588, 999u);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects unsupported engine");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x800 + 0x24, 0x33333333u);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects bad 'tags' literal");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x800 + 0x0C, 0xFFFFFFFFu);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects absurd tag count");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x800 + 0x0C, 0u);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects zero tag count");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x800 + 0x00, 0x00001000u);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects tag array ptr below base");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x5EC, 0x00F00000u);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects tag data offset past EOF");

    fixture_build(&b, true, 4); fixture_poke_u32(&b, 0x2C4, 0x7FFFFFFFu);
    CHECK(!hta_cache_open(&c, b.buf, b.size, err, sizeof(err)), "rejects oversized tag data region");

    CHECK(err[0] != 0, "failures produce a human-readable reason");

    /* ---------- fuzz: must never crash or hang ---------- */
    printf("\n[fuzz]\n");
    static uint8_t junk[8192];
    unsigned seed = 12345u;
    int accepted = 0;
    for (int iter = 0; iter < 4000; iter++) {
        for (size_t i = 0; i < sizeof(junk); i++) {
            seed = seed * 1103515245u + 12345u;
            junk[i] = (uint8_t)(seed >> 16);
        }
        hta_cache jc;
        if (hta_cache_open(&jc, junk, sizeof(junk), err, sizeof(err))) accepted++;
    }
    CHECK(accepted == 0, "4000 random buffers all rejected, no crash");

    /* fuzz by corrupting a VALID fixture one field at a time: the parser must
     * either accept coherently or reject, never read out of bounds. */
    int survived = 0;
    for (uint32_t byte = 0; byte < 0x800; byte += 7) {
        fixture_build(&b, true, 6);
        b.buf[byte] ^= 0xFFu;
        hta_cache jc;
        if (hta_cache_open(&jc, b.buf, b.size, err, sizeof(err))) {
            /* if accepted, every tag must still be readable safely */
            for (uint32_t i = 0; i < jc.tag_count; i++) {
                hta_tag_entry te;
                if (hta_cache_tag(&jc, i, &te)) hta_cache_tag_path(&jc, &te, path, sizeof(path));
            }
        }
        survived++;
    }
    CHECK(survived == 293, "header bit-flip sweep completed without crashing");

    printf("\n%s — %d checks, %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
