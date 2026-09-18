#include "fixture.h"
#include <string.h>
#include <stdio.h>

#define HEADER 0x800u

static void w32(fixture *f, uint32_t off, uint32_t v)
{
    if ((size_t)off + 4 > FIX_MAX) return;
    f->buf[off+0] = (uint8_t)(v & 0xFF);
    f->buf[off+1] = (uint8_t)((v >> 8) & 0xFF);
    f->buf[off+2] = (uint8_t)((v >> 16) & 0xFF);
    f->buf[off+3] = (uint8_t)((v >> 24) & 0xFF);
}
static void w16(fixture *f, uint32_t off, uint16_t v)
{
    if ((size_t)off + 2 > FIX_MAX) return;
    f->buf[off+0] = (uint8_t)(v & 0xFF);
    f->buf[off+1] = (uint8_t)((v >> 8) & 0xFF);
}
static void wstr(fixture *f, uint32_t off, const char *s)
{
    size_t n = strlen(s);
    if (n > 31) n = 31;
    if ((size_t)off + 32 > FIX_MAX) return;
    memset(f->buf + off, 0, 32);
    memcpy(f->buf + off, s, n);
}

void fixture_poke_u32(fixture *f, uint32_t off, uint32_t v) { w32(f, off, v); }
void fixture_poke_u16(fixture *f, uint32_t off, uint16_t v) { w16(f, off, v); }

void fixture_build(fixture *f, bool demo, uint32_t ntags)
{
    memset(f, 0, sizeof(*f));
    if (ntags < 1) ntags = 1;

    const uint32_t base = demo ? 0x4BF10000u : 0x40440000u;
    const uint32_t tag_data_offset = HEADER;

    /* Layout inside the tag data region:
     *   +0x00            tag data header (0x28)
     *   +0x28            tag array (ntags * 0x20)
     *   after that       path strings, then a little tag data
     */
    const uint32_t array_rel = 0x28u;
    const uint32_t array_bytes = ntags * 0x20u;
    uint32_t paths_rel = array_rel + array_bytes;

    /* --- tag array + paths --- */
    static const uint32_t classes[4] = {
        0x73636E72u, /* scnr */
        0x73627370u, /* sbsp */
        0x6269746Du, /* bitm */
        0x73656E76u, /* senv */
    };

    uint32_t cursor = paths_rel;
    for (uint32_t i = 0; i < ntags; i++) {
        uint32_t e = tag_data_offset + array_rel + i * 0x20u;
        uint32_t cls = (i == 0) ? classes[0] : classes[1 + (i - 1) % 3];
        uint32_t tag_id = 0xE1740000u | i;   /* arbitrary high word + index */

        char path[64];
        snprintf(path, sizeof(path), "levels\\test\\fixture_tag_%u", i);
        uint32_t plen = (uint32_t)strlen(path);

        uint32_t path_rel = cursor;
        if ((size_t)tag_data_offset + path_rel + plen + 1 < FIX_MAX)
            memcpy(f->buf + tag_data_offset + path_rel, path, plen + 1);
        cursor += plen + 1;

        w32(f, e + 0x00, cls);
        w32(f, e + 0x04, 0xFFFFFFFFu);
        w32(f, e + 0x08, 0xFFFFFFFFu);
        w32(f, e + 0x0C, tag_id);
        w32(f, e + 0x10, base + path_rel);        /* tag_path pointer */
        w32(f, e + 0x14, base + array_rel);       /* tag_data: point somewhere valid */
        w32(f, e + 0x18, 0);                      /* not indexed */
        w32(f, e + 0x1C, 0);
        if (i == 0) f->scenario_tag_id = tag_id;
    }

    uint32_t tag_data_size = cursor + 0x40u;      /* a little slack */

    /* --- tag data header --- */
    w32(f, tag_data_offset + 0x00, base + array_rel); /* tag_array_address */
    w32(f, tag_data_offset + 0x04, f->scenario_tag_id);
    w32(f, tag_data_offset + 0x08, 0x12345678u);      /* checksums */
    w32(f, tag_data_offset + 0x0C, ntags);
    w32(f, tag_data_offset + 0x10, 0);                /* model part count */
    w32(f, tag_data_offset + 0x14, 0);                /* model data file offset */
    w32(f, tag_data_offset + 0x18, 0);
    w32(f, tag_data_offset + 0x1C, 56);               /* vertex size */
    w32(f, tag_data_offset + 0x20, 0);                /* model data size */
    w32(f, tag_data_offset + 0x24, 0x74616773u);      /* "tags" */

    /* --- file header --- */
    uint32_t total = tag_data_offset + tag_data_size;
    if (demo) {
        w16(f, 0x002, 1);                  /* map_type: multiplayer */
        w32(f, 0x2C0, 0x45686564u);        /* "Ehed" */
        w32(f, 0x2C4, tag_data_size);
        wstr(f, 0x2C8, "01.00.00.0576");
        w32(f, 0x588, 6);                  /* engine = CACHE_FILE_DEMO */
        wstr(f, 0x58C, "bloodgulch");
        w32(f, 0x5B0, 0xDEADBEEFu);        /* crc32 */
        w32(f, 0x5E8, total);
        w32(f, 0x5EC, tag_data_offset);
        w32(f, 0x5F0, 0x47666F74u);        /* "Gfot" */
    } else {
        w32(f, 0x000, 0x68656164u);        /* "head" */
        w32(f, 0x004, 7);                  /* engine = retail */
        w32(f, 0x008, total);
        w32(f, 0x010, tag_data_offset);
        w32(f, 0x014, tag_data_size);
        wstr(f, 0x020, "bloodgulch");
        wstr(f, 0x040, "01.00.00.0564");
        w16(f, 0x060, 1);
        w32(f, 0x064, 0xDEADBEEFu);
        w32(f, 0x7FC, 0x666F6F74u);        /* "foot" */
    }

    f->size            = total;
    f->base_address    = base;
    f->tag_data_offset = tag_data_offset;
    f->tag_data_size   = tag_data_size;
    f->tag_count       = ntags;
}

