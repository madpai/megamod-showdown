/* Writes the procedural sound bank as WAV files, to listen to it:
 *     megamod-sfxdump OUTDIR [SEED]
 * One file per kind and variant, 22050 Hz mono. */
#include "engine/sfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void le16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s OUTDIR [SEED]\n", argv[0]); return 2; }
    static hta_sfx_bank b;
    if (!hta_sfx_build(&b, argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 7u)) return 1;
    for (int k = 0; k < HTA_SFX_COUNT; k++)
        for (uint32_t v = 0; v < b.variants[k]; v++) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s_%u.wav", argv[1], hta_sfx_name((hta_sfx_kind)k), v);
            FILE *f = fopen(path, "wb");
            if (!f) { perror(path); return 1; }
            uint32_t bytes = b.frames[k][v] * 2u;
            fwrite("RIFF", 1, 4, f); le32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
            le32(f, 16); le16(f, 1); le16(f, 1); le32(f, HTA_SFX_RATE); le32(f, HTA_SFX_RATE * 2); le16(f, 2); le16(f, 16);
            fwrite("data", 1, 4, f); le32(f, bytes);
            for (uint32_t i = 0; i < b.frames[k][v]; i++) le16(f, (uint16_t)b.pcm[k][v][i]);
            fclose(f);
            printf("%s\n", path);
        }
    hta_sfx_free(&b);
    return 0;
}
