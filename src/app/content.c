/* A match's content through hta_fs (app/content.h). Moved from the Android
 * loop's load_imported and load_world_package, which read the APK only. */
#include "app/content.h"
#include "app/session.h"
#include "platform/platform.h"
#include <stdio.h>
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
    static const char *const DIRS[2] = { "characters", "weapons" };
    static hta_fs_list_result names;
    for (int d = 0; d < 2; d++) {
        hta_fs_list(fs, DIRS[d], ".oalasset", &names);
        for (unsigned i = 0; i < names.count; i++) {
            if ((d ? s->imp_weap_count : s->imp_char_count) >= HTA_MAX_IMPORTED) break;
            hta_oal_asset *slot = d ? &s->imp_weap[s->imp_weap_count] : &s->imp_char[s->imp_char_count];
            char name[128];
            snprintf(name, sizeof(name), "%s/%s", DIRS[d], names.name[i]);
            if (!load_oal(fs, name, slot, false)) continue;
            if (d) {
                uint32_t k = s->imp_weap_count++;
                s->imp_clip[k][0] = s->imp_clip[k][1] = HTA_AUDIO_NO_CLIP;
            } else {
                s->imp_voice[s->imp_char_count][0] = s->imp_voice[s->imp_char_count][1] = HTA_AUDIO_NO_CLIP;
                s->imp_char_count++;
            }
            hta_log("[imported] %s '%s' (%s), %u models", slot->kind, slot->name, slot->display, slot->model_count);
        }
    }
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
