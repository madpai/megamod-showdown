/* BSP extraction tests against synthetic fixtures. No Halo data required. */
#include "asset/cache.h"
#include "asset/bsp.h"
#include "fixture.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static bool feq(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main(void)
{
    static bsp_fixture bf;
    hta_cache c;
    char err[HTA_ERRLEN];

    printf("bsp extraction tests\n\n[happy path]\n");
    fixture_build_bsp(&bf, 64, 20, 4);
    CHECK(hta_cache_open(&c, bf.f.buf, bf.f.size, err, sizeof(err)),
          "bsp fixture is a valid cache");

    hta_bsp_mesh m;
    bool ok = hta_bsp_load_first(&c, &m, err, sizeof(err));
    if (!ok) printf("  (loader said: %s)\n", err);
    CHECK(ok, "extracts geometry from the BSP");

    if (ok) {
        CHECK(m.vertex_count == 64, "vertex count matches the fixture");
        CHECK(m.index_count == 60, "index count == triangles * 3");
        CHECK(m.submesh_count == 1, "one submesh for one material");
        CHECK(m.materials_seen == 1, "saw exactly one material");
        CHECK(m.materials_skipped_bad == 0, "no materials rejected");

        /* vertices came through with the exact ramp the fixture wrote */
        CHECK(feq(m.vertices[0].pos[0], 0.0f) && feq(m.vertices[0].pos[1], 0.0f),
              "vertex 0 position exact");
        CHECK(feq(m.vertices[10].pos[0], 10.0f) &&
              feq(m.vertices[10].pos[1], 20.0f) &&
              feq(m.vertices[10].pos[2], -5.0f), "vertex 10 position exact");
        CHECK(feq(m.vertices[10].uv[0], 2.5f) && feq(m.vertices[10].uv[1], 7.5f),
              "vertex 10 uv read from offset 48/52");
        CHECK(feq(m.vertices[5].normal[2], 1.0f), "vertex normal read from offset 12");

        /* bounds must span the ramp */
        CHECK(feq(m.bounds_min[0], 0.0f) && feq(m.bounds_max[0], 63.0f),
              "bounds x computed over all vertices");
        CHECK(feq(m.bounds_max[1], 126.0f), "bounds y computed");
        CHECK(feq(m.bounds_min[2], -31.5f), "bounds z computed (negative handled)");

        /* lighting pulled from the sbsp struct */
        CHECK(feq(m.ambient[0], 0.10f) && feq(m.ambient[2], 0.30f), "ambient colour read");
        CHECK(feq(m.light0_color[0], 0.90f), "distant light 0 colour read");
        CHECK(feq(m.light0_dir[2], -1.0f), "distant light 0 direction read");

        /* every index must be in range — this is what keeps the GPU safe */
        bool inrange = true;
        for (uint32_t i = 0; i < m.index_count; i++)
            if (m.indices[i] >= m.vertex_count) { inrange = false; break; }
        CHECK(inrange, "every index is < vertex_count");

        CHECK(m.submeshes[0].index_count == 60, "submesh covers all indices");
        CHECK(m.submeshes[0].shader_tag_id == 0xE1740042u, "submesh carries its shader tag id");
        hta_bsp_free(&m);
        CHECK(m.vertices == NULL && m.vertex_count == 0, "free clears the mesh");
    }

    printf("\n[spawn points]\n");
    static hta_spawn_point sp[16];
    uint32_t n = hta_scenario_spawns(&c, sp, 16);
    CHECK(n == 4, "reads all 4 spawn points");
    CHECK(feq(sp[0].position[0], 10.0f) && feq(sp[0].position[1], 20.0f), "spawn 0 position exact");
    CHECK(feq(sp[2].position[0], 12.0f), "spawn 2 position exact");
    CHECK(feq(sp[0].facing, 1.25f), "spawn facing read");
    CHECK(sp[1].team_index == 1 && sp[0].team_index == 0, "spawn team index read");
    CHECK(hta_scenario_spawns(&c, sp, 2) == 2, "respects the caller's max");

    printf("\n[larger geometry]\n");
    static bsp_fixture big;
    fixture_build_bsp(&big, 1000, 600, 8);
    hta_cache bc;
    CHECK(hta_cache_open(&bc, big.f.buf, big.f.size, err, sizeof(err)), "larger fixture opens");
    hta_bsp_mesh bm;
    bool bok = hta_bsp_load_first(&bc, &bm, err, sizeof(err));
    if (!bok) printf("  (loader said: %s)\n", err);
    CHECK(bok, "extracts 1000 vertices / 600 triangles");
    if (bok) {
        CHECK(bm.vertex_count == 1000 && bm.index_count == 1800, "counts scale correctly");
        hta_bsp_free(&bm);
    }

    printf("\n[malformed BSP rejection]\n");
    static bsp_fixture t;
    hta_cache tc;
    hta_bsp_mesh tm;

    #define REJECT(setup, msg) do { \
        fixture_build_bsp(&t, 32, 10, 2); setup; \
        if (hta_cache_open(&tc, t.f.buf, t.f.size, err, sizeof(err))) { \
            CHECK(!hta_bsp_load_first(&tc, &tm, err, sizeof(err)), msg); \
        } else { CHECK(true, msg " (rejected at cache level)"); } \
    } while (0)

    REJECT(fixture_poke_u32(&t.f, t.bsp_start + 0x14, 0xBADBAD00u),
           "rejects bad 'sbsp' signature in compiled header");
    REJECT(fixture_poke_u32(&t.f, t.bsp_start + 0x00, 0x00000010u),
           "rejects sbsp pointer below the BSP base");
    REJECT(fixture_poke_u32(&t.f, t.bsp_start + 0x08, 0xFFFFFFF0u),
           "rejects vertex pointer outside the BSP region");
    REJECT(fixture_poke_u32(&t.f, t.sbsp_struct_off + HTA_SBSP_SURFACES, 0xFFFFFFFFu),
           "rejects absurd surface count");
    REJECT(fixture_poke_u32(&t.f, t.sbsp_struct_off + HTA_SBSP_LIGHTMAPS, 0u),
           "rejects zero lightmaps");
    REJECT(fixture_poke_u32(&t.f, t.material_off + HTA_MAT_RENDERED_VTX_COUNT, 0xFFFFFF00u),
           "rejects absurd vertex count");
    REJECT(fixture_poke_u32(&t.f, t.material_off + HTA_MAT_RENDERED_VTX_OFFSET, 0xFFFFFF00u),
           "rejects vertex offset past EOF");
    REJECT(fixture_poke_u32(&t.f, t.material_off + HTA_MAT_SURFACE_COUNT, 0xFFFFFFFFu),
           "rejects surface slice beyond the surfaces array");
    REJECT(fixture_poke_u32(&t.f, t.f.tag_data_offset + 0x28 + 0x14,
                            t.f.base_address - 0x1000u),
           "rejects scenario tag_data pointer below base");

    /* compressed-only material: not an error, but yields no geometry */
    fixture_build_bsp(&t, 32, 10, 2);
    fixture_poke_u16(&t.f, t.material_off + HTA_MAT_RENDERED_VTX_TYPE, 1); /* compressed */
    if (hta_cache_open(&tc, t.f.buf, t.f.size, err, sizeof(err))) {
        bool loaded = hta_bsp_load_first(&tc, &tm, err, sizeof(err));
        CHECK(!loaded, "compressed-vertex-only BSP is reported, not silently empty");
        CHECK(strstr(err, "compressed") != NULL, "error message mentions compressed vertices");
        if (loaded) hta_bsp_free(&tm);
    }

    /* a scenario with zero BSPs */
    fixture_build_bsp(&t, 32, 10, 2);
    fixture_poke_u32(&t.f, t.scenario_off + HTA_SCENARIO_STRUCTURE_BSPS_OFF, 0u);
    if (hta_cache_open(&tc, t.f.buf, t.f.size, err, sizeof(err)))
        CHECK(!hta_bsp_load_first(&tc, &tm, err, sizeof(err)), "rejects scenario with no BSPs");

    printf("\n[bit-flip sweep over the BSP region]\n");
    int swept = 0;
    for (uint32_t off = 0; off < 0x400u; off += 11u) {
        fixture_build_bsp(&t, 24, 8, 2);
        t.f.buf[t.bsp_start + off] ^= 0xFFu;
        hta_cache fc;
        if (hta_cache_open(&fc, t.f.buf, t.f.size, err, sizeof(err))) {
            hta_bsp_mesh fm;
            if (hta_bsp_load_first(&fc, &fm, err, sizeof(err))) {
                for (uint32_t i = 0; i < fm.index_count; i++)
                    if (fm.indices[i] >= fm.vertex_count) { failures++; printf("  FAIL: out-of-range index survived\n"); break; }
                hta_bsp_free(&fm);
            }
        }
        swept++;
    }
    checks++;
    printf("  ok:   %d BSP bit-flips handled without crash or bad index\n", swept);

    printf("\n%s — %d checks, %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
