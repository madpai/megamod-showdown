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

/* ------------------------------ FP skinning ------------------------------ */

typedef struct {
    const hta_anim_graph *graph;
    hta_transform        *rest_inv;   /* indexed by graph node */
    uint8_t              *have_rest;
    hta_skin_vertex     **out;        /* grows with dst->vertices */
    uint32_t              cap;
    /* model node -> graph node, resolved by name */
    int16_t               map[HTA_ANIM_MAX_NODES];
    uint32_t              node_count;
    int                   local_nodes; /* mod2 flag: per-part node tables */
    uint32_t              unbound;     /* vertices with no usable binding */
} skin_ctx;

/* Walks the mod2 node list, composes each node's bind pose into model space,
 * and stores its inverse against the matching graph node. Halo keys the two
 * skeletons by node name, not index: the hands model has 37 nodes and the FP
 * weapon 5, and they land in different slots of the graph's 42. */
static bool skin_bind_nodes(skin_ctx *sk, const hta_cache *c, uint32_t moff,
                            char *err, size_t errlen)
{
    uint32_t n = 0, ptr = 0, arr = 0;
    if (!hta_read_reflexive(c, moff + HTA_MOD2_NODES, &n, &ptr) || n == 0) {
        fail(err, errlen, "model has no nodes to skin against");
        return false;
    }
    if (n > HTA_ANIM_MAX_NODES) { fail(err, errlen, "model has %u nodes", n); return false; }
    if (!hta_cache_ptr_to_offset(c, ptr, &arr)) return false;

    hta_transform rest[HTA_ANIM_MAX_NODES];
    int16_t parent[HTA_ANIM_MAX_NODES];
    hta_transform local[HTA_ANIM_MAX_NODES];
    char name[32];

    sk->node_count = n;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t nb = arr + i * HTA_NODE_SIZE;
        memset(name, 0, sizeof(name));
        hta_rd_bytes(c, nb, name, 31);
        sk->map[i] = (int16_t)hta_anim_node_index(sk->graph, name);
        uint16_t par = 0xFFFFu;
        hta_rd_u16(c, nb + HTA_NODE_PARENT, &par);
        int16_t p = (int16_t)par;
        parent[i] = (p >= (int16_t)n) ? -1 : p;
        for (int k = 0; k < 3; k++) hta_rd_f32(c, nb + HTA_NODE_DEF_T + (uint32_t)k*4u, &local[i].t[k]);
        for (int k = 0; k < 4; k++) hta_rd_f32(c, nb + HTA_NODE_DEF_Q + (uint32_t)k*4u, &local[i].q[k]);
        /* Same rotation sense as the animation data -- conjugate on read. */
        local[i].q[0] = -local[i].q[0];
        local[i].q[1] = -local[i].q[1];
        local[i].q[2] = -local[i].q[2];
        local[i].s = 1.0f;
    }
    for (uint32_t i = 0; i < n; i++) {
        int16_t p = parent[i];
        if (p < 0 || (uint32_t)p == i || (uint32_t)p > i) rest[i] = local[i];
        else hta_xf_mul(&rest[i], &rest[p], &local[i]);
        if (sk->map[i] >= 0) {
            hta_xf_inverse(&sk->rest_inv[sk->map[i]], &rest[i]);
            sk->have_rest[sk->map[i]] = 1;
        }
    }
    return true;
}

