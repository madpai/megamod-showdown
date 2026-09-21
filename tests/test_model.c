/* Model UV restoration and placed-object lighting. Synthetic first; optional
 * owner-supplied Trial map checks the actual Warthog materials too. */
#include "asset/model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c, msg) do { checks++; if (!(c)) { failures++; printf("FAIL: %s\n", msg); } } while (0)
static void u32(unsigned char *b, unsigned o, uint32_t v)
{ for (unsigned k = 0; k < 4; k++) b[o+k] = (unsigned char)(v >> (8*k)); }
static void f32(unsigned char *b, unsigned o, float v)
{ uint32_t x; memcpy(&x, &v, 4); u32(b, o, x); }
static int near(float a, float b) { return fabsf(a-b) < 0.0001f; }
static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); rewind(f);
    if (len <= 0) { fclose(f); return NULL; }
    unsigned char *b = malloc((size_t)len);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)len; return b;
}

static void synthetic(void)
{
    unsigned char bytes[4096] = {0};
    hta_cache c = {0};
    c.data = bytes; c.size = sizeof(bytes); c.tag_data_size = sizeof(bytes);
    c.tag_count = 2; c.model_data_file_offset = 2048; c.vertex_size = 3 * 68;
    /* Two tags, one three-vertex model part, no proprietary assets. */
    u32(bytes, 0, HTA_TAG_MOD2); u32(bytes, 12, 1); u32(bytes, 20, 256);
    u32(bytes, 32, HTA_TAG_SOSO); u32(bytes, 44, 2); u32(bytes, 52, 1024);
    u32(bytes, 256+208, 1); u32(bytes, 256+212, 512); /* geometries */
    u32(bytes, 256+220, 1); u32(bytes, 256+224, 800); /* shaders */
    u32(bytes, 812, 2);
    u32(bytes, 512+36, 1); u32(bytes, 512+40, 576); /* parts */
    u32(bytes, 576+72, 1); u32(bytes, 576+84, 4); u32(bytes, 576+88, 3);
    bytes[2254] = 1; bytes[2256] = 2; /* triangle indices */
    for (unsigned k = 0; k < 3; k++) {
        f32(bytes, 2048+k*68, (float)k);
        f32(bytes, 2048+k*68+20, 1);
        f32(bytes, 2048+k*68+48, 0.25f);
        f32(bytes, 2048+k*68+52, -0.5f);
    }
    const float scales[][2] = {{2,3}, {0,0}, {1,1}, {-2,0.5f}, {NAN,INFINITY}};
    const float expected[][2] = {{0.5f,-1.5f}, {0.25f,-0.5f}, {0.25f,-0.5f}, {-0.5f,-0.25f}, {0.25f,-0.5f}};
    for (unsigned i = 0; i < 5; i++) {
        f32(bytes, 256+48, scales[i][0]); f32(bytes, 256+52, scales[i][1]);
        hta_bsp_mesh m = {0}; char err[HTA_ERRLEN];
        bool ok = hta_model_instance(&m, &c, NULL, 1, NULL, NULL, err, sizeof(err));
        CHECK(ok && m.vertex_count == 3, "synthetic model loads");
        if (ok && m.vertex_count == 3) {
            CHECK(near(m.vertices[0].uv[0], expected[i][0]) && near(m.vertices[0].uv[1], expected[i][1]),
                  "UVs restore model scales, including defaults and signed scales");
            CHECK(m.submeshes[0].scene_lit, "opaque placed model uses scene lighting");
        }
        hta_bsp_free(&m);
    }
    u32(bytes, 32, HTA_TAG_SGLA);
    hta_bsp_mesh m = {0}; char err[HTA_ERRLEN];
    CHECK(hta_model_instance(&m, &c, NULL, 1, NULL, NULL, err, sizeof(err)), "glass fixture loads");
    CHECK(m.submesh_count && !m.submeshes[0].scene_lit, "glass keeps its effect lighting path");
    hta_bsp_free(&m);
    hta_submesh sm; hta_submesh_init(&sm);
    CHECK(!sm.scene_lit, "ordinary BSP submeshes default to baked lighting");
}

static void real_map(const char *path)
{
    size_t n = 0; unsigned char *data = slurp(path, &n);
    CHECK(data != NULL, "map readable"); if (!data) return;
    hta_cache c; char err[HTA_ERRLEN];
    bool opened = hta_cache_open(&c, data, n, err, sizeof(err));
    CHECK(opened, "map opens"); if (!opened) { free(data); return; }
    uint32_t model = 0, moff = 0;
    for (uint32_t i = 0; i < c.tag_count; i++) {
        hta_tag_entry t; char name[256];
        if (!hta_cache_tag(&c, i, &t) || t.primary_class != HTA_TAG_MOD2) continue;
        if (hta_cache_tag_path(&c, &t, name, sizeof(name)) && !strcmp(name, "vehicles\\warthog\\warthog")) {
            model = t.tag_id; hta_cache_ptr_to_offset(&c, t.tag_data_ptr, &moff); break;
        }
    }
    CHECK(model != 0, "Trial Warthog model exists");
    if (!model) { free(data); return; }
    float u = 0, v = 0;
    hta_rd_f32(&c, moff+48, &u); hta_rd_f32(&c, moff+52, &v);
    CHECK(fabsf(u-2) < 0.01f && fabsf(v-3) < 0.01f, "Warthog carries non-unit 2x3 UV scales");
    char bmp[1024]; snprintf(bmp, sizeof(bmp), "%s", path);
    char *slash = strrchr(bmp, '/');
    if (slash) snprintf(slash+1, sizeof(bmp)-(size_t)(slash+1-bmp), "bitmaps.map");
    else snprintf(bmp, sizeof(bmp), "bitmaps.map");
    size_t bn = 0; unsigned char *bd = slurp(bmp, &bn); hta_resource_map rm = {0};
    if (bd) hta_resource_open(&rm, bd, bn, err, sizeof(err));
    hta_bsp_mesh m = {0}; m.textures = calloc(256, sizeof(*m.textures));
    bool ok = m.textures && hta_model_instance(&m, &c, rm.data ? &rm : NULL, model, NULL, NULL, err, sizeof(err));
    CHECK(ok, "Warthog geometry and textures load");
    unsigned hull = 0, tires = 0;
    if (ok) for (uint32_t i = 0; i < m.submesh_count; i++) {
        hta_submesh *sm = &m.submeshes[i]; hta_tag_entry t; char name[256];
        int32_t ti = hta_cache_find_tag_by_id(&c, sm->shader_tag_id);
        if (ti < 0 || !hta_cache_tag(&c, (uint32_t)ti, &t) || !hta_cache_tag_path(&c, &t, name, sizeof(name))) continue;
        if (!strstr(name, "warthog hull") && !strstr(name, "warthog tires")) continue;
        if (strstr(name, "warthog hull")) hull++; else tires++;
        CHECK(sm->albedo_tex < m.texture_count && m.textures[sm->albedo_tex].rgba, "Warthog hull/tire pixels decode");
        CHECK(sm->scene_lit, "Warthog hull/tires get scene lighting");
    }
    CHECK(hull && tires, "both hull and tire materials are present");
    hta_bsp_free(&m); free(bd); free(data);
}
int main(int argc, char **argv)
{
    synthetic(); if (argc > 1) real_map(argv[1]);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
