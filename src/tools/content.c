/* megamod-content: load the phone's content set on the desktop, by the
 * same names and through the same code (app/fs.h, app/content.h), and say
 * what is there. The agent's first check that the desktop sees what the
 * phone does: the Trial maps, every bundled world, character and weapon.
 *
 *   megamod-content [--trial DIR] [--bundle DIR] [--world NAME]...
 *   (or HTA_TRIAL_DIR / HTA_BUNDLE_DIR). With no --world, every
 *   maps/NAME.oalmap is loaded. Exit 1 if anything named fails. */
#include "app/content.h"
#include "app/fs.h"
#include "app/session.h"
#include "asset/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *trial = getenv("HTA_TRIAL_DIR"), *bundle = getenv("HTA_BUNDLE_DIR");
    const char *worlds[64];
    unsigned nworld = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trial") && i + 1 < argc) trial = argv[++i];
        else if (!strcmp(argv[i], "--bundle") && i + 1 < argc) bundle = argv[++i];
        else if (!strcmp(argv[i], "--world") && i + 1 < argc && nworld < 64) worlds[nworld++] = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--trial DIR] [--bundle DIR] [--world NAME]...\n", argv[0]);
            return 2;
        }
    }
    static hta_fs fs;
    hta_fs_init(&fs);
    hta_fs_mount_content(&fs, trial, bundle);
    int failed = 0;

    static const char *const TRIAL[4] = { "bloodgulch.map", "bitmaps.map", "sounds.map", "ui.map" };
    for (int i = 0; i < 4; i++) {
        char name[64];
        hta_fs_blob b;
        snprintf(name, sizeof(name), "maps/%s", TRIAL[i]);
        if (!hta_fs_map(&fs, name, &b)) { printf("trial   %-22s missing\n", TRIAL[i]); if (!i) failed = 1; continue; }
        if (!i) {
            static hta_cache c;
            char err[HTA_ERRLEN];
            if (hta_cache_open(&c, b.data, b.size, err, sizeof(err)))
                printf("trial   %-22s %zu bytes, crc %08x, %u tags\n", TRIAL[i], b.size, c.crc32, c.tag_count);
            else { printf("trial   %-22s bad: %s\n", TRIAL[i], err); failed = 1; }
        } else printf("trial   %-22s %zu bytes\n", TRIAL[i], b.size);
        hta_fs_unmap(&b);
    }

    hta_session *s = calloc(1, sizeof(*s));
    if (!s) return 1;
    hta_session_load_imported(s, &fs);
    printf("imported: %u characters, %u weapons, ui sounds %s\n", s->imp_char_count,
           s->imp_weap_count, s->ui_sounds.name[0] ? "yes" : "no");
    for (uint32_t i = 0; i < s->imp_char_count; i++) printf("  character %2u %s\n", i, s->imp_char[i].name);
    for (uint32_t i = 0; i < s->imp_weap_count; i++) printf("  weapon    %2u %s\n", i, s->imp_weap[i].name);

    static hta_fs_list_result maps;
    if (!nworld) {
        hta_fs_list(&fs, "maps", ".oalmap", &maps);
        for (unsigned i = 0; i < maps.count && nworld < 64; i++) {
            maps.name[i][strlen(maps.name[i]) - 7] = 0;   /* drop .oalmap */
            worlds[nworld++] = maps.name[i];
        }
    }
    for (unsigned i = 0; i < nworld; i++) {
        char err[HTA_ERRLEN] = "";
        snprintf(s->world, sizeof(s->world), "%s", worlds[i]);
        s->world_loaded = false;
        if (!hta_session_load_world(s, &fs, err, sizeof(err))) {
            printf("world   %-16s FAILED: %s\n", worlds[i], err);
            failed = 1;
            continue;
        }
        printf("world   %-16s %u triangles, %u textures, %u starts, %u breakables, key %08x\n", worlds[i],
               s->mesh.index_count / 3, s->mesh.texture_count, s->world_ext.spawn_count,
               s->world_ext.breakable_count, s->world_ext.key);
        hta_bsp_free(&s->mesh);
        hta_external_map_free(&s->world_ext);
    }
    printf("content: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
