#include "bsp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Sanity ceilings. Blood Gulch is far below all of these; they exist so a
 * malformed file can never make us allocate absurd amounts. */
#define MAX_VERTICES  (4u * 1000u * 1000u)
#define MAX_INDICES   (12u * 1000u * 1000u)
#define MAX_MATERIALS 8192u
#define MAX_LIGHTMAPS 2048u
#define MAX_SURFACES  (4u * 1000u * 1000u)

static void fail(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap);
}

bool hta_read_reflexive(const hta_cache *c, uint32_t off,
                        uint32_t *count, uint32_t *ptr)
{
    return hta_rd_u32(c, off + 0, count) && hta_rd_u32(c, off + 4, ptr);
}

/* A BSP has its own pointer base, so translation inside it differs from the
 * main tag data region. */
typedef struct {
    uint32_t start;    /* file offset of the BSP block */
    uint32_t size;
    uint32_t address;  /* base address its internal pointers assume */
} bsp_region;

static bool bsp_ptr(const bsp_region *r, uint32_t ptr, uint32_t *out)
{
    return hta_translate(ptr, r->address, r->start, r->size, out);
}

static bool read_rgb(const hta_cache *c, uint32_t off, float out[3])
{
    return hta_rd_f32(c, off + 0, &out[0]) &&
           hta_rd_f32(c, off + 4, &out[1]) &&
           hta_rd_f32(c, off + 8, &out[2]);
}

/* Resolves a material's vertex block: the uncompressed_vertices TagDataOffset
 * pointer, translated through the BSP's own base. Returns false if absent. */
static bool material_vertex_block(const hta_cache *c, const bsp_region *r,
                                  uint32_t material_off, uint32_t vcount,
                                  uint32_t *out_off)
{
    uint32_t blob_size = 0, blob_ptr = 0;
    if (!hta_rd_u32(c, material_off + HTA_MAT_UNCOMPRESSED_VERTS + HTA_TAGDATAOFFSET_SIZE, &blob_size))
        return false;
    if (!hta_rd_u32(c, material_off + HTA_MAT_UNCOMPRESSED_VERTS + HTA_TAGDATAOFFSET_POINTER, &blob_ptr))
        return false;
    if (blob_ptr == 0 || blob_size == 0) return false;

    /* the blob must be able to hold vcount render vertices */
    uint64_t need = (uint64_t)vcount * HTA_VERTEX_ENV_UNCOMPRESSED_SIZE;
    if (need > (uint64_t)blob_size) return false;

    uint32_t off;
    if (!bsp_ptr(r, blob_ptr, &off)) return false;
    if ((uint64_t)off + need > (uint64_t)c->size) return false;
    *out_off = off;
    return true;
}

uint32_t hta_scenario_spawns(const hta_cache *c, hta_spawn_point *out, uint32_t max)
{
    if (!c || !out || !max) return 0;
    int32_t si = hta_cache_find_tag_by_id(c, c->scenario_tag_id);
    if (si < 0) si = hta_cache_find_tag_by_class(c, HTA_TAG_SCNR);
    if (si < 0) return 0;

    hta_tag_entry st;
    if (!hta_cache_tag(c, (uint32_t)si, &st)) return 0;
    uint32_t scn_off;
    if (!hta_cache_ptr_to_offset(c, st.tag_data_ptr, &scn_off)) return 0;

    uint32_t count, ptr;
    if (!hta_read_reflexive(c, scn_off + HTA_SCENARIO_PLAYER_SPAWNS_OFF, &count, &ptr)) return 0;
    if (count == 0 || count > 4096u) return 0;

    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, ptr, &base)) return 0;

    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n < max; i++) {
        uint32_t e = base + i * 52u;   /* ScenarioPlayerStartingLocation == 52 */
        hta_spawn_point sp;
        memset(&sp, 0, sizeof(sp));
        if (!hta_rd_f32(c, e + 0x00, &sp.position[0])) break;
        if (!hta_rd_f32(c, e + 0x04, &sp.position[1])) break;
        if (!hta_rd_f32(c, e + 0x08, &sp.position[2])) break;
        if (!hta_rd_f32(c, e + 0x0C, &sp.facing))      break;
        if (!hta_rd_u16(c, e + 0x10, &sp.team_index))  break;
        if (!hta_rd_u16(c, e + 0x12, &sp.bsp_index))   break;
        out[n++] = sp;
    }
    return n;
}

void hta_bsp_free(hta_bsp_mesh *m)
{
    if (!m) return;
    free(m->vertices);
    free(m->indices);
    free(m->submeshes);
    memset(m, 0, sizeof(*m));
}

