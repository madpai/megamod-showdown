/* snd! parsing and Xbox ADPCM decoding.
 *
 * The decoder part runs anywhere. The tag part needs a real cache: pass
 * bloodgulch.map as argv[1]; sounds.map is read from beside it.
 */
#include "asset/cache.h"
#include "asset/sound.h"
#include "asset/effect.h"
#include "asset/weapon.h"
#include "asset/biped.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

int main(int argc, char **argv)
{
    printf("sound: Xbox ADPCM + snd! tags\n");

    /* --- the decoder, on data built by hand ----------------------------- */
    printf("\n[xbox adpcm block]\n");
    {
        uint8_t block[HTA_XBOX_ADPCM_BLOCK];
        int16_t pcm[HTA_XBOX_ADPCM_FRAMES];

        /* Seed 1000, step index 0, all codes 0. At index 0 the step is 7 and
         * code 0 means delta = 7 >> 3 = 0, while the index walks back to a
         * clamped 0 -- so the predictor must not move at all. That pins the
         * shift exactly: any rounding fudge in the delta shows up here. */
        memset(block, 0, sizeof(block));
        block[0] = (uint8_t)(1000 & 0xFF);
        block[1] = (uint8_t)(1000 >> 8);
        block[2] = 0;
        CHECK(hta_xbox_adpcm_decode(block, sizeof(block), 1, pcm, HTA_XBOX_ADPCM_FRAMES),
              "one mono block decodes");
        CHECK(pcm[0] == 1000, "the block's seed sample comes out first");
        int flat = 1;
        for (uint32_t i = 0; i < HTA_XBOX_ADPCM_FRAMES; i++)
            if (pcm[i] != 1000) flat = 0;
        CHECK(flat, "code 0 at step index 0 moves the predictor by exactly nothing");

        /* Code 7 is the largest positive step: delta = s/8 + s + s/2 + s/4,
         * and the index jumps by 8. From seed 0 at index 0 that is
         * 0 + 7 + 3 + 1 = 11, then step 16 gives 2 + 16 + 8 + 4 = 30. */
        memset(block, 0x77, sizeof(block));
        block[0] = 0; block[1] = 0; block[2] = 0; block[3] = 0;
        CHECK(hta_xbox_adpcm_decode(block, sizeof(block), 1, pcm, HTA_XBOX_ADPCM_FRAMES),
              "a block of maximum positive codes decodes");
        CHECK(pcm[0] == 0, "starts at the seed");
        CHECK(pcm[1] == 11, "first delta is 11 at step 7");
        CHECK(pcm[2] == 41, "second delta is 30 at step 16 (the index advanced by 8)");
        int rising = 1;
        for (uint32_t i = 1; i < HTA_XBOX_ADPCM_FRAMES; i++)
            if (pcm[i] < pcm[i-1]) rising = 0;
        CHECK(rising, "and it climbs monotonically from there");
        CHECK(pcm[HTA_XBOX_ADPCM_FRAMES-1] == 32767,
              "63 maximum steps saturate at the int16 ceiling, clamped not wrapped");

        /* A short buffer must be refused, not overrun. */
        CHECK(!hta_xbox_adpcm_decode(block, sizeof(block), 1, pcm, 8),
              "too small an output buffer is refused");
        CHECK(!hta_xbox_adpcm_decode(block, sizeof(block) - 1, 1, pcm,
                                     HTA_XBOX_ADPCM_FRAMES),
              "a partial block is refused");
        CHECK(!hta_xbox_adpcm_decode(block, sizeof(block), 3, pcm,
                                     HTA_XBOX_ADPCM_FRAMES),
              "3 channels is refused");
    }

    if (argc < 2) {
        printf("\n  skip: no map path (pass bloodgulch.map for the tag checks)\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    /* --- the tags, against the user's own data --------------------------- */
    size_t sz = 0;
    uint8_t *data = slurp(argv[1], &sz);
    CHECK(data != NULL, "map reads");
    if (!data) return 1;
    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    CHECK(hta_cache_open(&c, data, sz, err, sizeof(err)), "cache opens");

    /* sounds.map sits beside the cache. */
    char sp[1024];
    snprintf(sp, sizeof(sp), "%s", argv[1]);
    char *slash = strrchr(sp, '/');
    if (slash) snprintf(slash + 1, sizeof(sp) - (size_t)(slash + 1 - sp), "sounds.map");
    else snprintf(sp, sizeof(sp), "sounds.map");
    size_t ssz = 0;
    uint8_t *sdata = slurp(sp, &ssz);
    hta_resource_map sm;
    memset(&sm, 0, sizeof(sm));
    int have_sounds = 0;
    if (sdata) {
        have_sounds = hta_resource_open_typed(&sm, sdata, ssz, HTA_RESOURCE_SOUNDS,
                                              err, sizeof(err));
        CHECK(have_sounds, "sounds.map opens as resource type 2");
        /* Handing it bitmaps.map by mistake must be caught, not misread. */
        hta_resource_map wrong;
        CHECK(!hta_resource_open_typed(&wrong, sdata, ssz, HTA_RESOURCE_BITMAPS,
                                       err, sizeof(err)),
              "sounds.map is rejected when bitmaps.map was expected");
    } else {
        printf("  skip: no sounds.map beside the cache\n");
    }

    printf("\n[the gun's own gunshot]\n");
    {
        hta_weapon_def wd;
        memset(&wd, 0, sizeof(wd));
        CHECK(hta_weapon_load_default(&c, NULL, &wd, NULL, err, sizeof(err)),
              "default weapon loads");
        CHECK(wd.firing_fx_id != 0, "the trigger names a firing effect");

        /* Halo does not put the gunshot on the weapon: it hangs off the
         * trigger's firing `effe`, among that effect's parts. */
        uint32_t snd = hta_effect_first_sound(&c, wd.firing_fx_id);
        CHECK(snd != 0, "the firing effect contains a snd!");

        int32_t ti = hta_cache_find_tag_by_id(&c, snd);
        CHECK(ti >= 0, "that snd! is in this cache");
        char path[256] = "";
        if (ti >= 0) {
            hta_tag_entry t;
            if (hta_cache_tag(&c, (uint32_t)ti, &t)) {
                hta_cache_tag_path(&c, &t, path, sizeof(path));
                char cls[5];
                hta_fourcc_str(t.primary_class, cls);
                CHECK(t.primary_class == HTA_FOURCC('s','n','d','!'),
                      "and it really is a snd! tag");
                printf("    %s -> %s\n", wd.path, path);
            }
        }
        CHECK(strstr(path, "fire") != NULL, "its path is a firing sound");

        hta_sound_info info;
        CHECK(hta_sound_info_load(&c, snd, &info, err, sizeof(err)),
              "the snd! header parses");
        printf("    format %u, %s, %s, %u permutations\n", info.format,
               info.sample_rate ? "44100 Hz" : "22050 Hz",
               info.channels ? "stereo" : "mono", info.permutations);
        CHECK(info.permutations >= 1, "it has at least one permutation");
        CHECK(info.format == HTA_SND_FMT_XBOX,
              "Trial sound effects are Xbox ADPCM");

        if (have_sounds) {
            uint32_t decoded = 0;
            double shortest = 1e9, longest = 0.0;
            for (uint32_t p = 0; p < info.permutations; p++) {
                hta_pcm pcm;
                if (!hta_sound_decode(&c, &sm, snd, p, &pcm, err, sizeof(err))) {
                    printf("    permutation %u: %s\n", p, err);
                    continue;
                }
                double secs = (double)pcm.frame_count / (double)pcm.sample_rate;
                if (secs < shortest) shortest = secs;
                if (secs > longest) longest = secs;
                /* Silence would decode "successfully" and tell us nothing. */
                double sumsq = 0.0;
                uint32_t n = pcm.frame_count * pcm.channels;
                for (uint32_t i = 0; i < n; i++)
                    sumsq += (double)pcm.samples[i] * (double)pcm.samples[i];
                double rms = n ? sqrt(sumsq / (double)n) : 0.0;
                if (rms > 100.0) decoded++;
                hta_pcm_free(&pcm);
            }
            printf("    %u/%u permutations decoded to audible PCM, %.2f-%.2f s\n",
                   decoded, info.permutations, shortest, longest);
            CHECK(decoded == info.permutations,
                  "every permutation decodes to something audible");
            CHECK(longest > 0.05 && longest < 5.0,
                  "a gunshot is a plausible length");

            /* The permutation index is bounds-checked, not trusted. */
            hta_pcm bad;
            CHECK(!hta_sound_decode(&c, &sm, snd, info.permutations + 99u, &bad,
                                    err, sizeof(err)),
                  "an out-of-range permutation is refused");
            /* Without sounds.map the failure has to be clear, not a crash. */
            CHECK(!hta_sound_decode(&c, NULL, snd, 0, &bad, err, sizeof(err)),
                  "decoding without sounds.map fails cleanly");
        }
    }

    printf("\n[footstep sounds from the biped's own tag]\n");
    {
        hta_player_physics phys;
        hta_player_physics_defaults(&phys);
        CHECK(hta_player_physics_load(&phys, &c, err, sizeof(err)),
              "cyborg physics load");
        CHECK(phys.footsteps_id != 0, "the biped names a footsteps tag");

        int32_t fi = hta_cache_find_tag_by_id(&c, phys.footsteps_id);
        CHECK(fi >= 0, "and it is in this cache");
        if (fi >= 0) {
            hta_tag_entry ft;
            hta_cache_tag(&c, (uint32_t)fi, &ft);
            char fpath[192] = "";
            hta_cache_tag_path(&c, &ft, fpath, sizeof(fpath));
            printf("    %s\n", fpath);
            CHECK(ft.primary_class == HTA_FOURCC('f','o','o','t'),
                  "it really is a material_effects tag");
        }

        /* Blood Gulch's collision is sand, stone and metal, so those three
         * have to resolve or the map is silent underfoot. */
        const uint8_t want[3] = { 1u, 2u, 7u };   /* sand, stone, metal thick */
        const char *name[3] = { "sand", "stone", "metal thick" };
        uint32_t got = 0;
        for (int i = 0; i < 3; i++) {
            uint32_t sid = hta_material_effect_sound(&c, phys.footsteps_id, 0u, want[i]);
            char sp2[192] = "(none)";
            if (sid) {
                int32_t si = hta_cache_find_tag_by_id(&c, sid);
                if (si >= 0) {
                    hta_tag_entry st;
                    hta_cache_tag(&c, (uint32_t)si, &st);
                    hta_cache_tag_path(&c, &st, sp2, sizeof(sp2));
                    got++;
                }
            }
            printf("    %-12s -> %s\n", name[i], sp2);
        }
        CHECK(got == 3, "the materials Blood Gulch is made of all have a footstep");

        /* A material with no sound must come back 0, not a wrong one. */
        CHECK(hta_material_effect_sound(&c, phys.footsteps_id, 0u, 200u) == 0,
              "an out-of-range material is refused");
        CHECK(hta_material_effect_sound(&c, 0u, 0u, 1u) == 0,
              "no footsteps tag means no sound");

        if (have_sounds) {
            uint32_t sid = hta_material_effect_sound(&c, phys.footsteps_id, 0u, 1u);
            hta_pcm pcm;
            CHECK(sid && hta_sound_decode(&c, &sm, sid, 0, &pcm, err, sizeof(err)),
                  "the sand footstep decodes from sounds.map");
            if (sid && pcm.samples) {
                printf("    sand footstep: %.3f s\n",
                       (double)pcm.frame_count / (double)pcm.sample_rate);
                CHECK(pcm.frame_count > 0 && pcm.frame_count < pcm.sample_rate * 3u,
                      "and is a footstep's length, not a song");
                hta_pcm_free(&pcm);
            }
        }
    }

    printf("\n[impact sounds from the projectile's own tag]\n");
    {
        hta_weapon_def wd;
        memset(&wd, 0, sizeof(wd));
        CHECK(hta_weapon_load_default(&c, NULL, &wd, NULL, err, sizeof(err)),
              "weapon loads");
        CHECK(wd.projectile_id != 0, "the trigger names a projectile");

        /* Halo keeps one response per material ON THE PROJECTILE, each
         * naming an effect, and the sound is that effect's. */
        const uint8_t mats[4] = { 1u, 2u, 7u, 9u };
        const char *nm[4] = { "sand", "stone", "metal thick", "glass" };
        uint32_t resolved = 0, distinct = 0;
        uint32_t seen[4] = {0,0,0,0};
        for (int i = 0; i < 4; i++) {
            uint32_t sid = hta_projectile_impact_sound(&c, wd.projectile_id, mats[i]);
            char sp2[192] = "(none)";
            if (sid) {
                int32_t si = hta_cache_find_tag_by_id(&c, sid);
                if (si >= 0) {
                    hta_tag_entry st;
                    hta_cache_tag(&c, (uint32_t)si, &st);
                    hta_cache_tag_path(&c, &st, sp2, sizeof(sp2));
                    resolved++;
                }
                int dup = 0;
                for (int k = 0; k < 4; k++) if (seen[k] == sid) dup = 1;
                if (!dup) seen[distinct++] = sid;
            }
            printf("    %-12s -> %s\n", nm[i], sp2);
        }
        CHECK(resolved == 4, "every material Blood Gulch is made of has an impact");
        CHECK(distinct >= 3, "and they are not all the same sound");

        CHECK(hta_projectile_impact_sound(&c, wd.projectile_id, 200u) == 0,
              "an out-of-range material is refused");
        CHECK(hta_projectile_impact_sound(&c, 0u, 1u) == 0,
              "no projectile means no impact");

        if (have_sounds) {
            uint32_t sid = hta_projectile_impact_sound(&c, wd.projectile_id, 1u);
            hta_pcm pcm;
            CHECK(sid && hta_sound_decode(&c, &sm, sid, 0, &pcm, err, sizeof(err)),
                  "the sand impact decodes from sounds.map");
            if (sid && pcm.samples) {
                printf("    sand impact: %.3f s\n",
                       (double)pcm.frame_count / (double)pcm.sample_rate);
                CHECK(pcm.frame_count < pcm.sample_rate * 3u,
                      "and is an impact's length");
                hta_pcm_free(&pcm);
            }
        }
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
