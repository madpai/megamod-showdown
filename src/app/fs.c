/* Content by name across ordered roots (app/fs.h). */
#include "app/fs.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void hta_fs_init(hta_fs *fs)
{
    if (fs) memset(fs, 0, sizeof(*fs));
}

static bool mount_ok(const char *mount)
{
    return mount && (!*mount || hta_fs_name_ok(mount)) && strlen(mount) < sizeof(((hta_fs_root *)0)->mount);
}

bool hta_fs_mount_dir(hta_fs *fs, const char *mount, const char *dir)
{
    if (!fs || fs->count >= HTA_FS_MAX_ROOTS || !mount_ok(mount) || !dir || !*dir ||
        strlen(dir) >= sizeof(fs->root[0].dir)) return false;
    hta_fs_root *r = &fs->root[fs->count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->mount, sizeof(r->mount), "%s", mount);
    snprintf(r->dir, sizeof(r->dir), "%s", dir);
    return true;
}

bool hta_fs_mount(hta_fs *fs, const char *mount,
                  bool (*map)(void *, const char *, hta_fs_blob *),
                  void (*list)(void *, const char *, hta_fs_name_fn, void *), void *ctx)
{
    if (!fs || fs->count >= HTA_FS_MAX_ROOTS || !mount_ok(mount) || !map) return false;
    hta_fs_root *r = &fs->root[fs->count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->mount, sizeof(r->mount), "%s", mount);
    r->map = map; r->list = list; r->ctx = ctx;
    return true;
}

void hta_fs_mount_content(hta_fs *fs, const char *trial_dir, const char *bundle_dir)
{
    if (bundle_dir && *bundle_dir) {
        hta_fs_mount_dir(fs, "", bundle_dir);
        hta_fs_mount_dir(fs, "maps", bundle_dir);
    }
    if (trial_dir && *trial_dir) hta_fs_mount_dir(fs, "maps", trial_dir);
}

bool hta_fs_name_ok(const char *name)
{
    if (!name || !*name || *name == '/') return false;
    const char *p = name;
    while (*p) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == 0 || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return false;
        if (!e) break;
        p = e + 1;
    }
    return name[strlen(name) - 1] != '/' && !strchr(name, '\\');
}

/* The part of `name` under the root's mount, or NULL. `name` itself equal
 * to the mount is "" (for listing the mount's own directory). */
static const char *under(const hta_fs_root *r, const char *name)
{
    size_t m = strlen(r->mount);
    if (!m) return name;
    if (strncmp(name, r->mount, m)) return NULL;
    if (!name[m]) return "";
    return name[m] == '/' ? name + m + 1 : NULL;
}

bool hta_fs_map_path(const char *path, hta_fs_blob *out)
{
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return false; }
    if (st.st_size == 0) {
        close(fd);
        static const uint8_t empty[1];
        out->data = empty;
        return true;
    }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return false;
    out->data = p; out->size = (size_t)st.st_size;
    out->base = p; out->base_len = (size_t)st.st_size;
    return true;
}

bool hta_fs_map(const hta_fs *fs, const char *name, hta_fs_blob *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!fs || !hta_fs_name_ok(name)) return false;
    for (unsigned i = 0; i < fs->count; i++) {
        const hta_fs_root *r = &fs->root[i];
        const char *rel = under(r, name);
        if (!rel || !*rel) continue;
        if (r->map) {
            if (r->map(r->ctx, rel, out)) return true;
            continue;
        }
        char path[1024];
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", r->dir, rel) >= sizeof(path)) continue;
        if (hta_fs_map_path(path, out)) return true;
    }
    return false;
}

void hta_fs_unmap(hta_fs_blob *b)
{
    if (!b) return;
    if (b->base && b->heap) free(b->base);
    else if (b->base) munmap(b->base, b->base_len);
    memset(b, 0, sizeof(*b));
}

typedef struct {
    hta_fs_list_result *out;
    const char *suffix;
} list_ctx;

static void add_name(void *user, const char *fn)
{
    list_ctx *c = user;
    size_t n = strlen(fn), sn = strlen(c->suffix);
    if (n <= sn || strcmp(fn + n - sn, c->suffix) || n >= sizeof(c->out->name[0]) ||
        strchr(fn, '/') || fn[0] == '.') return;
    for (unsigned i = 0; i < c->out->count; i++)
        if (!strcmp(c->out->name[i], fn)) return;
    if (c->out->count >= HTA_FS_MAX_LIST) return;
    snprintf(c->out->name[c->out->count++], sizeof(c->out->name[0]), "%s", fn);
}

static int by_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

void hta_fs_list(const hta_fs *fs, const char *dir, const char *suffix, hta_fs_list_result *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!fs || !hta_fs_name_ok(dir)) return;
    list_ctx c = { out, suffix ? suffix : "" };
    for (unsigned i = 0; i < fs->count; i++) {
        const hta_fs_root *r = &fs->root[i];
        const char *rel = under(r, dir);
        if (!rel) continue;
        if (r->map) {
            if (r->list) r->list(r->ctx, rel, add_name, &c);
            continue;
        }
        char path[1024];
        if ((size_t)snprintf(path, sizeof(path), "%s%s%s", r->dir, *rel ? "/" : "", rel) >= sizeof(path))
            continue;
        DIR *d = opendir(path);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            char full[1400];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
            if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) add_name(&c, e->d_name);
        }
        closedir(d);
    }
    qsort(out->name, out->count, sizeof(out->name[0]), by_name);
}
