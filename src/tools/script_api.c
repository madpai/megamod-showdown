/* megamod-script-api [--json]: the host scripting API this build registers
 * -- version, callbacks, engine verbs with their arguments, libraries,
 * limits and the simulation phase -- printed from the same tables that
 * bind the Lua functions (src/script/script.c), so it cannot drift from
 * them. JSON is the only format; --json is accepted for clarity. */
#include "script/script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--json")) { fprintf(stderr, "usage: %s [--json]\n", argv[0]); return 2; }
    size_t n = hta_script_api_json(NULL, 0);
    char *buf = malloc(n + 1);
    if (!buf) return 1;
    hta_script_api_json(buf, n + 1);
    fputs(buf, stdout);
    free(buf);
    return 0;
}
