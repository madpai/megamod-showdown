#include "model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static void fail(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap);
}

static void xform_p(float o[3], const float in[3], const float pos[3], const float rot[3])
{
    float cy = cosf(rot[0]), sy = sinf(rot[0]);
    float cp = cosf(rot[1]), sp = sinf(rot[1]);
    float cr = cosf(rot[2]), sr = sinf(rot[2]);
    float x1 = in[0], y1 = in[1]*cr - in[2]*sr, z1 = in[1]*sr + in[2]*cr;
    float x2 = x1*cp + z1*sp, y2 = y1, z2 = -x1*sp + z1*cp;
    o[0] = x2*cy - y2*sy + pos[0];
    o[1] = x2*sy + y2*cy + pos[1];
    o[2] = z2 + pos[2];
}

static void xform_n(float o[3], const float in[3], const float rot[3])
{
    float z[3] = {0,0,0};
    xform_p(o, in, z, rot);
}

static bool grow(void **p, uint32_t *cap, uint32_t need, uint32_t elem)
{
    if (need <= *cap) return true;
    uint32_t ncap = *cap ? *cap : 64;
    while (ncap < need) ncap *= 2;
    void *np = realloc(*p, (size_t)ncap * elem);
    if (!np) return false;
    *p = np; *cap = ncap;
    return true;
}

