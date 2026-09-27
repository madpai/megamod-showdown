/* The imported rosters' fingerprint (app/compat.h), from real .oalasset
 * files through hta_fs: the same content in different folders matches; a
 * skin does not matter; a stat, a label, an extra or missing character or
 * weapon, or a different order does -- because the network names them by
 * position. */
#include "app/compat.h"
#include "app/content.h"
#include "app/fs.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* A minimal valid model (tests/test_oal_asset.c's layout): one triangle
 * on one bone, one group, a 1x1 texture, no attachments, no clips. The
 * fingerprint ignores models; the loader requires one. */
static void w32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void wf(FILE *f, float v) { fwrite(&v, 4, 1, f); }
static void model(FILE *f)
{
    w32(f, 3); w32(f, 3); w32(f, 1); w32(f, 1); w32(f, 1); w32(f, 0); w32(f, 0); w32(f, 0);
    const float tri[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) wf(f, tri[i][k]);
        wf(f, 0); wf(f, 0); wf(f, 1); wf(f, 0); wf(f, 0);
        uint8_t bi[4] = { 0, 0, 0, 0 }; fwrite(bi, 1, 4, f);
        wf(f, 1); wf(f, 0); wf(f, 0);
    }
    w32(f, 0); w32(f, 1); w32(f, 2);
    w32(f, 0); w32(f, 3); w32(f, 0); w32(f, 0);
    w32(f, 1); w32(f, 1); w32(f, 4);
    uint8_t px[4] = { 200, 100, 50, 255 }; fwrite(px, 1, 4, f);
    char bone[64] = "root"; fwrite(bone, 1, 64, f);
    w32(f, (uint32_t)-1);
    const float id[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
    for (int k = 0; k < 12; k++) wf(f, id[k]);
    wf(f, 0); wf(f, 0); wf(f, 0); wf(f, 0); wf(f, 0); wf(f, 0); wf(f, 1);
}

static void write_asset(const char *root, const char *rel, const char *manifest)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    char *slash = strrchr(path, '/');
    *slash = 0; mkdir(path, 0700); *slash = '/';
    FILE *f = fopen(path, "wb");
    assert(f);
    int models = strstr(manifest, "\"weapon\"") ? 2 : 1;       /* weapons: world + view */
    fwrite("OALA", 1, 4, f);
    w32(f, 1); w32(f, (uint32_t)strlen(manifest)); w32(f, (uint32_t)models); w32(f, 0);
    uint8_t pad[12] = { 0 }; fwrite(pad, 1, 12, f);
    fwrite(manifest, 1, strlen(manifest), f);
    for (int m = 0; m < models; m++) model(f);
    fclose(f);
}

static const char *GOKU = "{\"kind\":\"character\",\"name\":\"goku\",\"display_name\":\"Goku\","
    "\"body_health\":1.5,\"body_speed\":1.2,\"can_fly\":true,\"fly_speed\":9,"
    "\"loadout\":[\"Ki Bolts\",\"Fists\"],\"hero_group\":\"SAIYAN\",\"unique_limit\":1}";
static const char *CHELL = "{\"kind\":\"character\",\"name\":\"chell\",\"display_name\":\"Chell\","
    "\"body_health\":1,\"body_speed\":1}";
static const char *AK = "{\"kind\":\"weapon\",\"name\":\"ak47\",\"display_name\":\"AK-47\","
    "\"base\":\"assault rifle\",\"rounds_per_second\":10,\"damage_scale\":1.6,\"magazine\":30,"
    "\"crosshair\":\"arms\",\"crosshair_size\":12}";

/* A bundle in a fresh folder: goku, chell (at `chell_file`) and the
 * AK-47 from the given manifests, plus an optional extra character and
 * weapon; its fingerprint, as a joiner would compute it. */
static uint64_t fingerprint(const char *ak, const char *goku, const char *extra_char,
                            const char *extra_weap, const char *chell_file)
{
    char root[] = "/tmp/hta_compat_XXXXXX";
    assert(mkdtemp(root));
    write_asset(root, "characters/goku.oalasset", goku);
    write_asset(root, chell_file, CHELL);
    write_asset(root, "weapons/ak47.oalasset", ak);
    if (extra_char) write_asset(root, "characters/zz_extra.oalasset", extra_char);
    if (extra_weap) write_asset(root, "weapons/zz_extra.oalasset", extra_weap);
    hta_fs fs;
    hta_fs_init(&fs);
    hta_fs_mount_content(&fs, NULL, root);
    uint64_t fp = hta_content_fingerprint_fs(&fs);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
    assert(system(cmd) == 0);
    return fp;
}

