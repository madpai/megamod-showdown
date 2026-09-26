/* A match's content, loaded by name through hta_fs (app/fs.h), the same on
 * the phone and the desktop: the world package and the imported bodies,
 * weapons and UI sounds. Blood Gulch's Trial maps stay mapped by the
 * platform for now (they are kept mapped for the whole run).
 *
 * Portable: no platform header here. */
#ifndef HTA_APP_CONTENT_H
#define HTA_APP_CONTENT_H

#include <stdbool.h>
#include <stddef.h>
#include "app/fs.h"

typedef struct hta_session hta_session;

/* sounds/ui.oalasset, then characters/ and weapons/ *.oalasset in name
 * order (the index LAN sends), into the session. Once per run. */
void hta_session_load_imported(hta_session *s, const hta_fs *fs);

/* s->world's package, maps/<world>.oalmap -- "imported" is the file the
 * owner picked, external.oalmap in app storage -- into s->world_ext, its
 * mesh moved to s->mesh. */
bool hta_session_load_world(hta_session *s, const hta_fs *fs, char *err, size_t errlen);

#endif
