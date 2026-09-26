/* Content by name (app/fs.h): roots answer in order, a mount puts a folder
 * where the APK keeps it, listing is each name once in strcmp order (the
 * index LAN sends for imported bodies), and names cannot climb out. */
#include "app/fs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *dir, const char *rel, const char *text)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, rel);
    char *slash = strrchr(path, '/');
    *slash = 0; mkdir(path, 0700); *slash = '/';
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static bool fake_map(void *ctx, const char *rel, hta_fs_blob *out)
{
    (void)ctx;
    if (strcmp(rel, "sounds/ui.oalasset")) return false;
    static const uint8_t text[] = "apk";
    memset(out, 0, sizeof(*out));
    out->data = text; out->size = 3;
    return true;
}

static void fake_list(void *ctx, const char *rel, hta_fs_name_fn fn, void *user)
{
    (void)ctx;
    if (!strcmp(rel, "characters")) { fn(user, "m.oalasset"); fn(user, "readme.txt"); }
}

static bool is(const hta_fs *fs, const char *name, const char *want)
{
    hta_fs_blob b;
    if (!hta_fs_map(fs, name, &b)) return want == NULL;
    bool ok = want && b.size == strlen(want) && !memcmp(b.data, want, b.size);
    hta_fs_unmap(&b);
    return ok;
}

int main(void)
{
    char a[] = "/tmp/hta_fs_a_XXXXXX", t[] = "/tmp/hta_fs_t_XXXXXX";
    assert(mkdtemp(a) && mkdtemp(t));
    /* a: a bundle, laid out as the APK; t: a Trial folder, mounted at maps/ */
    put(a, "maps/x.oalmap", "bundle-x");
    put(a, "characters/zed.oalasset", "z");
    put(a, "characters/bob.oalasset", "b");
    put(a, "characters/.hidden.oalasset", "h");
    put(a, "characters/notes.txt", "n");
    put(a, "empty.bin", "");
    put(t, "x.oalmap", "trial-x");
    put(t, "bloodgulch.map", "bg");

    hta_fs fs;
    hta_fs_init(&fs);
    assert(hta_fs_mount_dir(&fs, "", a));
    assert(hta_fs_mount_dir(&fs, "maps", t));
    assert(hta_fs_mount(&fs, "", fake_map, fake_list, NULL));

    assert(is(&fs, "maps/x.oalmap", "bundle-x"));        /* the first root wins */
    assert(is(&fs, "maps/bloodgulch.map", "bg"));        /* through the mount */
    assert(is(&fs, "sounds/ui.oalasset", "apk"));        /* a callback root */
    assert(is(&fs, "maps/none.map", NULL));
    assert(is(&fs, "empty.bin", ""));
    /* names cannot climb out or be absolute */
    const char *bad[] = { "", "/etc/passwd", "../x", "maps/../x", "maps//x", "./x", "maps/", "a\\b" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(!hta_fs_name_ok(bad[i]));
        assert(is(&fs, bad[i], NULL));
    }
    assert(hta_fs_name_ok("characters/bob.oalasset"));

    static hta_fs_list_result l;
    hta_fs_list(&fs, "characters", ".oalasset", &l);
    assert(l.count == 3);
    assert(!strcmp(l.name[0], "bob.oalasset") && !strcmp(l.name[1], "m.oalasset") &&
           !strcmp(l.name[2], "zed.oalasset"));
    hta_fs_list(&fs, "maps", ".oalmap", &l);
    assert(l.count == 1 && !strcmp(l.name[0], "x.oalmap"));   /* once, though in two roots */
    hta_fs_list(&fs, "maps", ".map", &l);
    assert(l.count == 1 && !strcmp(l.name[0], "bloodgulch.map"));
    hta_fs_list(&fs, "../", ".map", &l);
    assert(l.count == 0);

    /* a direct path, outside the roots */
    char p[1024];
    snprintf(p, sizeof(p), "%s/bloodgulch.map", t);
    hta_fs_blob b;
    assert(hta_fs_map_path(p, &b) && b.size == 2);
    hta_fs_unmap(&b);
    assert(!b.data);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s %s", a, t);
    assert(system(cmd) == 0);
    puts("fs OK");
    return 0;
}