static bool append_mod2(hta_bsp_mesh *dst, const hta_cache *c,
                        const hta_resource_map *bitmaps, uint32_t model_tag_id,
                        const float *pos, const float *rot, int sky,
                        char *err, size_t errlen)
{
    int32_t mi = hta_cache_find_tag_by_id(c, model_tag_id);
    if (mi < 0) { fail(err, errlen, "model tag 0x%08X missing", model_tag_id); return false; }
    hta_tag_entry mt;
    if (!hta_cache_tag(c, (uint32_t)mi, &mt) || mt.primary_class != HTA_TAG_MOD2) {
        fail(err, errlen, "tag 0x%08X is not mod2", model_tag_id); return false;
    }
    uint32_t moff;
    if (!hta_cache_ptr_to_offset(c, mt.tag_data_ptr, &moff)) return false;

    uint32_t gcount=0, gptr=0, scount=0, sptr=0;
    if (!hta_read_reflexive(c, moff + HTA_MOD2_GEOMETRIES, &gcount, &gptr) || gcount == 0)
        return false;
    hta_read_reflexive(c, moff + HTA_MOD2_SHADERS, &scount, &sptr);
    uint32_t garr, sarr = 0;
    if (!hta_cache_ptr_to_offset(c, gptr, &garr)) return false;
    if (scount) hta_cache_ptr_to_offset(c, sptr, &sarr);

    /* highest-detail geometry is index 0 */
    uint32_t pcount=0, pptr=0;
    if (!hta_read_reflexive(c, garr + HTA_GEOM_PARTS, &pcount, &pptr) || pcount == 0)
        return false;
    if (pcount > 64) pcount = 64;
    uint32_t parr;
    if (!hta_cache_ptr_to_offset(c, pptr, &parr)) return false;

    uint32_t vcap = dst->vertex_count, icap = dst->index_count, scap = dst->submesh_count;
    /* not true caps — we stored exact counts. Track separately via realloc from current. */
    vcap = dst->vertex_count;
    icap = dst->index_count;
    scap = dst->submesh_count;

    for (uint32_t pi = 0; pi < pcount; pi++) {
        uint32_t pe = parr + pi * HTA_PART_SIZE;
        uint16_t sh = 0, vtype = 0, tbuf = 0;
        uint32_t tcount = 0, toff = 0, vcount = 0, voff = 0;
        hta_rd_u16(c, pe + HTA_PART_SHADER, &sh);
        hta_rd_u16(c, pe + HTA_PART_TRI_BUF, &tbuf);
        hta_rd_u32(c, pe + HTA_PART_TRI_COUNT, &tcount);
        hta_rd_u32(c, pe + HTA_PART_TRI_OFFSET, &toff);
        hta_rd_u16(c, pe + HTA_PART_VTYPE, &vtype);
        hta_rd_u32(c, pe + HTA_PART_VCOUNT, &vcount);
        hta_rd_u32(c, pe + HTA_PART_VOFFSET, &voff);
        /* vtype 4 = model uncompressed (68-byte). tbuf 1 = triangle strip. */
        if (vtype != HTA_VTYPE_MODEL_UNCOMP || vcount == 0 || tcount == 0 ||
            vcount > 20000 || tcount > 80000)
            continue;

        uint32_t vfile = c->model_data_file_offset + voff;
        uint32_t tfile = c->model_data_file_offset + c->vertex_size + toff;
        int strip = (tbuf == 1);
        uint32_t index_words = strip ? tcount : tcount * 3u;
        if ((uint64_t)vfile + (uint64_t)vcount * HTA_MODEL_VTX_SIZE > c->size) continue;
        if ((uint64_t)tfile + (uint64_t)index_words * 2u > c->size) continue;

        uint32_t need_v = dst->vertex_count + vcount;
        uint32_t need_i = dst->index_count + index_words * 3u;
        uint32_t need_s = dst->submesh_count + 1;
        if (!grow((void **)&dst->vertices, &vcap, need_v, sizeof(hta_vertex))) return false;
        if (!grow((void **)&dst->indices, &icap, need_i, sizeof(uint32_t))) return false;
        if (!grow((void **)&dst->submeshes, &scap, need_s, sizeof(hta_submesh))) return false;

        uint32_t base = dst->vertex_count;
        float zp[3] = {0,0,0}, zr[3] = {0,0,0};
        const float *ppos = pos ? pos : zp;
        const float *prot = rot ? rot : zr;
        for (uint32_t v = 0; v < vcount; v++) {
            uint32_t vo = vfile + v * HTA_MODEL_VTX_SIZE;
            float ip[3], in[3], uv[2];
            if (!hta_rd_f32(c, vo+0, &ip[0]) || !hta_rd_f32(c, vo+4, &ip[1]) ||
                !hta_rd_f32(c, vo+8, &ip[2]) ||
                !hta_rd_f32(c, vo+12, &in[0]) || !hta_rd_f32(c, vo+16, &in[1]) ||
                !hta_rd_f32(c, vo+20, &in[2]) ||
                !hta_rd_f32(c, vo+48, &uv[0]) || !hta_rd_f32(c, vo+52, &uv[1]))
                return false;
            hta_vertex *d = &dst->vertices[base + v];
            if (sky) { d->pos[0]=ip[0]; d->pos[1]=ip[1]; d->pos[2]=ip[2];
                       d->normal[0]=in[0]; d->normal[1]=in[1]; d->normal[2]=in[2]; }
            else { xform_p(d->pos, ip, ppos, prot); xform_n(d->normal, in, prot); }
            d->uv[0]=uv[0]; d->uv[1]=uv[1];
            d->lm_uv[0]=d->lm_uv[1]=0;
        }

        uint32_t first = dst->index_count, emitted = 0;
        if (!strip) {
            for (uint32_t t = 0; t < tcount; t++) {
                uint16_t a,b,d;
                uint32_t to = tfile + t * 6u;
                if (!hta_rd_u16(c, to+0, &a) || !hta_rd_u16(c, to+2, &b) ||
                    !hta_rd_u16(c, to+4, &d)) break;
                if (a >= vcount || b >= vcount || d >= vcount) continue;
                dst->indices[first + emitted + 0] = base + a;
                dst->indices[first + emitted + 1] = base + b;
                dst->indices[first + emitted + 2] = base + d;
                emitted += 3;
            }
        } else {
            /* Gearbox stores a triangle strip. tcount is the number of u16
             * indices. 0xFFFF restarts; duplicated verts are degenerates. */
            uint16_t prev0 = 0, prev1 = 0;
            int have = 0, odd = 0;
            for (uint32_t i = 0; i < tcount; i++) {
                uint16_t ix = 0;
                if (!hta_rd_u16(c, tfile + i * 2u, &ix)) break;
                if (ix == 0xFFFFu) { have = 0; odd = 0; continue; }
                if (ix >= vcount) { have = 0; odd = 0; continue; }
                if (have < 2) {
                    if (have == 0) prev0 = ix;
                    else prev1 = ix;
                    have++;
                    continue;
                }
                uint16_t a = prev0, b = prev1, d = ix;
                if (a != b && b != d && a != d) {
                    if (odd) { uint16_t tmp = a; a = b; b = tmp; }
                    dst->indices[first + emitted + 0] = base + a;
                    dst->indices[first + emitted + 1] = base + b;
                    dst->indices[first + emitted + 2] = base + d;
                    emitted += 3;
                }
                prev0 = prev1;
                prev1 = ix;
                odd ^= 1;
            }
        }
        if (emitted == 0) continue;

        uint32_t shader_id = 0;
        if (sarr && sh < scount)
            hta_rd_u32(c, sarr + (uint32_t)sh * 32u + 12u, &shader_id);

        hta_submesh *sm = &dst->submeshes[dst->submesh_count];
        memset(sm, 0, sizeof(*sm));
        sm->first_index = first;
        sm->index_count = emitted;
        sm->shader_tag_id = shader_id;
        sm->lightmap_index = 0xFFFFu;
        sm->albedo_tex = ~0u;
        sm->lightmap_tex = ~0u;
        sm->draw_mode = hta_shader_draw_mode(c, shader_id);
        if (sky && sm->draw_mode == HTA_DRAW_SKIP) sm->draw_mode = HTA_DRAW_OPAQUE;
        uint32_t base_bm = hta_shader_base_bitmap(c, shader_id);
        if (base_bm) sm->albedo_tex = hta_mesh_intern_bitmap(dst, c, bitmaps, base_bm, 0);

        dst->vertex_count += vcount;
        dst->index_count += emitted;
        dst->submesh_count++;
    }
    return dst->submesh_count > 0 || dst->vertex_count > 0;
}