static bool append_mod2(hta_bsp_mesh *dst, const hta_cache *c,
                        const hta_resource_map *bitmaps, uint32_t model_tag_id,
                        const float *pos, const float *rot, int sky,
                        skin_ctx *sk, char *err, size_t errlen)
{
    int32_t mi = hta_cache_find_tag_by_id(c, model_tag_id);
    if (mi < 0) { fail(err, errlen, "model tag 0x%08X missing", model_tag_id); return false; }
    hta_tag_entry mt;
    if (!hta_cache_tag(c, (uint32_t)mi, &mt) || mt.primary_class != HTA_TAG_MOD2) {
        fail(err, errlen, "tag 0x%08X is not mod2", model_tag_id); return false;
    }
    uint32_t moff;
    if (!hta_cache_ptr_to_offset(c, mt.tag_data_ptr, &moff)) return false;

    if (sk) {
        uint32_t mflags = 0;
        hta_rd_u32(c, moff + HTA_MOD2_FLAGS, &mflags);
        sk->local_nodes = (mflags & HTA_MOD2_FLAG_LOCAL_NODES) != 0;
        if (!skin_bind_nodes(sk, c, moff, err, errlen)) return false;
    }

    uint32_t gcount=0, gptr=0, scount=0, sptr=0;
    if (!hta_read_reflexive(c, moff + HTA_MOD2_GEOMETRIES, &gcount, &gptr) || gcount == 0)
        return false;
    hta_read_reflexive(c, moff + HTA_MOD2_SHADERS, &scount, &sptr);
    uint32_t garr, sarr = 0;
    if (!hta_cache_ptr_to_offset(c, gptr, &garr)) return false;
    if (scount) hta_cache_ptr_to_offset(c, sptr, &sarr);

    /* Regions compose the model (hull + gun + tires). Geometry 0 is often
     * just the gun. Render verts are already in model/bind space — rest-pose
     * skinning is identity. Object `coll` BSPs are node-local instead. */
    uint32_t geom_ids[32];
    uint32_t ngeom = 0;
    uint32_t rcount = 0, rptr = 0;
    if (hta_read_reflexive(c, moff + HTA_MOD2_REGIONS, &rcount, &rptr) &&
        rcount > 0 && rcount <= 32) {
        uint32_t rarr;
        if (hta_cache_ptr_to_offset(c, rptr, &rarr)) {
            for (uint32_t ri = 0; ri < rcount; ri++) {
                uint32_t pc = 0, pp = 0;
                if (!hta_read_reflexive(c, rarr + ri * HTA_REGION_SIZE + HTA_REGION_PERMS,
                                        &pc, &pp) || pc == 0 || pc > 16)
                    continue;
                uint32_t parr_p;
                if (!hta_cache_ptr_to_offset(c, pp, &parr_p)) continue;
                for (uint32_t k = 0; k < pc; k++) {
                    char pname[32];
                    memset(pname, 0, sizeof(pname));
                    hta_rd_bytes(c, parr_p + k * HTA_PERM_SIZE, pname, 31);
                    if (pname[0] == '~') continue; /* blur / damaged */
                    uint16_t cand[5];
                    uint32_t po = parr_p + k * HTA_PERM_SIZE;
                    hta_rd_u16(c, po + 72, &cand[0]); /* super high */
                    hta_rd_u16(c, po + 70, &cand[1]);
                    hta_rd_u16(c, po + 68, &cand[2]);
                    hta_rd_u16(c, po + 66, &cand[3]);
                    hta_rd_u16(c, po + 64, &cand[4]);
                    uint16_t gi = 0xFFFFu;
                    for (int t = 0; t < 5; t++)
                        if (cand[t] != 0xFFFFu && cand[t] < gcount) { gi = cand[t]; break; }
                    if (gi == 0xFFFFu) continue;
                    int dup = 0;
                    for (uint32_t d = 0; d < ngeom; d++) if (geom_ids[d] == gi) dup = 1;
                    if (!dup && ngeom < 32) geom_ids[ngeom++] = gi;
                    break;
                }
            }
        }
    }
    if (ngeom == 0) geom_ids[ngeom++] = 0;

    uint32_t vcap = dst->vertex_count, icap = dst->index_count, scap = dst->submesh_count;
    /* not true caps — we stored exact counts. Track separately via realloc from current. */
    vcap = dst->vertex_count;
    icap = dst->index_count;
    scap = dst->submesh_count;

    for (uint32_t gii = 0; gii < ngeom; gii++) {
    uint32_t pcount=0, pptr=0;
    if (!hta_read_reflexive(c, garr + geom_ids[gii] * HTA_GEOM_SIZE + HTA_GEOM_PARTS,
                            &pcount, &pptr) || pcount == 0)
        continue;
    if (pcount > 64) pcount = 64;
    uint32_t parr;
    if (!hta_cache_ptr_to_offset(c, pptr, &parr)) continue;

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
        uint8_t lnodes[HTA_PART_MAX_LOCAL_NODES], lncount = 0;
        if (sk && sk->local_nodes) {
            hta_rd_u8(c, pe + HTA_PART_LOCAL_NODE_COUNT, &lncount);
            if (lncount > HTA_PART_MAX_LOCAL_NODES) lncount = HTA_PART_MAX_LOCAL_NODES;
            memset(lnodes, 0, sizeof(lnodes));
            hta_rd_bytes(c, pe + HTA_PART_LOCAL_NODES, lnodes, lncount);
        }
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
        if (sk && !grow((void **)sk->out, &sk->cap, need_v, sizeof(hta_skin_vertex)))
            return false;

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

            if (sk) {
                hta_skin_vertex *sv = &(*sk->out)[base + v];
                uint16_t idx[2] = {0,0};
                float    w[2] = {0.0f,0.0f};
                hta_rd_u16(c, vo + HTA_MODEL_VTX_NODE0, &idx[0]);
                hta_rd_u16(c, vo + HTA_MODEL_VTX_NODE1, &idx[1]);
                hta_rd_f32(c, vo + HTA_MODEL_VTX_WEIGHT0, &w[0]);
                hta_rd_f32(c, vo + HTA_MODEL_VTX_WEIGHT1, &w[1]);
                float total = 0.0f;
                for (int k = 0; k < 2; k++) {
                    uint16_t mn = idx[k];
                    if (sk->local_nodes)
                        mn = (mn < lncount) ? lnodes[mn] : 0xFFFFu;
                    int16_t gn = (mn < sk->node_count) ? sk->map[mn] : -1;
                    if (gn < 0 || !(w[k] > 0.0f)) { sv->node[k] = HTA_SKIN_NONE; sv->weight[k] = 0.0f; }
                    else { sv->node[k] = (uint16_t)gn; sv->weight[k] = w[k]; total += w[k]; }
                }
                if (total > 1e-6f) { sv->weight[0] /= total; sv->weight[1] /= total; }
                else sk->unbound++;
            }
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
    } /* gii */
    return dst->submesh_count > 0 || dst->vertex_count > 0;
}

