/* The session header is portable: it compiles on the host with no Android
 * header, so the match state can move out of platform_android.c (loop
 * extraction, docs/ENGINE_ARCHITECTURE.md). */
#include "app/session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    hta_session *s = calloc(1, sizeof(*s));
    assert(s);
    s->me = -1;
    s->game_on = false;
    hta_player_init(&s->player);
    hta_camera_init(&s->cam);
    printf("hta_session: %zu bytes, portable\n", sizeof(*s));
    free(s);
    puts("session OK");
    return 0;
}
