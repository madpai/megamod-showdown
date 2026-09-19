/* htasound — inspect the `snd!` tags in a user-supplied Halo cache.
 *
 * Answers the question that decides the whole audio path: what format are the
 * Trial's samples in, and where do the bytes live? Offsets derived from
 * Invader's sound.json; all three struct sizes reconcile (Sound 164,
 * SoundPitchRange 72, SoundPermutation 124), which is the check that catches a
 * mis-ordered field.
 *
 *   htasound <cache.map>                  every snd! tag, one line each
 *   htasound <cache.map> --tag <substr>   full detail for matching tags
 *   htasound <cache.map> --summary        format/rate histogram
 *   htasound <cache.map> --dump <substr> --out <dir>
 *       decode every permutation of the matching tags to a .wav, and report
 *       duration, peak, RMS and the worst sample-to-sample jump at a decoder
 *       block boundary. A wrong block layout shows up there as a crack long
 *       before it is audible anywhere else.
 *
 * You must supply your own legally obtained Halo Trial data.
 */
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/sound.h"

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sound */
#define SND_SAMPLE_RATE      6u
#define SND_CHANNEL_COUNT  108u
#define SND_FORMAT         110u
#define SND_SOUND_CLASS      4u
#define SND_PITCH_RANGES   152u

/* SoundPitchRange */
#define SPR_SIZE            72u
#define SPR_ACTUAL_COUNT    44u
#define SPR_PERMUTATIONS    60u

/* SoundPermutation */
#define SPERM_SIZE         124u
#define SPERM_NAME           0u
#define SPERM_GAIN          36u
#define SPERM_FORMAT        40u
#define SPERM_BUFFER_SIZE   56u
#define SPERM_SAMPLES       64u   /* TagDataOffset: size, external, file_offset */

static const char *fmt_name(uint16_t f)
{
    switch (f) {
    case 0: return "16-bit PCM";
    case 1: return "Xbox ADPCM";
    case 2: return "IMA ADPCM";
    case 3: return "Ogg Vorbis";
    default: return "?";
    }
}

static uint8_t *slurp(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)n;
    return buf;
}

static void write_wav(const char *path, const hta_pcm *p)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "  cannot write %s\n", path); return; }
    uint32_t data_bytes = p->frame_count * p->channels * 2u;
    uint32_t byte_rate = p->sample_rate * p->channels * 2u;
    uint16_t block_align = (uint16_t)(p->channels * 2);
    uint32_t riff = 36u + data_bytes;
    uint32_t fmt_len = 16u;
    uint16_t pcm_tag = 1, chans = p->channels, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt_len, 4, 1, f);
    fwrite(&pcm_tag, 2, 1, f); fwrite(&chans, 2, 1, f);
    fwrite(&p->sample_rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
    fwrite(p->samples, 1, data_bytes, f);
    fclose(f);
}

/* A decoder that gets the block layout wrong still produces plausible noise;
 * what it cannot hide is a step discontinuity every 64 frames. Compare the
 * worst jump ACROSS a block boundary with the worst jump inside one. */