#define HTA_MOD2_MARKERS           0x0ACu
#define HTA_MARKER_SIZE            64u
#define HTA_MARKER_INSTANCES       52u
#define HTA_MARKER_INST_SIZE       32u
#define HTA_MARKER_INST_NODE        2u
#define HTA_MARKER_INST_TRANS       4u
#define HTA_MODEL_NODE_SIZE       156u

bool hta_model_marker(const hta_cache *c, uint32_t model_tag_id,
                      const char *marker_name,
                      char out_node_name[32], float out_translation[3])
{
    if (!c || !marker_name || !out_node_name || !out_translation) return false;
    out_node_name[0] = '\0';
    int32_t ti = hta_cache_find_tag_by_id(c, model_tag_id);
    if (ti < 0) return false;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return false;
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return false;

    uint32_t mc = 0, mp = 0, mo = 0;
    if (!hta_read_reflexive(c, base + HTA_MOD2_MARKERS, &mc, &mp)) return false;
    if (!mc || !hta_cache_ptr_to_offset(c, mp, &mo)) return false;

    for (uint32_t i = 0; i < mc; i++) {
        uint32_t m = mo + i * HTA_MARKER_SIZE;
        char name[33] = {0};
        if (!hta_rd_bytes(c, m, name, 32)) continue;
        name[32] = '\0';
        if (strcmp(name, marker_name) != 0) continue;

        uint32_t ic = 0, ip = 0, io = 0;
        if (!hta_read_reflexive(c, m + HTA_MARKER_INSTANCES, &ic, &ip)) return false;
        if (!ic || !hta_cache_ptr_to_offset(c, ip, &io)) return false;

        uint8_t node = 0;
        if (!hta_rd_u8(c, io + HTA_MARKER_INST_NODE, &node)) return false;
        if (!hta_rd_f32(c, io + HTA_MARKER_INST_TRANS + 0u, &out_translation[0]) ||
            !hta_rd_f32(c, io + HTA_MARKER_INST_TRANS + 4u, &out_translation[1]) ||
            !hta_rd_f32(c, io + HTA_MARKER_INST_TRANS + 8u, &out_translation[2]))
            return false;

        /* Resolve the node index against this model's own node list. */
        uint32_t nc = 0, np = 0, no = 0;
        if (!hta_read_reflexive(c, base + HTA_MOD2_NODES, &nc, &np)) return false;
        if (node >= nc || !hta_cache_ptr_to_offset(c, np, &no)) return false;
        if (!hta_rd_bytes(c, no + (uint32_t)node * HTA_MODEL_NODE_SIZE,
                          out_node_name, 31))
            return false;
        out_node_name[31] = '\0';
        return true;
    }
    return false;
}