int main(void)
{
    const char *chell = "characters/chell.oalasset";
    uint64_t base = fingerprint(AK, GOKU, NULL, NULL, chell);
    assert(base != 0 && base != hta_content_fingerprint(NULL, 0, NULL, 0));   /* they loaded */
    /* Different folders, same files: the same (paths are not identity). */
    assert(fingerprint(AK, GOKU, NULL, NULL, chell) == base);

    /* A skin -- here the crosshair -- is not gameplay: still the same. */
    const char *ak_skin = "{\"kind\":\"weapon\",\"name\":\"ak47\",\"display_name\":\"AK-47\","
        "\"base\":\"assault rifle\",\"rounds_per_second\":10,\"damage_scale\":1.6,\"magazine\":30,"
        "\"crosshair\":\"dot\",\"crosshair_size\":20}";
    assert(fingerprint(ak_skin, GOKU, NULL, NULL, chell) == base);

    /* Same names, different numbers: different. */
    const char *ak_hot = "{\"kind\":\"weapon\",\"name\":\"ak47\",\"display_name\":\"AK-47\","
        "\"base\":\"assault rifle\",\"rounds_per_second\":10,\"damage_scale\":2.0,\"magazine\":30,"
        "\"crosshair\":\"arms\",\"crosshair_size\":12}";
    assert(fingerprint(ak_hot, GOKU, NULL, NULL, chell) != base);
    const char *goku_fast = "{\"kind\":\"character\",\"name\":\"goku\",\"display_name\":\"Goku\","
        "\"body_health\":1.5,\"body_speed\":1.3,\"can_fly\":true,\"fly_speed\":9,"
        "\"loadout\":[\"Ki Bolts\",\"Fists\"],\"hero_group\":\"SAIYAN\",\"unique_limit\":1}";
    assert(fingerprint(AK, goku_fast, NULL, NULL, chell) != base);
    /* A loadout names weapons by label: a renamed label changes play. */
    const char *ak_label = "{\"kind\":\"weapon\",\"name\":\"ak47\",\"display_name\":\"AK47\","
        "\"base\":\"assault rifle\",\"rounds_per_second\":10,\"damage_scale\":1.6,\"magazine\":30,"
        "\"crosshair\":\"arms\",\"crosshair_size\":12}";
    assert(fingerprint(ak_label, GOKU, NULL, NULL, chell) != base);

    /* A different set: an extra character or weapon. */
    assert(fingerprint(AK, GOKU, CHELL, NULL, chell) != base);
    assert(fingerprint(AK, GOKU, NULL, AK, chell) != base);

    /* A different order. The roster is the files in name order, and
     * character #N goes over the wire as N: renaming chell's file to sort
     * after goku swaps their positions, which is a real mismatch. */
    assert(fingerprint(AK, GOKU, NULL, NULL, "characters/zchell.oalasset") != base);

    /* Directly: nothing is not zero, and the two rosters do not alias. */
    hta_oal_asset a;
    memset(&a, 0, sizeof(a));
    snprintf(a.kind, sizeof(a.kind), "weapon");
    snprintf(a.name, sizeof(a.name), "x");
    uint64_t none = hta_content_fingerprint(NULL, 0, NULL, 0);
    assert(none != 0 && none != base);
    assert(hta_content_fingerprint(&a, 1, NULL, 0) != hta_content_fingerprint(NULL, 0, &a, 1));
    /* Floats are compared by their exact runtime value (schema 2): the same
     * text parses to the same float everywhere; any difference the game
     * would play by splits peers -- 1.2 vs 1.20001 did not in schema 1. */
    hta_oal_asset b = a;
    a.damage_scale = 1.6f; b.damage_scale = 1.60000002f;      /* the same float */
    assert(hta_content_fingerprint(NULL, 0, &a, 1) == hta_content_fingerprint(NULL, 0, &b, 1));
    a.damage_scale = strtof("1.2", NULL); b.damage_scale = strtof("1.20001", NULL);
    assert(hta_content_fingerprint(NULL, 0, &a, 1) != hta_content_fingerprint(NULL, 0, &b, 1));
    b.damage_scale = nextafterf(a.damage_scale, 2.0f);        /* one ulp */
    assert(hta_content_fingerprint(NULL, 0, &a, 1) != hta_content_fingerprint(NULL, 0, &b, 1));
    a.knockback = 0.0f; b = a; b.knockback = -0.0f;           /* -0 is 0 */
    assert(hta_content_fingerprint(NULL, 0, &a, 1) == hta_content_fingerprint(NULL, 0, &b, 1));

    printf("content fingerprint %016llx\n", (unsigned long long)base);
    puts("compat OK");
    return 0;
}