bool hta_model_instance(hta_bsp_mesh *world, const hta_cache *c,
                        const hta_resource_map *bitmaps, uint32_t model_tag_id,
                        const float pos[3], const float rot[3],
                        char *err, size_t errlen)
{
    if (!world || !c) { fail(err, errlen, "bad arguments"); return false; }
    return append_mod2(world, c, bitmaps, model_tag_id, pos, rot, 0, err, errlen);
}

static uint32_t palette_model(const hta_cache *c, uint32_t pal_arr, uint32_t pal_count,
                              uint16_t type)
{
    if (type >= pal_count) return 0;
    uint32_t obj_id = 0;
    if (!hta_rd_u32(c, pal_arr + (uint32_t)type * HTA_PALETTE_ENTRY_SIZE + 12u, &obj_id))
        return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, obj_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return 0;
    uint32_t off, mid = 0;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;
    hta_rd_u32(c, off + HTA_OBJECT_MODEL_ID, &mid);
    return mid;
}

static uint32_t add_palette_objects(hta_bsp_mesh *world, const hta_cache *c,
                                    const hta_resource_map *bitmaps,
                                    uint32_t place_off, uint32_t pal_off,
                                    uint32_t entry_size)
{
    int32_t si = hta_cache_find_tag_by_class(c, HTA_TAG_SCNR);
    if (si < 0) return 0;
    hta_tag_entry st;
    if (!hta_cache_tag(c, (uint32_t)si, &st)) return 0;
    uint32_t scn;
    if (!hta_cache_ptr_to_offset(c, st.tag_data_ptr, &scn)) return 0;
    uint32_t n=0, p=0, pn=0, pp=0;
    if (!hta_read_reflexive(c, scn + place_off, &n, &p) || n == 0) return 0;
    if (!hta_read_reflexive(c, scn + pal_off, &pn, &pp) || pn == 0) return 0;
    uint32_t arr, parr;
    if (!hta_cache_ptr_to_offset(c, p, &arr) || !hta_cache_ptr_to_offset(c, pp, &parr))
        return 0;
    uint32_t added = 0;
    if (n > 512) n = 512;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = arr + i * entry_size;
        uint16_t type = 0;
        float pos[3], rot[3];
        if (!hta_rd_u16(c, e + 0, &type)) break;
        if (!hta_rd_f32(c, e + 8, &pos[0]) || !hta_rd_f32(c, e + 12, &pos[1]) ||
            !hta_rd_f32(c, e + 16, &pos[2])) break;
        if (!hta_rd_f32(c, e + 20, &rot[0]) || !hta_rd_f32(c, e + 24, &rot[1]) ||
            !hta_rd_f32(c, e + 28, &rot[2])) break;
        uint32_t mid = palette_model(c, parr, pn, type);
        if (!mid) continue;
        char err[HTA_ERRLEN];
        if (hta_model_instance(world, c, bitmaps, mid, pos, rot, err, sizeof(err)))
            added++;
    }
    return added;
}