bool hta_model_instance(hta_bsp_mesh *world, const hta_cache *c,
                        const hta_resource_map *bitmaps, uint32_t model_tag_id,
                        const float pos[3], const float rot[3],
                        char *err, size_t errlen)
{
    if (!world || !c) { fail(err, errlen, "bad arguments"); return false; }
    return append_mod2(world, c, bitmaps, model_tag_id, pos, rot, 0, NULL, err, errlen);
}

bool hta_model_append_skinned(hta_bsp_mesh *dst, hta_skin_vertex **skin,
                              const hta_cache *c, const hta_resource_map *bitmaps,
                              uint32_t model_tag_id, const hta_anim_graph *g,
                              hta_transform *rest_inv, uint8_t *have_rest,
                              char *err, size_t errlen)
{
    if (!dst || !skin || !c || !g || !rest_inv || !have_rest) {
        fail(err, errlen, "bad arguments"); return false;
    }
    skin_ctx sk;
    memset(&sk, 0, sizeof(sk));
    sk.graph = g;
    sk.rest_inv = rest_inv;
    sk.have_rest = have_rest;
    sk.out = skin;
    sk.cap = dst->vertex_count;
    for (uint32_t i = 0; i < HTA_ANIM_MAX_NODES; i++) sk.map[i] = -1;

    if (!append_mod2(dst, c, bitmaps, model_tag_id, NULL, NULL, 0, &sk, err, errlen))
        return false;
    if (sk.unbound) {
        fail(err, errlen, "%u vertices bound to no graph node", sk.unbound);
        return false;
    }
    return true;
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

#define HTA_MAX_COLL_NODES 64

typedef struct { float m[16]; } hta_m4; /* column-major */

static hta_m4 m4_id(void)
{
    hta_m4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static hta_m4 m4_mul(hta_m4 a, hta_m4 b)
{
    hta_m4 o;
    for (int col = 0; col < 4; col++)
    for (int row = 0; row < 4; row++) {
        o.m[col * 4 + row] =
            a.m[0 * 4 + row] * b.m[col * 4 + 0] +
            a.m[1 * 4 + row] * b.m[col * 4 + 1] +
            a.m[2 * 4 + row] * b.m[col * 4 + 2] +
            a.m[3 * 4 + row] * b.m[col * 4 + 3];
    }
    return o;
}

static hta_m4 m4_translate(float x, float y, float z)
{
    hta_m4 r = m4_id();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static hta_m4 m4_quat(float x, float y, float z, float w)
{
    hta_m4 r = m4_id();
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

/* Rest-pose node-to-object matrices from a GBXModel. Identity if missing. */
static uint32_t load_rest_pose(const hta_cache *c, uint32_t model_id,
                               hta_m4 *world, uint32_t maxn)
{
    if (!c || !model_id || !world || maxn == 0) return 0;
    int32_t mi = hta_cache_find_tag_by_id(c, model_id);
    if (mi < 0) return 0;
    hta_tag_entry mt;
    if (!hta_cache_tag(c, (uint32_t)mi, &mt) || mt.primary_class != HTA_TAG_MOD2)
        return 0;
    uint32_t moff;
    if (!hta_cache_ptr_to_offset(c, mt.tag_data_ptr, &moff)) return 0;
    uint32_t ncount = 0, nptr = 0, narr = 0;
    if (!hta_read_reflexive(c, moff + HTA_MOD2_NODES, &ncount, &nptr) || ncount == 0)
        return 0;
    if (ncount > maxn) ncount = maxn;
    if (!hta_cache_ptr_to_offset(c, nptr, &narr)) return 0;
    hta_m4 local[HTA_MAX_COLL_NODES];
    uint16_t parent[HTA_MAX_COLL_NODES];
    for (uint32_t i = 0; i < ncount; i++) {
        uint32_t no = narr + i * HTA_NODE_SIZE;
        float t[3], q[4];
        hta_rd_u16(c, no + HTA_NODE_PARENT, &parent[i]);
        hta_rd_f32(c, no + HTA_NODE_DEF_T + 0, &t[0]);
        hta_rd_f32(c, no + HTA_NODE_DEF_T + 4, &t[1]);
        hta_rd_f32(c, no + HTA_NODE_DEF_T + 8, &t[2]);
        hta_rd_f32(c, no + HTA_NODE_DEF_Q + 0, &q[0]);
        hta_rd_f32(c, no + HTA_NODE_DEF_Q + 4, &q[1]);
        hta_rd_f32(c, no + HTA_NODE_DEF_Q + 8, &q[2]);
        hta_rd_f32(c, no + HTA_NODE_DEF_Q + 12, &q[3]);
        if (!isfinite(t[0]) || !isfinite(q[3])) {
            local[i] = m4_id();
            parent[i] = 0xFFFF;
            continue;
        }
        local[i] = m4_mul(m4_translate(t[0], t[1], t[2]), m4_quat(q[0], q[1], q[2], q[3]));
    }
    for (uint32_t i = 0; i < ncount; i++) {
        hta_m4 acc = local[i];
        uint16_t p = parent[i];
        int guard = 0;
        while (p != 0xFFFF && p < ncount && guard++ < 64) {
            acc = m4_mul(local[p], acc);
            p = parent[p];
        }
        world[i] = acc;
    }
    return ncount;
}

typedef struct {
    const float *node; /* 16 floats or NULL */
    const float *pos;
    const float *rot;
} coll_xf;

static void coll_xform(float o[3], const float in[3], void *user)
{
    const coll_xf *x = (const coll_xf *)user;
    float t[3] = { in[0], in[1], in[2] };
    if (x->node) {
        const float *M = x->node;
        t[0] = M[0]*in[0] + M[4]*in[1] + M[8]*in[2]  + M[12];
        t[1] = M[1]*in[0] + M[5]*in[1] + M[9]*in[2]  + M[13];
        t[2] = M[2]*in[0] + M[6]*in[1] + M[10]*in[2] + M[14];
    }
    xform_p(o, t, x->pos, x->rot);
}

static uint32_t palette_collision(const hta_cache *c, uint32_t pal_arr,
                                  uint32_t pal_count, uint16_t type,
                                  uint32_t *out_model)
{
    if (out_model) *out_model = 0;
    if (type >= pal_count) return 0;
    uint32_t obj_id = 0;
    if (!hta_rd_u32(c, pal_arr + (uint32_t)type * HTA_PALETTE_ENTRY_SIZE + 12u, &obj_id))
        return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, obj_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) return 0;
    uint32_t off, cid = 0, mid = 0;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;
    hta_rd_u32(c, off + HTA_OBJECT_COLLISION_ID, &cid);
    hta_rd_u32(c, off + HTA_OBJECT_MODEL_ID, &mid);
    if (out_model) *out_model = mid;
    if (!cid || cid == 0xFFFFFFFFu) return 0;
    if (hta_cache_find_tag_by_id(c, cid) < 0) return 0;
    return cid;
}

static uint32_t instance_coll_tag(hta_bsp_mesh *col, const hta_cache *c,
                                  uint32_t coll_id, uint32_t model_id,
                                  const float pos[3], const float rot[3])
{
    int32_t ci = hta_cache_find_tag_by_id(c, coll_id);
    if (ci < 0) return 0;
    hta_tag_entry ct;
    if (!hta_cache_tag(c, (uint32_t)ci, &ct) || ct.primary_class != HTA_TAG_COLL)
        return 0;
    uint32_t coff;
    if (!hta_cache_ptr_to_offset(c, ct.tag_data_ptr, &coff)) return 0;
    uint32_t ncount = 0, nptr = 0, narr = 0;
    if (!hta_read_reflexive(c, coff + HTA_COLL_NODES, &ncount, &nptr) || ncount == 0)
        return 0;
    if (ncount > HTA_MAX_COLL_NODES) ncount = HTA_MAX_COLL_NODES;
    if (!hta_cache_ptr_to_offset(c, nptr, &narr)) return 0;

    hta_m4 rest[HTA_MAX_COLL_NODES];
    uint32_t nrest = load_rest_pose(c, model_id, rest, HTA_MAX_COLL_NODES);

    uint32_t added = 0;
    for (uint32_t ni = 0; ni < ncount; ni++) {
        uint32_t bc = 0, bp = 0;
        if (!hta_read_reflexive(c, narr + ni * HTA_COLL_NODE_SIZE + HTA_COLL_NODE_BSPS,
                                &bc, &bp) || bc == 0)
            continue;
        uint32_t barr;
        if (!hta_cache_ptr_to_offset(c, bp, &barr)) continue;
        coll_xf xf;
        xf.pos = pos;
        xf.rot = rot;
        xf.node = (ni < nrest) ? rest[ni].m : NULL;
        if (bc > 8) bc = 8;
        for (uint32_t bi = 0; bi < bc; bi++) {
            char e[HTA_ERRLEN];
            if (hta_coll_bsp_append(col, c, barr + bi * 96u, coll_xform, &xf, e, sizeof(e)))
                added++;
        }
    }
    return added;
}

static uint32_t add_palette_collision(hta_bsp_mesh *col, const hta_cache *c,
                                      uint32_t place_off, uint32_t pal_off,
                                      uint32_t entry_size)
{
    int32_t si = hta_cache_find_tag_by_class(c, HTA_TAG_SCNR);
    if (si < 0) return 0;
    hta_tag_entry st;
    if (!hta_cache_tag(c, (uint32_t)si, &st)) return 0;
    uint32_t scn;
    if (!hta_cache_ptr_to_offset(c, st.tag_data_ptr, &scn)) return 0;
    uint32_t n = 0, p = 0, pn = 0, pp = 0;
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
        uint32_t mid = 0;
        uint32_t cid = palette_collision(c, parr, pn, type, &mid);
        if (!cid) continue;
        if (instance_coll_tag(col, c, cid, mid, pos, rot))
            added++;
    }
    return added;
}

bool hta_scenario_add_collision(hta_bsp_mesh *col, const hta_cache *c,
                                char *err, size_t errlen)
{
    if (!col || !c) { fail(err, errlen, "bad arguments"); return false; }
    uint32_t s = add_palette_collision(col, c,
                                       HTA_SCENARIO_SCENERY_OFF, HTA_SCENARIO_SCENERY_PAL,
                                       HTA_SCENERY_ENTRY_SIZE);
    uint32_t v = add_palette_collision(col, c,
                                       HTA_SCENARIO_VEHICLES_OFF, HTA_SCENARIO_VEHICLE_PAL,
                                       HTA_VEHICLE_ENTRY_SIZE);
    if (err && errlen)
        snprintf(err, errlen, "scenery+vehicle colliders: %u+%u", s, v);
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
    if (!append_mod2(out, c, bitmaps, mid, NULL, NULL, 1, NULL, err, errlen)) {
        hta_bsp_free(out);
        return false;
    }
    return true;
}