/* ------------------------------------------------------------------ */
/* BSP fixture                                                         */
/* ------------------------------------------------------------------ */

static void wf32(fixture *f, uint32_t off, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    w32(f, off, u);
}

#define SCENARIO_SIZE       1456u
#define SCN_SPAWNS_OFF      0x354u
#define SCN_BSPS_OFF        0x5A4u
#define SBSP_SIZE           648u
#define SBSP_AMBIENT        0x02Cu
#define SBSP_L0_COLOR       0x03Cu
#define SBSP_L0_DIR         0x048u
#define SBSP_SURFACES       0x0ECu
#define SBSP_LIGHTMAPS      0x0F8u
#define LIGHTMAP_SIZE       32u
#define LIGHTMAP_MATERIALS  0x014u
#define MATERIAL_SIZE       256u
#define MAT_SHADER          0x000u
#define MAT_SURFACES        0x014u
#define MAT_SURFACE_COUNT   0x018u
#define MAT_VTX_TYPE        0x0B0u
#define MAT_VTX_COUNT       0x0B4u
#define MAT_VTX_OFFSET      0x0B8u
#define VERTEX_SIZE         56u

static void wrefl(fixture *f, uint32_t off, uint32_t count, uint32_t ptr)
{
    w32(f, off + 0, count);
    w32(f, off + 4, ptr);
    w32(f, off + 8, 0);
}