static void dump_permutation(const hta_cache *c, const hta_resource_map *sm,
                             uint32_t tag_id, uint32_t perm,
                             const char *path, const char *outdir)
{
    hta_pcm pcm;
    char err[HTA_ERRLEN] = {0};
    if (!hta_sound_decode(c, sm, tag_id, perm, &pcm, err, sizeof(err))) {
        printf("        decode failed: %s\n", err);
        return;
    }
    uint32_t n = pcm.frame_count * pcm.channels;
    int32_t peak = 0;
    double sumsq = 0.0;
    int32_t worst_edge = 0, worst_inner = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t v = pcm.samples[i];
        int32_t a = v < 0 ? -v : v;
        if (a > peak) peak = a;
        sumsq += (double)v * (double)v;
        if (i) {
            int32_t d = pcm.samples[i] - pcm.samples[i-1];
            if (d < 0) d = -d;
            uint32_t frame = i / pcm.channels;
            if (frame % HTA_XBOX_ADPCM_FRAMES == 0) {
                if (d > worst_edge) worst_edge = d;
            } else if (d > worst_inner) worst_inner = d;
        }
    }
    double rms = n ? sqrt(sumsq / (double)n) : 0.0;

    /* file name: last path component, non-alnum squashed */
    char base[160];
    const char *slash = strrchr(path, '\\');
    snprintf(base, sizeof(base), "%s", slash ? slash + 1 : path);
    for (char *q = base; *q; q++) if (*q == ' ' || *q == '/') *q = '_';
    char out[1024];
    snprintf(out, sizeof(out), "%s/%s_p%u.wav", outdir, base, perm);
    write_wav(out, &pcm);

    printf("        -> %s  %.3f s  %u frames @%u Hz  peak %d (%.1f%%)  rms %.0f\n",
           out, (double)pcm.frame_count / (double)pcm.sample_rate,
           pcm.frame_count, pcm.sample_rate, peak, 100.0 * peak / 32767.0, rms);
    printf("           worst step: across block edges %d, inside blocks %d  %s\n",
           worst_edge, worst_inner,
           worst_edge <= worst_inner ? "(continuous)"
                                     : "(!! edges jump more than the signal does)");
    hta_pcm_free(&pcm);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cache.map> [--tag <substr>] [--summary]\n"
                        "\nYou must supply your own legally obtained Trial data.\n",
                argv[0]);
        return 2;
    }
    const char *want = NULL, *dump = NULL, *outdir = ".";
    bool summary = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--tag") && i + 1 < argc) want = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) { dump = argv[++i]; want = dump; }
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--summary")) summary = true;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }

    size_t size = 0;
    uint8_t *data = slurp(argv[1], &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, size, err, sizeof(err))) {
        fprintf(stderr, "cache: %s\n", err);
        free(data);
        return 1;
    }

    /* sounds.map lives next to the cache on Gearbox Trial. */
    hta_resource_map sm;
    memset(&sm, 0, sizeof(sm));
    uint8_t *sdata = NULL;
    size_t ssz = 0;
    if (dump) {
        char sp[1024];
        snprintf(sp, sizeof(sp), "%s", argv[1]);
        char *slash = strrchr(sp, '/');
        if (slash) snprintf(slash + 1, sizeof(sp) - (size_t)(slash + 1 - sp), "sounds.map");
        else snprintf(sp, sizeof(sp), "sounds.map");
        sdata = slurp(sp, &ssz);
        if (!sdata) {
            fprintf(stderr, "cannot read %s (needed for --dump)\n", sp);
            return 1;
        }
        if (!hta_resource_open_typed(&sm, sdata, ssz, HTA_RESOURCE_SOUNDS, err, sizeof(err))) {
            fprintf(stderr, "sounds.map: %s\n", err);
            return 1;
        }
        printf("sounds.map      %s (%.1f MiB)\n\n", sp, (double)ssz / (1024.0*1024.0));
    }

    uint32_t fmt_count[4] = {0,0,0,0}, other_fmt = 0;
    uint32_t total_snd = 0, total_perms = 0, indexed_snd = 0;
    uint64_t total_bytes = 0;
    uint32_t external_perms = 0, internal_perms = 0;

    const uint32_t SND = HTA_FOURCC('s','n','d','!');
    for (uint32_t i = 0; i < c.tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(&c, i, &t)) continue;
        if (t.primary_class != SND) continue;
        total_snd++;

        char path[256] = "(no path)";
        hta_cache_tag_path(&c, &t, path, sizeof(path));
        bool match = (want == NULL) || (strstr(path, want) != NULL);

        /* An indexed tag's body is not in this cache at all. */
        if (t.indexed) {
            indexed_snd++;
            if (match && want) printf("%-58s  INDEXED (body not in this cache)\n", path);
            continue;
        }

        uint32_t base;
        if (!hta_cache_ptr_to_offset(&c, t.tag_data_ptr, &base)) continue;

        uint16_t rate = 0, chans = 0, sfmt = 0, sclass = 0;
        hta_rd_u16(&c, base + SND_SAMPLE_RATE, &rate);
        hta_rd_u16(&c, base + SND_CHANNEL_COUNT, &chans);
        hta_rd_u16(&c, base + SND_FORMAT, &sfmt);
        hta_rd_u16(&c, base + SND_SOUND_CLASS, &sclass);
        if (sfmt < 4) fmt_count[sfmt]++; else other_fmt++;

        uint32_t pr_count = 0, pr_ptr = 0;
        hta_read_reflexive(&c, base + SND_PITCH_RANGES, &pr_count, &pr_ptr);

        uint32_t perms_here = 0;
        uint64_t bytes_here = 0;
        uint32_t pr_off = 0;
        if (pr_count && hta_cache_ptr_to_offset(&c, pr_ptr, &pr_off)) {
            for (uint32_t p = 0; p < pr_count; p++) {
                uint32_t pe = pr_off + p * SPR_SIZE;
                uint32_t n = 0, pptr = 0, poff = 0;
                hta_read_reflexive(&c, pe + SPR_PERMUTATIONS, &n, &pptr);
                if (!n || !hta_cache_ptr_to_offset(&c, pptr, &poff)) continue;
                for (uint32_t k = 0; k < n; k++) {
                    uint32_t pk = poff + k * SPERM_SIZE;
                    uint16_t pfmt = 0;
                    uint32_t ssize = 0, sext = 0, sfo = 0, bufsz = 0;
                    hta_rd_u16(&c, pk + SPERM_FORMAT, &pfmt);
                    hta_rd_u32(&c, pk + SPERM_BUFFER_SIZE, &bufsz);
                    hta_rd_u32(&c, pk + SPERM_SAMPLES + 0, &ssize);
                    hta_rd_u32(&c, pk + SPERM_SAMPLES + 4, &sext);
                    hta_rd_u32(&c, pk + SPERM_SAMPLES + 8, &sfo);
                    perms_here++;
                    bytes_here += ssize;
                    if (sext & 1u) external_perms++; else internal_perms++;
                    if (match && want) {
                        char pname[33] = {0};
                        hta_rd_bytes(&c, pk + SPERM_NAME, pname, 32);
                        printf("      perm %2u  %-24s %-11s  %8u bytes  %s  @0x%08X"
                               "  pcm buffer %u\n",
                               k, pname, fmt_name(pfmt), ssize,
                               (sext & 1u) ? "sounds.map" : "in cache  ", sfo, bufsz);
                    }
                    if (dump && match)
                        dump_permutation(&c, &sm, t.tag_id, k, path, outdir);
                }
            }
        }
        total_perms += perms_here;
        total_bytes += bytes_here;

        if (match && !summary) {
            if (want)
                printf("%s\n    class %u  %s  %s  %u pitch range(s), %u permutation(s)\n",
                       path, sclass, rate == 0 ? "22050 Hz" : "44100 Hz",
                       chans == 0 ? "mono" : "stereo", pr_count, perms_here);
            else
                printf("  %-56s %-11s %-9s %-6s %2u perm  %6llu KiB\n",
                       path, fmt_name(sfmt), rate == 0 ? "22050 Hz" : "44100 Hz",
                       chans == 0 ? "mono" : "stereo", perms_here,
                       (unsigned long long)(bytes_here / 1024));
        }
    }

    printf("\n%u snd! tags (%u indexed), %u permutations, %.1f MiB of samples\n",
           total_snd, indexed_snd, total_perms, (double)total_bytes / (1024.0*1024.0));
    printf("sample bytes live in: sounds.map %u, this cache %u\n",
           external_perms, internal_perms);
    printf("tag-level formats: PCM %u, Xbox ADPCM %u, IMA ADPCM %u, Ogg %u, other %u\n",
           fmt_count[0], fmt_count[1], fmt_count[2], fmt_count[3], other_fmt);
    free(data);
    return 0;
}
