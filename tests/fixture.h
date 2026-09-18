/* Builds synthetic Halo cache files in memory so the parser can be tested
 * without any proprietary Halo data. Never ships in the engine. */
#ifndef HTA_FIXTURE_H
#define HTA_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIX_MAX (8 * 1024 * 1024)

typedef struct {
    uint8_t  buf[FIX_MAX];
    size_t   size;
    /* recorded so tests can assert against what was built */
    uint32_t base_address;
    uint32_t tag_data_offset;
    uint32_t tag_data_size;
    uint32_t tag_count;
    uint32_t scenario_tag_id;
} fixture;

/* Build a minimal but structurally valid cache.
 * demo=true  -> Trial header permutation + 'Ehed'/'Gfot' + engine 6 + base 0x4BF10000
 * demo=false -> retail layout + 'head'/'foot' + engine 7 + base 0x40440000
 * Contains `ntags` tags: index 0 is a scenario ('scnr'), then sbsp, bitm, senv
 * cycling, each with a readable path string. */
void fixture_build(fixture *f, bool demo, uint32_t ntags);

/* helpers to corrupt a built fixture */
void fixture_poke_u32(fixture *f, uint32_t off, uint32_t v);
void fixture_poke_u16(fixture *f, uint32_t off, uint16_t v);


/* ---- BSP fixture ---- */
#define FIX_BSP_BASE 0x50000000u

typedef struct {
    fixture  f;
    uint32_t bsp_start;
    uint32_t bsp_size;
    uint32_t bsp_address;
    uint32_t sbsp_struct_off;   /* file offset of the ScenarioStructureBSP */
    uint32_t vertex_block_off;  /* file offset of the vertex data */
    uint32_t material_off;      /* file offset of material 0 */
    uint32_t scenario_off;      /* file offset of the Scenario struct */
    uint32_t vertex_count;
    uint32_t triangle_count;
    uint32_t spawn_count;
} bsp_fixture;

/* Builds a demo-layout cache containing one scenario, one structure BSP with
 * `nverts` uncompressed environment vertices and `ntris` triangles, plus
 * `nspawns` player spawn points. */
void fixture_build_bsp(bsp_fixture *b, uint32_t nverts, uint32_t ntris, uint32_t nspawns);

/* Builds a BSP whose geometry is an nx-by-ny heightfield grid — real,
 * non-degenerate triangles with area, so it can actually be rasterised.
 * fixture_build_bsp's ramp is collinear by design (exact values are easy to
 * assert) and therefore renders nothing; use this one for renderer tests.
 * Requires nx*ny <= 65535 because per-material indices are 16-bit. */
void fixture_build_grid(bsp_fixture *b, uint32_t nx, uint32_t ny, uint32_t nspawns);

#endif