void fixture_build_bsp(bsp_fixture *b, uint32_t nverts, uint32_t ntris, uint32_t nspawns)
{
    memset(b, 0, sizeof(*b));
    fixture *f = &b->f;

    /* start from the plain 2-tag demo fixture, then extend it */
    const uint32_t ntags = 2;              /* 0 = scnr, 1 = sbsp */
    fixture_build(f, true, ntags);

    const uint32_t base = f->base_address;
    const uint32_t tdo  = f->tag_data_offset;

    /* --- append structures to the tag data region --- */
    uint32_t cur_rel = f->tag_data_size;   /* relative to tdo */
    cur_rel = (cur_rel + 15u) & ~15u;

    uint32_t scenario_rel = cur_rel;                  cur_rel += SCENARIO_SIZE;
    uint32_t bsps_rel     = cur_rel;                  cur_rel += 32u;
    uint32_t spawns_rel   = cur_rel;                  cur_rel += (nspawns ? nspawns * 52u : 4u);
    uint32_t new_tag_data_size = cur_rel + 16u;

    b->scenario_off = tdo + scenario_rel;

    /* scenario -> structure_bsps (1 entry) and player spawns */
    wrefl(f, tdo + scenario_rel + SCN_BSPS_OFF, 1, base + bsps_rel);
    wrefl(f, tdo + scenario_rel + SCN_SPAWNS_OFF, nspawns, base + spawns_rel);

    for (uint32_t i = 0; i < nspawns; i++) {
        uint32_t e = tdo + spawns_rel + i * 52u;
        wf32(f, e + 0x00, 10.0f + (float)i);
        wf32(f, e + 0x04, 20.0f + (float)i);
        wf32(f, e + 0x08, 0.5f);
        wf32(f, e + 0x0C, 1.25f);
        w16 (f, e + 0x10, (uint16_t)(i & 1));      /* team */
        w16 (f, e + 0x12, 0);                      /* bsp index */
    }

    /* point tag 0's tag_data at the scenario struct */
    uint32_t tag0 = tdo + 0x28u + 0 * 0x20u;
    w32(f, tag0 + 0x14, base + scenario_rel);

    /* --- BSP region, placed after the tag data region --- */
    uint32_t bsp_start = tdo + new_tag_data_size;
    bsp_start = (bsp_start + 15u) & ~15u;
    const uint32_t bsp_address = FIX_BSP_BASE;

    uint32_t r = 0;
    uint32_t hdr_rel        = r; r += 0x18u;
    uint32_t sbsp_rel       = r; r += SBSP_SIZE;
    uint32_t surfaces_rel   = r; r += (ntris ? ntris * 6u : 6u);
    surfaces_rel = surfaces_rel;
    r = (r + 3u) & ~3u;
    uint32_t lightmaps_rel  = r; r += LIGHTMAP_SIZE;
    uint32_t materials_rel  = r; r += MATERIAL_SIZE;
    uint32_t vertices_rel   = r; r += (nverts ? nverts * VERTEX_SIZE : VERTEX_SIZE);
    uint32_t bsp_size = r + 16u;

    b->bsp_start        = bsp_start;
    b->bsp_size         = bsp_size;
    b->bsp_address      = bsp_address;
    b->sbsp_struct_off  = bsp_start + sbsp_rel;
    b->vertex_block_off = bsp_start + vertices_rel;
    b->material_off     = bsp_start + materials_rel;
    b->vertex_count     = nverts;
    b->triangle_count   = ntris;
    b->spawn_count      = nspawns;

    /* ScenarioBSP entry in tag data */
    w32(f, tdo + bsps_rel + 0x00, bsp_start);
    w32(f, tdo + bsps_rel + 0x04, bsp_size);
    w32(f, tdo + bsps_rel + 0x08, bsp_address);
    w32(f, tdo + bsps_rel + 0x0C, 0);
    w32(f, tdo + bsps_rel + 0x10, 0x73627370u);          /* 'sbsp' dependency */
    w32(f, tdo + bsps_rel + 0x1C, 0xE1740001u);          /* tag id of tag 1 */

    /* compiled header at bsp_start */
    w32(f, bsp_start + hdr_rel + 0x00, bsp_address + sbsp_rel);      /* -> sbsp struct */
    w32(f, bsp_start + hdr_rel + 0x04, 1);                            /* material count */
    w32(f, bsp_start + hdr_rel + 0x08, bsp_address + vertices_rel);   /* -> vertices */
    w32(f, bsp_start + hdr_rel + 0x0C, 1);
    w32(f, bsp_start + hdr_rel + 0x10, bsp_address + vertices_rel);
    w32(f, bsp_start + hdr_rel + 0x14, 0x73627370u);                  /* 'sbsp' signature */

    /* sbsp struct */
    wf32(f, bsp_start + sbsp_rel + SBSP_AMBIENT + 0, 0.10f);
    wf32(f, bsp_start + sbsp_rel + SBSP_AMBIENT + 4, 0.20f);
    wf32(f, bsp_start + sbsp_rel + SBSP_AMBIENT + 8, 0.30f);
    wf32(f, bsp_start + sbsp_rel + SBSP_L0_COLOR + 0, 0.90f);
    wf32(f, bsp_start + sbsp_rel + SBSP_L0_COLOR + 4, 0.85f);
    wf32(f, bsp_start + sbsp_rel + SBSP_L0_COLOR + 8, 0.80f);
    wf32(f, bsp_start + sbsp_rel + SBSP_L0_DIR + 0, 0.0f);
    wf32(f, bsp_start + sbsp_rel + SBSP_L0_DIR + 4, 0.0f);
    wf32(f, bsp_start + sbsp_rel + SBSP_L0_DIR + 8, -1.0f);
    wrefl(f, bsp_start + sbsp_rel + SBSP_SURFACES,  ntris, bsp_address + surfaces_rel);
    wrefl(f, bsp_start + sbsp_rel + SBSP_LIGHTMAPS, 1,     bsp_address + lightmaps_rel);

    /* surfaces: fan over the vertex block so indices stay in range */
    for (uint32_t i = 0; i < ntris; i++) {
        uint32_t so = bsp_start + surfaces_rel + i * 6u;
        uint16_t a = (uint16_t)(nverts ? (i * 3u + 0u) % nverts : 0);
        uint16_t bb= (uint16_t)(nverts ? (i * 3u + 1u) % nverts : 0);
        uint16_t cc= (uint16_t)(nverts ? (i * 3u + 2u) % nverts : 0);
        w16(f, so + 0, a); w16(f, so + 2, bb); w16(f, so + 4, cc);
    }

    /* lightmap 0 -> 1 material */
    w16(f, bsp_start + lightmaps_rel + 0x00, 0);   /* bitmap index */
    wrefl(f, bsp_start + lightmaps_rel + LIGHTMAP_MATERIALS, 1, bsp_address + materials_rel);

    /* material 0 */
    w32(f, bsp_start + materials_rel + MAT_SHADER + 0x0C, 0xE1740042u); /* shader tag id */
    w32(f, bsp_start + materials_rel + MAT_SURFACES, 0);
    w32(f, bsp_start + materials_rel + MAT_SURFACE_COUNT, ntris);
    w16(f, bsp_start + materials_rel + MAT_VTX_TYPE, 0);               /* env uncompressed */
    w32(f, bsp_start + materials_rel + MAT_VTX_COUNT, nverts);
    w32(f, bsp_start + materials_rel + MAT_VTX_OFFSET, 0);

    /* vertices: a deterministic ramp so tests can assert exact values */
    for (uint32_t i = 0; i < nverts; i++) {
        uint32_t vo = bsp_start + vertices_rel + i * VERTEX_SIZE;
        wf32(f, vo + 0,  (float)i);            /* pos.x */
        wf32(f, vo + 4,  (float)i * 2.0f);     /* pos.y */
        wf32(f, vo + 8,  (float)i * -0.5f);    /* pos.z */
        wf32(f, vo + 12, 0.0f);                /* normal */
        wf32(f, vo + 16, 0.0f);
        wf32(f, vo + 20, 1.0f);
        wf32(f, vo + 48, (float)i * 0.25f);    /* uv */
        wf32(f, vo + 52, (float)i * 0.75f);
    }

    /* --- fix up header sizes now that the file grew --- */
    uint32_t total = bsp_start + bsp_size;
    f->size          = total;
    f->tag_data_size = new_tag_data_size;
    w32(f, 0x2C4, new_tag_data_size);   /* demo tag_data_size */
    w32(f, 0x5E8, total);               /* demo decompressed_file_size */
}
