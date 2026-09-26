/* Content by name, wherever it lives (docs/DESKTOP_AGENT.md step 2,
 * "files"). The game asks for "maps/ctf_2fort.oalmap" or lists
 * every characters/NAME.oalasset; an hta_fs answers from its roots in order:
 * on the phone the APK's assets and the app's storage, on the desktop the
 * Trial folder and the Asset Lab bundle, mounted where the APK keeps them.
 *
 * Portable (POSIX): no platform header here. A platform with its own
 * storage (the APK) adds a root with callbacks. */
#ifndef HTA_APP_FS_H
#define HTA_APP_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A read-only file mapped into memory. `data` stays valid until unmapped. */
typedef struct {
    const uint8_t *data;
    size_t size;
    void  *base;      /* what to unmap: a page-aligned start before data */
    size_t base_len;
    bool   heap;      /* base is a malloc'd copy (a compressed APK asset) */
} hta_fs_blob;

typedef void (*hta_fs_name_fn)(void *user, const char *name);

typedef struct {
    char   mount[64];          /* "" or "maps": names under it resolve here */
    char   dir[512];           /* a directory, or "" for a callback root */
    bool (*map)(void *ctx, const char *rel, hta_fs_blob *out);
    void (*list)(void *ctx, const char *rel_dir, hta_fs_name_fn fn, void *user);
    void  *ctx;
} hta_fs_root;

#define HTA_FS_MAX_ROOTS 8
typedef struct {
    hta_fs_root root[HTA_FS_MAX_ROOTS];
    unsigned count;
} hta_fs;

void hta_fs_init(hta_fs *fs);
/* `dir` answers for names under `mount` ("" for every name):
 * mount "maps", dir "/x/trial" makes "maps/ui.map" /x/trial/ui.map. */
bool hta_fs_mount_dir(hta_fs *fs, const char *mount, const char *dir);
bool hta_fs_mount(hta_fs *fs, const char *mount,
                  bool (*map)(void *, const char *, hta_fs_blob *),
                  void (*list)(void *, const char *, hta_fs_name_fn, void *), void *ctx);

/* The desktop's content, laid out as the phone's APK: the Asset Lab bundle
 * (NAME.oalmap at its top, characters/, weapons/, sounds/) answers at the
 * top and under maps/, the Trial folder (bloodgulch.map, bitmaps.map,
 * sounds.map, ui.map) under maps/. Either may be NULL. From the command
 * line or HTA_TRIAL_DIR / HTA_BUNDLE_DIR. */
void hta_fs_mount_content(hta_fs *fs, const char *trial_dir, const char *bundle_dir);

/* The first root that has `name` maps it. Names are relative, '/'-separated,
 * with no "..", no leading '/' and no empty part. */
bool hta_fs_map(const hta_fs *fs, const char *name, hta_fs_blob *out);
void hta_fs_unmap(hta_fs_blob *b);
/* Any file by its own path, outside the roots (a --map given on a desktop
 * command line). */
bool hta_fs_map_path(const char *path, hta_fs_blob *out);

/* The file names in `dir` ending in `suffix`, across every root, each once
 * (an earlier root wins), sorted by strcmp: the same order on every device,
 * which LAN relies on -- imported characters are sent by index. */
#define HTA_FS_MAX_LIST 128
typedef struct {
    char name[HTA_FS_MAX_LIST][96];
    unsigned count;
} hta_fs_list_result;
void hta_fs_list(const hta_fs *fs, const char *dir, const char *suffix, hta_fs_list_result *out);

bool hta_fs_name_ok(const char *name);

#endif