bool hta_bsp_load_first(const hta_cache *c, hta_bsp_mesh *out,
                        char *err, size_t errlen)
{
    if (!c || !out) { fail(err, errlen, "bad arguments"); return false; }
    memset(out, 0, sizeof(*out));

    /* ---- scenario ---- */
    int32_t si = hta_cache_find_tag_by_id(c, c->scenario_tag_id);
    if (si < 0) si = hta_cache_find_tag_by_class(c, HTA_TAG_SCNR);
    if (si < 0) { fail(err, errlen, "no scenario ('scnr') tag in cache"); return false; }

    hta_tag_entry st;
    if (!hta_cache_tag(c, (uint32_t)si, &st)) { fail(err, errlen, "scenario tag unreadable"); return false; }
    uint32_t scn_off;
    if (!hta_cache_ptr_to_offset(c, st.tag_data_ptr, &scn_off)) {
        fail(err, errlen, "scenario tag_data pointer 0x%08X out of range", st.tag_data_ptr);
        return false;
    }

    uint32_t bsp_count, bsp_ptr_addr;
    if (!hta_read_reflexive(c, scn_off + HTA_SCENARIO_STRUCTURE_BSPS_OFF, &bsp_count, &bsp_ptr_addr)) {
        fail(err, errlen, "cannot read structure_bsps reflexive"); return false;
    }
    if (bsp_count == 0) { fail(err, errlen, "scenario has no structure BSPs"); return false; }
    if (bsp_count > 16u) { fail(err, errlen, "implausible BSP count %u", bsp_count); return false; }

    uint32_t bsp_arr_off;
    if (!hta_cache_ptr_to_offset(c, bsp_ptr_addr, &bsp_arr_off)) {
        fail(err, errlen, "structure_bsps pointer 0x%08X out of range", bsp_ptr_addr);
        return false;
    }

    /* ---- first ScenarioBSP entry ---- */
    bsp_region reg;
    if (!hta_rd_u32(c, bsp_arr_off + 0x00, &reg.start) ||
        !hta_rd_u32(c, bsp_arr_off + 0x04, &reg.size)  ||
        !hta_rd_u32(c, bsp_arr_off + 0x08, &reg.address)) {
        fail(err, errlen, "ScenarioBSP entry unreadable"); return false;
    }
    if (reg.size < HTA_BSP_COMPILED_HEADER_SIZE ||
        (uint64_t)reg.start + reg.size > (uint64_t)c->size) {
        fail(err, errlen, "BSP block [0x%X +0x%X] outside file (size %zu)",
             reg.start, reg.size, c->size);
        return false;
    }

    /* ---- compiled header at bsp_start ---- */
    uint32_t sbsp_ptr = 0, signature = 0;
    if (!hta_rd_u32(c, reg.start + 0x00, &sbsp_ptr) ||
        !hta_rd_u32(c, reg.start + 0x14, &signature)) {
        fail(err, errlen, "BSP compiled header unreadable"); return false;
    }
    /* Note: the header's rendered/lightmap vertex pointers (+0x08/+0x10) are
     * zero on PC/Trial. Vertices are found per-material instead. */
    if (signature != HTA_TAG_SBSP) {
        fail(err, errlen, "BSP compiled header signature 0x%08X != 'sbsp'", signature);
        return false;
    }

    uint32_t sbsp_off;
    if (!bsp_ptr(&reg, sbsp_ptr, &sbsp_off)) {
        fail(err, errlen, "sbsp struct pointer 0x%08X outside BSP region", sbsp_ptr);
        return false;
    }

    /* ---- lighting + bounds (nice to have, never fatal) ---- */
    read_rgb(c, sbsp_off + HTA_SBSP_AMBIENT_COLOR, out->ambient);
    read_rgb(c, sbsp_off + HTA_SBSP_LIGHT0_COLOR, out->light0_color);
    read_rgb(c, sbsp_off + HTA_SBSP_LIGHT0_DIRECTION, out->light0_dir);

    /* ---- surfaces (the index source) ---- */
    uint32_t surf_count, surf_ptr;
    if (!hta_read_reflexive(c, sbsp_off + HTA_SBSP_SURFACES, &surf_count, &surf_ptr)) {
        fail(err, errlen, "cannot read surfaces reflexive"); return false;
    }
    if (surf_count == 0 || surf_count > MAX_SURFACES) {
        fail(err, errlen, "implausible surface count %u", surf_count); return false;
    }
    uint32_t surf_off;
    if (!bsp_ptr(&reg, surf_ptr, &surf_off)) {
        fail(err, errlen, "surfaces pointer 0x%08X outside BSP region", surf_ptr);
        return false;
    }
    if ((uint64_t)surf_off + (uint64_t)surf_count * 6u > (uint64_t)c->size) {
        fail(err, errlen, "surfaces array exceeds file"); return false;
    }

    /* ---- lightmaps -> materials ---- */
    uint32_t lm_count, lm_ptr;
    if (!hta_read_reflexive(c, sbsp_off + HTA_SBSP_LIGHTMAPS, &lm_count, &lm_ptr)) {
        fail(err, errlen, "cannot read lightmaps reflexive"); return false;
    }
    if (lm_count == 0 || lm_count > MAX_LIGHTMAPS) {
        fail(err, errlen, "implausible lightmap count %u", lm_count); return false;
    }
    uint32_t lm_off;
    if (!bsp_ptr(&reg, lm_ptr, &lm_off)) {
        fail(err, errlen, "lightmaps pointer 0x%08X outside BSP region", lm_ptr);
        return false;
    }

    /* ---- pass 1: count so we can size the buffers exactly ---- */
    uint64_t total_v = 0, total_i = 0;
    uint32_t total_m = 0;
    for (uint32_t li = 0; li < lm_count; li++) {
        uint32_t le = lm_off + li * HTA_LIGHTMAP_ENTRY_SIZE;
        uint32_t mcount, mptr;
        if (!hta_read_reflexive(c, le + HTA_LIGHTMAP_MATERIALS_OFF, &mcount, &mptr)) continue;
        if (mcount == 0 || mcount > MAX_MATERIALS) continue;
        uint32_t moff;
        if (!bsp_ptr(&reg, mptr, &moff)) continue;
        for (uint32_t mi = 0; mi < mcount; mi++) {
            uint32_t me = moff + mi * HTA_MATERIAL_ENTRY_SIZE;
            uint16_t vtype = 0xFFFF;
            uint32_t vcount = 0, scount = 0;
            if (!hta_rd_u16(c, me + HTA_MAT_RENDERED_VTX_TYPE, &vtype)) continue;
            if (!hta_rd_u32(c, me + HTA_MAT_RENDERED_VTX_COUNT, &vcount)) continue;
            if (!hta_rd_u32(c, me + HTA_MAT_SURFACE_COUNT, &scount)) continue;
            if (vtype != HTA_VTX_ENV_UNCOMPRESSED) continue;
            if (vcount == 0 || scount == 0) continue;
            uint32_t probe;
            if (!material_vertex_block(c, &reg, me, vcount, &probe)) continue;
            total_v += vcount;
            total_i += (uint64_t)scount * 3u;
            total_m++;
            if (total_v > MAX_VERTICES || total_i > MAX_INDICES) {
                fail(err, errlen, "geometry exceeds sanity limits (v=%llu i=%llu)",
                     (unsigned long long)total_v, (unsigned long long)total_i);
                return false;
            }
        }
    }
    if (total_m == 0 || total_v == 0 || total_i == 0) {
        fail(err, errlen, "no uncompressed environment geometry found "
                          "(lightmaps=%u surfaces=%u) — map may use compressed vertices",
             lm_count, surf_count);
        return false;
    }

    out->vertices  = (hta_vertex *)calloc(total_v, sizeof(hta_vertex));
    out->indices   = (uint32_t *)calloc(total_i, sizeof(uint32_t));
    out->submeshes = (hta_submesh *)calloc(total_m, sizeof(hta_submesh));
    if (!out->vertices || !out->indices || !out->submeshes) {
        fail(err, errlen, "out of memory for %llu vertices / %llu indices",
             (unsigned long long)total_v, (unsigned long long)total_i);
        hta_bsp_free(out);
        return false;
    }

    /* ---- pass 2: fill ---- */
    bool have_bounds = false;
    for (uint32_t li = 0; li < lm_count; li++) {
        uint32_t le = lm_off + li * HTA_LIGHTMAP_ENTRY_SIZE;
        uint16_t lm_bitmap = 0xFFFF;
        hta_rd_u16(c, le + 0x00, &lm_bitmap);

        uint32_t mcount, mptr;
        if (!hta_read_reflexive(c, le + HTA_LIGHTMAP_MATERIALS_OFF, &mcount, &mptr)) continue;
        if (mcount == 0 || mcount > MAX_MATERIALS) continue;
        uint32_t moff;
        if (!bsp_ptr(&reg, mptr, &moff)) continue;

        for (uint32_t mi = 0; mi < mcount; mi++) {
            uint32_t me = moff + mi * HTA_MATERIAL_ENTRY_SIZE;
            uint16_t vtype = 0xFFFF;
            uint32_t vcount = 0, first_surf = 0, scount = 0, shader_id = 0;

            if (!hta_rd_u16(c, me + HTA_MAT_RENDERED_VTX_TYPE, &vtype))    continue;
            if (!hta_rd_u32(c, me + HTA_MAT_RENDERED_VTX_COUNT, &vcount))  continue;
            if (!hta_rd_u32(c, me + HTA_MAT_SURFACES, &first_surf))        continue;
            if (!hta_rd_u32(c, me + HTA_MAT_SURFACE_COUNT, &scount))       continue;
            hta_rd_u32(c, me + HTA_MAT_SHADER + 0x0C, &shader_id);

            out->materials_seen++;
            if (vtype == HTA_VTX_ENV_COMPRESSED) { out->materials_skipped_compressed++; continue; }
            if (vtype != HTA_VTX_ENV_UNCOMPRESSED) { out->materials_skipped_bad++; continue; }
            if (vcount == 0 || scount == 0) { out->materials_skipped_bad++; continue; }

            /* surfaces slice must be inside the surfaces array */
            if ((uint64_t)first_surf + scount > (uint64_t)surf_count) {
                out->materials_skipped_bad++; continue;
            }

            uint32_t vblock = 0;
            if (!material_vertex_block(c, &reg, me, vcount, &vblock)) {
                out->materials_skipped_bad++; continue;
            }
            uint64_t vstart = (uint64_t)vblock;

            uint32_t base_vertex = out->vertex_count;

            /* vertices: 56-byte environment_vertex_uncompressed */
            bool vok = true;
            for (uint32_t v = 0; v < vcount; v++) {
                uint32_t vo = (uint32_t)vstart + v * HTA_VERTEX_ENV_UNCOMPRESSED_SIZE;
                hta_vertex *dst = &out->vertices[base_vertex + v];
                if (!hta_rd_f32(c, vo + 0,  &dst->pos[0])   ||
                    !hta_rd_f32(c, vo + 4,  &dst->pos[1])   ||
                    !hta_rd_f32(c, vo + 8,  &dst->pos[2])   ||
                    !hta_rd_f32(c, vo + 12, &dst->normal[0])||
                    !hta_rd_f32(c, vo + 16, &dst->normal[1])||
                    !hta_rd_f32(c, vo + 20, &dst->normal[2])||
                    !hta_rd_f32(c, vo + 48, &dst->uv[0])    ||
                    !hta_rd_f32(c, vo + 52, &dst->uv[1])) { vok = false; break; }

                for (int k = 0; k < 3; k++) {
                    float p = dst->pos[k];
                    if (!isfinite(p)) { vok = false; break; }
                    if (!have_bounds) { out->bounds_min[k] = out->bounds_max[k] = p; }
                    else {
                        if (p < out->bounds_min[k]) out->bounds_min[k] = p;
                        if (p > out->bounds_max[k]) out->bounds_max[k] = p;
                    }
                }
                if (!vok) break;
                have_bounds = true;
            }
            if (!vok) { out->materials_skipped_bad++; continue; }

            /* indices from the shared surfaces array; they are local to this
             * material's vertex block, so rebase into the global buffer */
            uint32_t first_index = out->index_count;
            bool iok = true;
            uint32_t emitted = 0;
            for (uint32_t s = 0; s < scount; s++) {
                uint32_t so = surf_off + (first_surf + s) * 6u;
                uint16_t a, b, d;
                if (!hta_rd_u16(c, so + 0, &a) || !hta_rd_u16(c, so + 2, &b) ||
                    !hta_rd_u16(c, so + 4, &d)) { iok = false; break; }
                if (a >= vcount || b >= vcount || d >= vcount) continue; /* drop bad triangle */
                out->indices[first_index + emitted + 0] = base_vertex + a;
                out->indices[first_index + emitted + 1] = base_vertex + b;
                out->indices[first_index + emitted + 2] = base_vertex + d;
                emitted += 3;
            }
            if (!iok || emitted == 0) { out->materials_skipped_bad++; continue; }

            out->vertex_count += vcount;
            out->index_count  += emitted;
            out->submeshes[out->submesh_count].first_index    = first_index;
            out->submeshes[out->submesh_count].index_count    = emitted;
            out->submeshes[out->submesh_count].shader_tag_id  = shader_id;
            out->submeshes[out->submesh_count].lightmap_index = lm_bitmap;
            out->submesh_count++;
        }
    }

    if (out->submesh_count == 0 || out->index_count == 0) {
        fail(err, errlen, "extracted no drawable geometry (%u materials seen, "
                          "%u compressed, %u malformed)",
             out->materials_seen, out->materials_skipped_compressed, out->materials_skipped_bad);
        hta_bsp_free(out);
        return false;
    }
    return true;
}
