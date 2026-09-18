/* htainfo — inspect a user-supplied Halo cache (.map) file.
 *
 * Answers the Phase 2 question "can our code reliably understand the Trial
 * data?" before any rendering exists.
 *
 *   htainfo <file.map>            summary + tag class histogram
 *   htainfo <file.map> --tags     list every tag
 *   htainfo <file.map> --bsp      extract geometry and report it
 *   htainfo <file.map> --spawns   list player spawn points
 */
#include "asset/cache.h"
#include "asset/bsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)n;
    return buf;
}

typedef struct { uint32_t fourcc; uint32_t count; uint32_t indexed; } class_stat;

static void bump(class_stat *st, uint32_t *n, uint32_t fourcc, bool indexed)
{
    for (uint32_t i = 0; i < *n; i++) {
        if (st[i].fourcc == fourcc) { st[i].count++; if (indexed) st[i].indexed++; return; }
    }
    if (*n < 256) { st[*n].fourcc = fourcc; st[*n].count = 1; st[*n].indexed = indexed ? 1 : 0; (*n)++; }
}

static int cmp_stat(const void *a, const void *b)
{
    const class_stat *x = a, *y = b;
    if (x->count != y->count) return (int)y->count - (int)x->count;
    return (int)x->fourcc - (int)y->fourcc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cache.map> [--tags] [--bsp] [--spawns]\n", argv[0]);
        fprintf(stderr, "\nYou must supply your own legally obtained Halo Trial data.\n");
        return 2;
    }
    bool want_tags = false, want_bsp = false, want_spawns = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--tags"))   want_tags = true;
        else if (!strcmp(argv[i], "--bsp"))    want_bsp = true;
        else if (!strcmp(argv[i], "--spawns")) want_spawns = true;
        else { fprintf(stderr, "unknown option: %s\n", argv[i]); return 2; }
    }

    size_t size = 0;
    uint8_t *data = slurp(argv[1], &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, size, err, sizeof(err))) {
        fprintf(stderr, "not a usable cache file: %s\n", err);
        free(data);
        return 1;
    }

    char lit[5];
    printf("file            %s\n", argv[1]);
    printf("size            %zu bytes (%.1f MiB)\n", size, (double)size / (1024.0*1024.0));
    printf("header layout   %s\n", c.is_demo_layout ? "DEMO/Trial (permuted, 'Ehed'/'Gfot')"
                                                    : "retail ('head'/'foot')");
    printf("engine          %u — %s\n", c.engine, hta_engine_name(c.engine));
    printf("name            \"%s\"\n", c.name);
    printf("build           \"%s\"\n", c.build);
    printf("map type        %u (%s)\n", c.map_type,
           c.map_type == 0 ? "singleplayer" : c.map_type == 1 ? "multiplayer" :
           c.map_type == 2 ? "user interface" : "?");
    printf("crc32           0x%08X\n", c.crc32);
    printf("base address    0x%08X\n", c.base_address);
    printf("tag data        offset 0x%08X  size 0x%X (%.2f MiB)\n",
           c.tag_data_offset, c.tag_data_size, (double)c.tag_data_size/(1024.0*1024.0));
    printf("tag array       ptr 0x%08X -> file offset 0x%08X\n", c.tag_array_ptr, c.tag_array_offset);
    printf("tag count       %u\n", c.tag_count);
    hta_fourcc_str(c.tags_literal, lit);
    printf("tags literal    '%s'\n", lit);
    printf("scenario tag id 0x%08X\n", c.scenario_tag_id);
    printf("model data      offset 0x%08X size 0x%X vertex size %u\n",
           c.model_data_file_offset, c.model_data_size, c.vertex_size);

    /* ---- class histogram + indexed count (answers the bitmaps.map question) ---- */
    static class_stat st[256];
    uint32_t nst = 0, indexed_total = 0, unreadable_paths = 0;
    for (uint32_t i = 0; i < c.tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(&c, i, &t)) continue;
        bump(st, &nst, t.primary_class, t.indexed != 0);
        if (t.indexed) indexed_total++;
        char p[256];
        if (!hta_cache_tag_path(&c, &t, p, sizeof(p))) unreadable_paths++;
    }
    qsort(st, nst, sizeof(st[0]), cmp_stat);

    printf("\ntag classes     %u distinct\n", nst);
    printf("indexed tags    %u of %u (%.1f%%) — these live in external resource maps\n",
           indexed_total, c.tag_count, c.tag_count ? 100.0*indexed_total/c.tag_count : 0.0);
    printf("unreadable path %u\n", unreadable_paths);
    printf("\n  %-6s %8s %8s\n", "class", "count", "indexed");
    for (uint32_t i = 0; i < nst; i++) {
        char f[5];
        hta_fourcc_str(st[i].fourcc, f);
        printf("  %-6s %8u %8u\n", f, st[i].count, st[i].indexed);
    }

    if (want_tags) {
        printf("\n--- tags ---\n");
        for (uint32_t i = 0; i < c.tag_count; i++) {
            hta_tag_entry t;
            if (!hta_cache_tag(&c, i, &t)) { printf("  [%5u] <unreadable>\n", i); continue; }
            char f[5], p[256];
            hta_fourcc_str(t.primary_class, f);
            if (!hta_cache_tag_path(&c, &t, p, sizeof(p))) snprintf(p, sizeof(p), "<bad path>");
            printf("  [%5u] %s id=0x%08X %s%s\n", i, f, t.tag_id,
                   t.indexed ? "(indexed) " : "", p);
        }
    }

    if (want_spawns) {
        printf("\n--- player spawn points ---\n");
        static hta_spawn_point sp[256];
        uint32_t n = hta_scenario_spawns(&c, sp, 256);
        if (n == 0) printf("  none found\n");
        for (uint32_t i = 0; i < n; i++)
            printf("  [%3u] pos (%9.3f %9.3f %9.3f) facing %7.3f team %u bsp %u\n",
                   i, sp[i].position[0], sp[i].position[1], sp[i].position[2],
                   sp[i].facing, sp[i].team_index, sp[i].bsp_index);
        printf("  %u spawn point(s)\n", n);
    }

    if (want_bsp) {
        printf("\n--- structure BSP geometry ---\n");
        hta_bsp_mesh m;
        char berr[HTA_ERRLEN] = {0};
        if (!hta_bsp_load_first(&c, &m, berr, sizeof(berr))) {
            printf("  extraction FAILED: %s\n", berr);
            free(data);
            return 1;
        }
        printf("  vertices        %u  (%.2f MiB as 32-byte verts)\n",
               m.vertex_count, m.vertex_count * 32.0 / (1024.0*1024.0));
        printf("  indices         %u  (%u triangles)\n", m.index_count, m.index_count / 3);
        printf("  submeshes       %u\n", m.submesh_count);
        printf("  materials seen  %u (skipped: %u compressed, %u malformed)\n",
               m.materials_seen, m.materials_skipped_compressed, m.materials_skipped_bad);
        printf("  bounds min      (%.3f %.3f %.3f)\n", m.bounds_min[0], m.bounds_min[1], m.bounds_min[2]);
        printf("  bounds max      (%.3f %.3f %.3f)\n", m.bounds_max[0], m.bounds_max[1], m.bounds_max[2]);
        printf("  extent          (%.3f %.3f %.3f) world units\n",
               m.bounds_max[0]-m.bounds_min[0], m.bounds_max[1]-m.bounds_min[1],
               m.bounds_max[2]-m.bounds_min[2]);
        printf("  ambient         (%.3f %.3f %.3f)\n", m.ambient[0], m.ambient[1], m.ambient[2]);
        printf("  distant light 0 colour (%.3f %.3f %.3f) dir (%.3f %.3f %.3f)\n",
               m.light0_color[0], m.light0_color[1], m.light0_color[2],
               m.light0_dir[0], m.light0_dir[1], m.light0_dir[2]);
        hta_bsp_free(&m);
    }

    free(data);
    return 0;
}