bool hta_scenario_add_objects(hta_bsp_mesh *world, const hta_cache *c,
                              const hta_resource_map *bitmaps,
                              char *err, size_t errlen)
{
    if (!world || !c) { fail(err, errlen, "bad arguments"); return false; }
    uint32_t s = add_palette_objects(world, c, bitmaps,
                                     HTA_SCENARIO_SCENERY_OFF, HTA_SCENARIO_SCENERY_PAL,
                                     HTA_SCENERY_ENTRY_SIZE);
    uint32_t v = add_palette_objects(world, c, bitmaps,
                                     HTA_SCENARIO_VEHICLES_OFF, HTA_SCENARIO_VEHICLE_PAL,
                                     HTA_VEHICLE_ENTRY_SIZE);
    if (err && errlen)
        snprintf(err, errlen, "scenery+vehicles instanced: %u+%u", s, v);
    return true;
}

bool hta_sky_load(hta_bsp_mesh *out, const hta_cache *c,
                  const hta_resource_map *bitmaps, char *err, size_t errlen)
{
    if (!out || !c) { fail(err, errlen, "bad arguments"); return false; }
    memset(out, 0, sizeof(*out));
    int32_t si = hta_cache_find_tag_by_class(c, HTA_TAG_SCNR);
    if (si < 0) { fail(err, errlen, "no scenario"); return false; }
    hta_tag_entry st;
    if (!hta_cache_tag(c, (uint32_t)si, &st)) return false;
    uint32_t scn;
    if (!hta_cache_ptr_to_offset(c, st.tag_data_ptr, &scn)) return false;
    uint32_t n=0, p=0;
    if (!hta_read_reflexive(c, scn + HTA_SCENARIO_SKIES_OFF, &n, &p) || n == 0) {
        fail(err, errlen, "scenario has no skies"); return false;
    }
    uint32_t arr, sky_id=0;
    if (!hta_cache_ptr_to_offset(c, p, &arr)) return false;
    hta_rd_u32(c, arr + 12, &sky_id);
    int32_t ti = hta_cache_find_tag_by_id(c, sky_id);
    if (ti < 0) { fail(err, errlen, "sky tag missing"); return false; }
    hta_tag_entry sky;
    if (!hta_cache_tag(c, (uint32_t)ti, &sky)) return false;
    uint32_t soff, mid=0;
    if (!hta_cache_ptr_to_offset(c, sky.tag_data_ptr, &soff)) return false;
    hta_rd_u32(c, soff + 12, &mid); /* Sky.model tag_id */
    if (!mid) { fail(err, errlen, "sky has no model"); return false; }

    out->textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
    if (!out->textures) return false;
    if (!append_mod2(out, c, bitmaps, mid, NULL, NULL, 1, err, errlen)) {
        hta_bsp_free(out);
        return false;
    }
    return true;
}
