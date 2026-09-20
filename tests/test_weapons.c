/* The playable roster: every weapon the map places must be usable.
 *
 * Needs the Trial's own bloodgulch.map -- there is nothing synthetic to
 * check here, the whole point is that the tags say what we think they say.
 */
#include "asset/cache.h"
#include "asset/weapon.h"
#include "engine/viewmodel.h"
#include "engine/ammo.h"
#include "asset/effect.h"
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

static const char *leaf(const char *path)
{
    const char *s = strrchr(path, '\\');
    return s ? s + 1 : path;
}

int main(int argc, char **argv)
{
    printf("weapons\n");
    if (argc < 2) {
        printf("  skip: pass bloodgulch.map\n");
        return 0;
    }

    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }

    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) {
        printf("  FAIL: %s\n", err); return 1;
    }

    /* bitmaps.map beside the cache, for the meshes' textures. */
    char bmp[512];
    snprintf(bmp, sizeof(bmp), "%s", argv[1]);
    char *bslash = strrchr(bmp, '/');
    if (bslash) snprintf(bslash + 1, sizeof(bmp) - (size_t)(bslash + 1 - bmp), "bitmaps.map");
    else snprintf(bmp, sizeof(bmp), "bitmaps.map");
    size_t bsz = 0;
    uint8_t *bdata = slurp(bmp, &bsz);
    hta_resource_map bm;
    int have_bitmaps = bdata && hta_resource_open(&bm, bdata, bsz, err, sizeof(err));

    uint32_t ids[32];
    uint32_t count = hta_weapon_list_playable(&c, ids, 32);
    printf("\n[roster]\n");
    printf("  %u playable weapon(s)\n", count);
    CHECK(count >= 8, "the roster has at least the eight the map places");

    printf("\n[every weapon animates]\n");
    /* The flamethrower's antr genuinely has no fire clip -- it is the one
     * weapon Bungie animated with a held pose and a particle jet instead.
     * Every other weapon must have one, and before the clip-name fallbacks
     * only the assault rifle did: it is alone in calling its clip "firing"
     * where the rest say "fire-1". */
    int no_fire = 0, zoomers = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, have_bitmaps ? &bm : NULL, ids[i], &w, NULL, err, sizeof(err))) {
            printf("  FAIL: %s\n", err); failures++; checks++; continue;
        }
        hta_viewmodel vm;
        if (!hta_viewmodel_load(&vm, &c, have_bitmaps ? &bm : NULL, &w,
                                err, sizeof(err))) {
            printf("  FAIL: no viewmodel for %s: %s\n", leaf(w.path), err);
            failures++; checks++; continue;
        }
        printf("  %-16s ready %c  idle %c  fire %c  reload %c  melee %c",
               leaf(w.path),
               vm.clip[HTA_VM_READY]  >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_IDLE]   >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_FIRE]   >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_RELOAD] >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_MELEE]  >= 0 ? 'y' : '-');
        if (w.zoom_levels > 0) {
            printf("   zoom %dx", w.zoom_levels);
            zoomers++;
        }
        printf("\n");

        if (vm.clip[HTA_VM_FIRE] < 0) {
            no_fire++;
            if (!strstr(w.path, "flamethrower")) {
                printf("  FAIL: %s has no fire clip\n", leaf(w.path));
                failures++;
            }
            checks++;
        } else { CHECK(1, "has a firing animation"); }

        CHECK(vm.clip[HTA_VM_IDLE] >= 0, "  and an idle");
        CHECK(vm.clip[HTA_VM_MELEE] >= 0, "  and a melee");
        hta_viewmodel_free(&vm);
    }
    CHECK(no_fire <= 1, "at most one weapon lacks a fire clip");

    printf("\n[zoom comes from the tag]\n");
    /* `zoom levels` at weap+986, `zoom magnification range` at +988. Only
     * three weapons in the Trial zoom, and only the sniper does it twice. */
    struct { const char *name; int levels; float lo, hi; } want[] = {
        { "pistol",          1, 2.0f, 2.0f },
        { "rocket launcher", 1, 2.0f, 2.0f },
        { "sniper rifle",    2, 2.0f, 8.0f },
    };
    int checked = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); k++) {
            if (strcmp(leaf(w.path), want[k].name)) continue;
            printf("  %-16s %d level(s), %.1fx .. %.1fx\n",
                   leaf(w.path), w.zoom_levels, w.zoom_mag[0], w.zoom_mag[1]);
            CHECK(w.zoom_levels == want[k].levels, "the tagged zoom level count");
            CHECK(fabsf(w.zoom_mag[0] - want[k].lo) < 0.01f, "  low magnification");
            CHECK(fabsf(w.zoom_mag[1] - want[k].hi) < 0.01f, "  high magnification");
            checked++;
        }
        if (w.zoom_levels == 0) {
            /* A weapon that does not zoom must not report a magnification
             * we would then divide the field of view by. */
            CHECK(w.zoom_mag[0] == 0.0f || w.zoom_mag[0] == 1.0f,
                  "a non-zooming weapon has no magnification");
        }
    }
    CHECK(checked == 3, "all three zooming weapons are in the roster");
    CHECK(zoomers == 3, "and nothing else claims to zoom");

    printf("\n[every weapon makes a noise]\n");
    /* Almost every weapon's gunshot hangs off its trigger's firing effect.
     * The flamethrower's firing effect has no sound in it at all -- its roar
     * is a looping sound attached to the weapon object's `primary trigger`,
     * and that is the only weapon we should fall back to it for. The plasma
     * pistol hangs its OVERCHARGE whine on that same marker, so falling back
     * unconditionally would make it hum every time it fired. */
    int loops = 0, silent = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        uint32_t shot = hta_effect_first_sound(&c, w.firing_fx_id);
        hta_loop_sound ls;
        bool has_loop = hta_object_loop_sound(&c, ids[i], "primary trigger", &ls);
        if (!shot) {
            silent++;
            printf("  %-16s no effect sound; loop %s\n", leaf(w.path),
                   has_loop ? "yes" : "NO");
            CHECK(has_loop, "a weapon with no shot sound has a looping one");
            CHECK(ls.loop != 0 || ls.start != 0, "  and the track names a sound");
            CHECK(ls.gain > 0.0f, "  at an audible gain");
            CHECK(strstr(w.path, "flamethrower") != NULL,
                  "  and it is the flamethrower");
            loops++;
        }
        if (has_loop && shot) {
            /* Fine to exist -- it just must not be treated as the gunshot. */
            printf("  %-16s has both a shot sound and a loop (not the gunshot)\n",
                   leaf(w.path));
        }
    }
    CHECK(silent == 1, "exactly one weapon has no firing-effect sound");
    CHECK(loops == 1, "and exactly one continuous weapon");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
