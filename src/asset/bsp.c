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

void hta_submesh_init(hta_submesh *sm)
{
    if (!sm) return;
    memset(sm, 0, sizeof(*sm));
    sm->albedo_tex    = ~0u;
    sm->lightmap_tex  = ~0u;
    sm->detail_tex    = ~0u;
    sm->detail2_tex   = ~0u;
    sm->multi_tex     = ~0u;
    sm->lightmap_index = 0xFFFFu;
    sm->meter = -1.0f;
}

void hta_bsp_free(hta_bsp_mesh *m)
{
    if (!m) return;
    free(m->vertices);
    free(m->indices);
    free(m->submeshes);
    free(m->tri_material);
    if (m->textures) {
        for (uint32_t i = 0; i < m->texture_count; i++) free(m->textures[i].rgba);
        free(m->textures);
    }
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
    hta_rd_u32(c, sbsp_off + HTA_SBSP_LIGHTMAPS_BITMAP + 0x0C, &out->lightmaps_bitmap_id);

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
            uint32_t blob_size = 0;
            hta_rd_u32(c, me + HTA_MAT_UNCOMPRESSED_VERTS + HTA_TAGDATAOFFSET_SIZE, &blob_size);
            uint32_t lm_base = 0;
            if ((uint64_t)vcount * (HTA_VERTEX_ENV_UNCOMPRESSED_SIZE + HTA_VERTEX_LIGHTMAP_SIZE)
                <= (uint64_t)blob_size)
                lm_base = (uint32_t)vstart + vcount * HTA_VERTEX_ENV_UNCOMPRESSED_SIZE;

            uint32_t base_vertex = out->vertex_count;

            /* vertices: 56-byte environment_vertex_uncompressed, then optional
             * 20-byte lightmap vertices (UV at +12). */
            bool vok = true;
            for (uint32_t v = 0; v < vcount; v++) {
                uint32_t vo = (uint32_t)vstart + v * HTA_VERTEX_ENV_UNCOMPRESSED_SIZE;
                hta_vertex *dst = &out->vertices[base_vertex + v];
                dst->lm_uv[0] = dst->lm_uv[1] = 0.0f;
                if (!hta_rd_f32(c, vo + 0,  &dst->pos[0])   ||
                    !hta_rd_f32(c, vo + 4,  &dst->pos[1])   ||
                    !hta_rd_f32(c, vo + 8,  &dst->pos[2])   ||
                    !hta_rd_f32(c, vo + 12, &dst->normal[0])||
                    !hta_rd_f32(c, vo + 16, &dst->normal[1])||
                    !hta_rd_f32(c, vo + 20, &dst->normal[2])||
                    !hta_rd_f32(c, vo + 48, &dst->uv[0])    ||
                    !hta_rd_f32(c, vo + 52, &dst->uv[1])) { vok = false; break; }
                if (lm_base) {
                    uint32_t lo = lm_base + v * HTA_VERTEX_LIGHTMAP_SIZE + 12u;
                    hta_rd_f32(c, lo + 0, &dst->lm_uv[0]);
                    hta_rd_f32(c, lo + 4, &dst->lm_uv[1]);
                }

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
            out->submeshes[out->submesh_count].albedo_tex     = ~0u;
            out->submeshes[out->submesh_count].lightmap_tex   = ~0u;
            out->submeshes[out->submesh_count].draw_mode      = HTA_DRAW_OPAQUE;
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

#define HTA_COLL_SURF_SIZE 12u
#define HTA_COLL_EDGE_SIZE 24u
#define HTA_COLL_VERT_SIZE 16u

static bool grow_mesh(void **p, uint32_t *cap, uint32_t need, uint32_t elem)
{
    if (need <= *cap) return true;
    uint32_t ncap = *cap ? *cap : 64;
    while (ncap < need) ncap *= 2;
    void *np = realloc(*p, (size_t)ncap * elem);
    if (!np) return false;
    *p = np; *cap = ncap;
    return true;
}

static void bump_bounds(hta_bsp_mesh *dst, const float p[3])
{
    for (int k = 0; k < 3; k++) {
        if (p[k] < dst->bounds_min[k]) dst->bounds_min[k] = p[k];
        if (p[k] > dst->bounds_max[k]) dst->bounds_max[k] = p[k];
    }
}

/* Fan-triangulate a ModelCollisionGeometryBSP whose arrays are already
 * resolved to file offsets. `xform` may be NULL (identity). */
static bool coll_bsp_emit(hta_bsp_mesh *dst, const hta_cache *c,
                          uint32_t voff, uint32_t vcount,
                          uint32_t soff, uint32_t scount,
                          uint32_t eoff, uint32_t ecount,
                          const uint8_t *mat_lut, uint32_t mat_count,
                          hta_coll_xform_fn xform, void *user)
{
    if (!dst || !c || vcount == 0 || scount == 0 || ecount == 0) return false;
    if (vcount > 200000 || scount > 200000) return false;

    uint32_t vcap = dst->vertex_count, icap = dst->index_count;
    uint32_t need_v = dst->vertex_count + vcount;
    if (!grow_mesh((void **)&dst->vertices, &vcap, need_v, sizeof(hta_vertex)))
        return false;
    uint32_t need_i = dst->index_count + scount * 12u;
    if (!grow_mesh((void **)&dst->indices, &icap, need_i, sizeof(uint32_t)))
        return false;

    uint32_t base = dst->vertex_count;
    uint32_t nidx_start = dst->index_count;
    int seeded = dst->vertex_count > 0 || dst->index_count > 0;
    for (uint32_t i = 0; i < vcount; i++) {
        float ip[3], op[3];
        if (!hta_rd_f32(c, voff + i * HTA_COLL_VERT_SIZE + 0, &ip[0]) ||
            !hta_rd_f32(c, voff + i * HTA_COLL_VERT_SIZE + 4, &ip[1]) ||
            !hta_rd_f32(c, voff + i * HTA_COLL_VERT_SIZE + 8, &ip[2]))
            return false;
        if (xform) xform(op, ip, user);
        else { op[0] = ip[0]; op[1] = ip[1]; op[2] = ip[2]; }
        memset(&dst->vertices[base + i], 0, sizeof(hta_vertex));
        dst->vertices[base + i].pos[0] = op[0];
        dst->vertices[base + i].pos[1] = op[1];
        dst->vertices[base + i].pos[2] = op[2];
        if (!seeded) {
            dst->bounds_min[0] = dst->bounds_max[0] = op[0];
            dst->bounds_min[1] = dst->bounds_max[1] = op[1];
            dst->bounds_min[2] = dst->bounds_max[2] = op[2];
            seeded = 1;
        } else {
            bump_bounds(dst, op);
        }
    }

    uint32_t nidx = dst->index_count;
    uint32_t mcap = dst->index_count / 3u;
    uint32_t loop[64];
    for (uint32_t s = 0; s < scount; s++) {
        uint32_t first = 0;
        hta_rd_u32(c, soff + s * HTA_COLL_SURF_SIZE + 4, &first);
        /* The surface's material index maps through the BSP's collision
         * materials to one of Halo's 33 MaterialTypes. */
        uint16_t midx = 0;
        uint8_t mtype = HTA_MATERIAL_NONE;
        if (hta_rd_u16(c, soff + s * HTA_COLL_SURF_SIZE + 10, &midx) &&
            mat_lut && midx < mat_count)
            mtype = mat_lut[midx];
        if (first >= ecount) continue;
        uint32_t e = first, nv = 0;
        for (uint32_t guard = 0; guard < 32 && nv < 64; guard++) {
            uint32_t start=0, end=0, fwd=0, rev=0, left=0, right=0;
            uint32_t eo = eoff + e * HTA_COLL_EDGE_SIZE;
            hta_rd_u32(c, eo + 0, &start);
            hta_rd_u32(c, eo + 4, &end);
            hta_rd_u32(c, eo + 8, &fwd);
            hta_rd_u32(c, eo + 12, &rev);
            hta_rd_u32(c, eo + 16, &left);
            hta_rd_u32(c, eo + 20, &right);
            uint32_t vert, next;
            if (left == s) { vert = start; next = fwd; }
            else           { vert = end;   next = rev; }
            if (vert >= vcount) break;
            loop[nv++] = vert;
            e = next;
            if (e == first || e >= ecount) break;
        }
        if (nv < 3) continue;
        for (uint32_t i = 1; i + 1 < nv; i++) {
            uint32_t need = nidx + 3;
            if (!grow_mesh((void **)&dst->indices, &icap, need, sizeof(uint32_t)))
                return false;
            if (!grow_mesh((void **)&dst->tri_material, &mcap, nidx / 3u + 1u,
                           sizeof(uint8_t)))
                return false;
            dst->tri_material[nidx / 3u] = mtype;
            dst->indices[nidx++] = base + loop[0];
            dst->indices[nidx++] = base + loop[i];
            dst->indices[nidx++] = base + loop[i + 1];
        }
    }
    dst->vertex_count += vcount;
    dst->index_count = nidx;
    return dst->index_count > nidx_start;
}

bool hta_coll_bsp_append_mat(hta_bsp_mesh *dst, const hta_cache *c, uint32_t cb_off,
                             const uint8_t *mat_lut, uint32_t mat_count,
                         hta_coll_xform_fn xform, void *user,
                         char *err, size_t errlen)
{
    if (!dst || !c) { fail(err, errlen, "bad arguments"); return false; }
    uint32_t scount=0, sptr=0, ecount=0, eptr=0, vcount=0, vptr=0;
    if (!hta_read_reflexive(c, cb_off + 60, &scount, &sptr) ||
        !hta_read_reflexive(c, cb_off + 72, &ecount, &eptr) ||
        !hta_read_reflexive(c, cb_off + 84, &vcount, &vptr)) {
        fail(err, errlen, "object collision reflexives"); return false;
    }
    uint32_t soff, eoff, voff;
    if (!hta_cache_ptr_to_offset(c, sptr, &soff) ||
        !hta_cache_ptr_to_offset(c, eptr, &eoff) ||
        !hta_cache_ptr_to_offset(c, vptr, &voff)) {
        fail(err, errlen, "object collision arrays"); return false;
    }
    uint32_t before = dst->index_count;
    if (!coll_bsp_emit(dst, c, voff, vcount, soff, scount, eoff, ecount,
                       mat_lut, mat_count, xform, user)) {
        fail(err, errlen, "object collision emit failed"); return false;
    }
    return dst->index_count > before;
}

bool hta_coll_bsp_append(hta_bsp_mesh *dst, const hta_cache *c, uint32_t cb_off,
                         hta_coll_xform_fn xform, void *user,
                         char *err, size_t errlen)
{
    /* Object colliders index their own `coll` tag's materials, not the
     * BSP's, so they come back unknown rather than wrong. */
    return hta_coll_bsp_append_mat(dst, c, cb_off, NULL, 0, xform, user, err, errlen);
}

bool hta_bsp_load_collision(const hta_cache *c, hta_bsp_mesh *out,
                            char *err, size_t errlen)
{
    if (!c || !out) { fail(err, errlen, "bad arguments"); return false; }
    memset(out, 0, sizeof(*out));

    int32_t si = hta_cache_find_tag_by_id(c, c->scenario_tag_id);
    if (si < 0) si = hta_cache_find_tag_by_class(c, HTA_TAG_SCNR);
    if (si < 0) { fail(err, errlen, "no scenario"); return false; }
    hta_tag_entry st;
    if (!hta_cache_tag(c, (uint32_t)si, &st)) return false;
    uint32_t scn_off;
    if (!hta_cache_ptr_to_offset(c, st.tag_data_ptr, &scn_off)) return false;
    uint32_t bsp_count, bsp_ptr_addr;
    if (!hta_read_reflexive(c, scn_off + HTA_SCENARIO_STRUCTURE_BSPS_OFF, &bsp_count, &bsp_ptr_addr)
        || bsp_count == 0) {
        fail(err, errlen, "no structure BSP"); return false;
    }
    uint32_t bsp_arr_off;
    if (!hta_cache_ptr_to_offset(c, bsp_ptr_addr, &bsp_arr_off)) return false;
    bsp_region reg;
    if (!hta_rd_u32(c, bsp_arr_off + 0x00, &reg.start) ||
        !hta_rd_u32(c, bsp_arr_off + 0x04, &reg.size)  ||
        !hta_rd_u32(c, bsp_arr_off + 0x08, &reg.address)) return false;
    uint32_t hdr_ptr = 0, sbsp_off = 0;
    if (!hta_rd_u32(c, reg.start, &hdr_ptr) || !bsp_ptr(&reg, hdr_ptr, &sbsp_off)) {
        fail(err, errlen, "sbsp header"); return false;
    }

    uint32_t cb_count = 0, cb_ptr = 0;
    if (!hta_read_reflexive(c, sbsp_off + HTA_SBSP_COLLISION_BSP, &cb_count, &cb_ptr) || cb_count == 0) {
        fail(err, errlen, "no collision BSP"); return false;
    }
    uint32_t cb_off;
    if (!bsp_ptr(&reg, cb_ptr, &cb_off)) { fail(err, errlen, "collision BSP ptr"); return false; }

    uint32_t scount=0, sptr=0, ecount=0, eptr=0, vcount=0, vptr=0;
    if (!hta_read_reflexive(c, cb_off + 60, &scount, &sptr) ||
        !hta_read_reflexive(c, cb_off + 72, &ecount, &eptr) ||
        !hta_read_reflexive(c, cb_off + 84, &vcount, &vptr)) {
        fail(err, errlen, "collision reflexives"); return false;
    }
    uint32_t soff, eoff, voff;
    if (!bsp_ptr(&reg, sptr, &soff) || !bsp_ptr(&reg, eptr, &eoff) || !bsp_ptr(&reg, vptr, &voff)) {
        fail(err, errlen, "collision arrays"); return false;
    }
    /* Which of Halo's 33 material types each collision surface is. The
     * surface stores an index into the BSP's own collision materials, and
     * each of those names the type outright. */
    uint8_t mat_lut[256];
    uint32_t mat_count = 0;
    {
        uint32_t mc = 0, mp = 0, mo = 0;
        if (hta_read_reflexive(c, sbsp_off + HTA_SBSP_COLLISION_MATERIALS, &mc, &mp) &&
            mc && bsp_ptr(&reg, mp, &mo)) {
            if (mc > 256) mc = 256;
            for (uint32_t i = 0; i < mc; i++) {
                uint16_t ty = 0;
                hta_rd_u16(c, mo + i * HTA_SBSP_COLL_MAT_SIZE + HTA_SBSP_COLL_MAT_TYPE, &ty);
                mat_lut[i] = (ty < 33u) ? (uint8_t)ty : HTA_MATERIAL_NONE;
            }
            mat_count = mc;
        }
    }

    if (!coll_bsp_emit(out, c, voff, vcount, soff, scount, eoff, ecount,
                       mat_lut, mat_count, NULL, NULL) ||
        out->index_count < 3) {
        fail(err, errlen, "collision BSP produced no triangles");
        hta_bsp_free(out);
        return false;
    }
    return true;
}

uint32_t hta_scenario_spawn_pick(const hta_spawn_point *spawns, uint32_t count,
                                 const float avoid[3], uint32_t *rng)
{
    if (!spawns || !count) return 0;

    uint32_t seed = rng ? *rng : 1u;
    seed = seed * 1103515245u + 12345u;
    if (rng) *rng = seed;

    if (!avoid) return (seed >> 16) % count;

    /* Furthest wins, but not always the same one: ties and near-ties on a
     * symmetrical map like Blood Gulch would otherwise send you to the
     * identical rock every time. Pick at random among the furthest half. */
    float best = -1.0f;
    for (uint32_t i = 0; i < count; i++) {
        float dx = spawns[i].position[0] - avoid[0];
        float dy = spawns[i].position[1] - avoid[1];
        float dz = spawns[i].position[2] - avoid[2];
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > best) best = d2;
    }
    if (!(best > 0.0f)) return (seed >> 16) % count;

    uint32_t good[64];
    uint32_t n = 0;
    const float HALF = 0.25f;   /* d^2 >= a quarter of the best is half the distance */
    for (uint32_t i = 0; i < count && n < 64u; i++) {
        float dx = spawns[i].position[0] - avoid[0];
        float dy = spawns[i].position[1] - avoid[1];
        float dz = spawns[i].position[2] - avoid[2];
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 >= best * HALF) good[n++] = i;
    }
    if (!n) return 0;
    return good[(seed >> 16) % n];
}
