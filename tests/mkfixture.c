/* Writes a synthetic cache file to disk so the CLI tool can be exercised
 * end-to-end without any Halo data. Test-only. */
#include "fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: mkfixture <out.map> [nverts ntris nspawns]\n");
        fprintf(stderr, "       mkfixture <out.map> --grid <nx> <ny> [nspawns]\n");
        return 2;
    }
    if (argc > 2 && strcmp(argv[2], "--grid") == 0) {
        uint32_t gx = argc > 3 ? (uint32_t)atoi(argv[3]) : 64;
        uint32_t gy = argc > 4 ? (uint32_t)atoi(argv[4]) : 64;
        uint32_t gs = argc > 5 ? (uint32_t)atoi(argv[5]) : 6;
        static bsp_fixture gb;
        fixture_build_grid(&gb, gx, gy, gs);
        FILE *gf = fopen(argv[1], "wb");
        if (!gf) { fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
        fwrite(gb.f.buf, 1, gb.f.size, gf);
        fclose(gf);
        printf("wrote %s (%zu bytes, grid %ux%u = %u verts, %u tris)\n",
               argv[1], gb.f.size, gx, gy, gx*gy, gb.triangle_count);
        return 0;
    }
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
