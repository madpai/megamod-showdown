/* A match's content through hta_fs (app/content.h). Moved from the Android
 * loop's load_imported and load_world_package, which read the APK only. */
#include "app/content.h"
#include "app/compat.h"
#include "app/session.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A missing optional file is quiet; a present one that fails says why. */
static bool load_oal(const hta_fs *fs, const char *name, hta_oal_asset *out, bool optional)
{
    char err[HTA_ERRLEN];
    hta_fs_blob b;
    if (!hta_fs_map(fs, name, &b)) {
        if (!optional) hta_log("[imported] %s: not found", name);
        return false;
    }
    bool ok = hta_oal_load_memory(b.data, b.size, out, err, sizeof(err));
    hta_fs_unmap(&b);
    if (!ok) hta_log("[imported] %s: %s", name, err);
    return ok;
}

void hta_session_load_imported(hta_session *s, const hta_fs *fs)
{
    if (s->imported_loaded) return;
    s->imported_loaded = true;
    load_oal(fs, "sounds/ui.oalasset", &s->ui_sounds, true);
    s->imp_char_count = hta_content_load_dir(fs, "characters", s->imp_char, HTA_MAX_IMPORTED);
    s->imp_weap_count = hta_content_load_dir(fs, "weapons", s->imp_weap, HTA_MAX_IMPORTED);
    for (uint32_t k = 0; k < s->imp_char_count; k++)
        s->imp_voice[k][0] = s->imp_voice[k][1] = HTA_AUDIO_NO_CLIP;
    for (uint32_t k = 0; k < s->imp_weap_count; k++)
        s->imp_clip[k][0] = s->imp_clip[k][1] = HTA_AUDIO_NO_CLIP;
}

uint32_t hta_content_load_dir(const hta_fs *fs, const char *dir, hta_oal_asset *out, uint32_t max)
{
    static hta_fs_list_result names;
    uint32_t count = 0;
    hta_fs_list(fs, dir, ".oalasset", &names);
    for (unsigned i = 0; i < names.count && count < max; i++) {
        char name[128];
        snprintf(name, sizeof(name), "%s/%s", dir, names.name[i]);
        if (!load_oal(fs, name, &out[count], false)) continue;
        hta_log("[imported] %s '%s' (%s), %u models", out[count].kind, out[count].name,
                out[count].display, out[count].model_count);
        count++;
    }
    return count;
}

uint64_t hta_content_fingerprint_fs(const hta_fs *fs)
{
    hta_oal_asset *c = calloc(HTA_MAX_IMPORTED, sizeof(*c));
    hta_oal_asset *w = calloc(HTA_MAX_IMPORTED, sizeof(*w));
    uint64_t fp = 0;
    if (c && w) {
        uint32_t nc = hta_content_load_dir(fs, "characters", c, HTA_MAX_IMPORTED);
        uint32_t nw = hta_content_load_dir(fs, "weapons", w, HTA_MAX_IMPORTED);
        fp = hta_content_fingerprint(c, nc, w, nw);
        for (uint32_t i = 0; i < nc; i++) hta_oal_free(&c[i]);
        for (uint32_t i = 0; i < nw; i++) hta_oal_free(&w[i]);
    }
    free(c); free(w);
    return fp;
}

bool hta_session_load_world(hta_session *s, const hta_fs *fs, char *err, size_t errlen)
{
    for (const char *c = s->world; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) {
            snprintf(err, errlen, "bad map name");
            return false;
        }
    if (!s->world[0]) { snprintf(err, errlen, "no map name"); return false; }
    char name[96];
    if (!strcmp(s->world, "imported")) snprintf(name, sizeof(name), "external.oalmap");
    else snprintf(name, sizeof(name), "maps/%s.oalmap", s->world);
    hta_fs_blob b;
    if (!hta_fs_map(fs, name, &b)) { snprintf(err, errlen, "%s: not found", name); return false; }
    bool ok = hta_external_map_load_memory(b.data, b.size, &s->world_ext, err, errlen);
    hta_fs_unmap(&b);
    if (!ok) return false;
    s->mesh = s->world_ext.mesh;
    memset(&s->world_ext.mesh, 0, sizeof(s->world_ext.mesh));
    s->world_loaded = true;
    hta_log("[world] %s: %u triangles, %u textures, %u starts", s->world,
            s->mesh.index_count / 3, s->mesh.texture_count, s->world_ext.spawn_count);
    return true;
}
