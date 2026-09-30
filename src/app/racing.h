#ifndef HTA_APP_RACING_H
#define HTA_APP_RACING_H
#include "session.h"

/* Race slots use existing game unit IDs, preserving roster identity. */
bool hta_racing_join(hta_session *s, uint8_t unit);
void hta_racing_leave(hta_session *s, uint8_t unit);
void hta_racing_tick(hta_session *s, float dt);
void hta_racing_reset(hta_session *s);
#endif
