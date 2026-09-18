/* Writes a synthetic cache file to disk so the CLI tool can be exercised
 * end-to-end without any Halo data. Test-only. */
#include "fixture.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: mkfixture <out.map> [nverts ntris nspawns]\n"); return 2; }
    uint32_t nv = argc > 2 ? (uint32_t)atoi(argv[2]) : 128;
    uint32_t nt = argc > 3 ? (uint32_t)atoi(argv[3]) : 40;
    uint32_t ns = argc > 4 ? (uint32_t)atoi(argv[4]) : 6;
    static bsp_fixture b;
    fixture_build_bsp(&b, nv, nt, ns);
    FILE *f = fopen(argv[1], "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    fwrite(b.f.buf, 1, b.f.size, f);
    fclose(f);
    printf("wrote %s (%zu bytes, %u verts, %u tris, %u spawns)\n", argv[1], b.f.size, nv, nt, ns);
    return 0;
}
