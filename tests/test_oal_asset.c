/* .oalasset v1 loader, posing, skinning, holding -- synthetic packages only.
 *
 * A two-bone "body" whose hand bone turns 90 degrees about Z in its idle
 * clip, and a one-bone "weapon" sharing the hand's name, built byte by byte
 * here the way Asset Lab writes them.
 */
#include "asset/oal_asset.h"
#include "game/imported.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

typedef struct { uint8_t *d; size_t n, cap; } buf;
static void put(buf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->d = realloc(b->d, b->cap); }
    memcpy(b->d + b->n, p, n); b->n += n;
}
static void u32(buf *b, uint32_t v) { put(b, &v, 4); }
static void f32(buf *b, float v) { put(b, &v, 4); }
static void name(buf *b, const char *s, size_t field)
{
    char tmp[64] = { 0 };
    snprintf(tmp, sizeof(tmp), "%s", s);
    put(b, tmp, field);
}

/* One model: a triangle, `bones` bones in a chain along +Z (one unit each),
 * the triangle on the last bone, and an "idle" clip that turns the last
 * bone 90 degrees about Z when `turn`. `source_frame`: the last bone has a Source world model's weapon-bone
 * frame (rows 1 0 0 / 0 0 -1 / 0 1 0: 90 degrees about X), as CS:S's do. */
static const char *grip_attachment;   /* one attachment on the last bone, identity */

static void model(buf *b, int bones, const char *const *names, bool turn, bool source_frame)
{
    u32(b, 3); u32(b, 3); u32(b, 1); u32(b, 1); u32(b, (uint32_t)bones); u32(b, grip_attachment ? 1 : 0);
    u32(b, turn ? 1 : 0); u32(b, 0);
    const float tri[3][3] = { { 1, 0, 1 }, { 0, 1, 1 }, { 0, 0, 1 } };
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) f32(b, tri[i][k] * (float)(bones - 1));
        f32(b, 0); f32(b, 0); f32(b, 1); f32(b, 0); f32(b, 0);
        uint8_t bi[4] = { (uint8_t)(bones - 1), 0, 0, 0 };
        put(b, bi, 4);
        f32(b, 1); f32(b, 0); f32(b, 0);
    }
    u32(b, 0); u32(b, 1); u32(b, 2);
    u32(b, 0); u32(b, 3); u32(b, 0); u32(b, 0);
    u32(b, 1); u32(b, 1); u32(b, 4);
    uint8_t px[4] = { 200, 100, 50, 255 };
    put(b, px, 4);
    for (int i = 0; i < bones; i++) {
        name(b, names[i], 64);
        u32(b, (uint32_t)(i - 1));
        bool rx = source_frame && i == bones - 1;
        const float plain[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,(float)-i };
        const float turned[12] = { 1,0,0,0, 0,0,1,(float)-i, 0,-1,0,0 };
        for (int k = 0; k < 12; k++) f32(b, rx ? turned[k] : plain[k]);
        f32(b, 0); f32(b, 0); f32(b, i ? 1.0f : 0.0f);
        if (rx) { f32(b, sinf(0.7853982f)); f32(b, 0); f32(b, 0); f32(b, cosf(0.7853982f)); }
        else { f32(b, 0); f32(b, 0); f32(b, 0); f32(b, 1); }
    }
    if (grip_attachment) {
        name(b, grip_attachment, 64);
        u32(b, (uint32_t)(bones - 1));
        const float id[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
        for (int k = 0; k < 12; k++) f32(b, id[k]);
    }
    if (turn) {
        name(b, "idle", 16);
        f32(b, 30.0f); u32(b, 1); u32(b, 1);
        for (int i = 0; i < bones; i++) {
            float z = (i == bones - 1) ? sinf(0.7853982f) : 0.0f, w = (i == bones - 1) ? cosf(0.7853982f) : 1.0f;
            f32(b, 0); f32(b, 0); f32(b, z); f32(b, w);
            f32(b, 0); f32(b, 0); f32(b, i ? 1.0f : 0.0f);
        }
    }
}

static buf package(const char *manifest, int models, bool weapon)
{
    buf b = { 0 };
    put(&b, "OALA", 4); u32(&b, 1); u32(&b, (uint32_t)strlen(manifest)); u32(&b, (uint32_t)models); u32(&b, 0);
    uint8_t pad[12] = { 0 }; put(&b, pad, 12);
    put(&b, manifest, strlen(manifest));
    static const char *const body[2] = { "root", "ValveBiped.weapon_bone" };
    static const char *const gun[2] = { "ValveBiped", "ValveBiped.weapon_bone" };
    model(&b, 2, weapon ? gun : body, !weapon, weapon);
    if (weapon) model(&b, 2, gun, true, true);
    return b;
}

int main(void)
{
    printf("oal asset tests\n");
    char err[256];
    static hta_oal_asset ch, wp, bad;
    buf c = package("{\"kind\":\"character\",\"name\":\"guy\"}", 1, false);
    CHECK(hta_oal_load_memory(c.d, c.n, &ch, err, sizeof(err)), "a character package loads");
    const hta_oal_model *m = &ch.models[0];
    CHECK(m->bone_count == 2 && !strcmp(m->bone_name[1], "ValveBiped.weapon_bone"), "bones and names read");
    CHECK(m->mesh.texture_count == 2 && m->mesh.submeshes[0].change_color &&
          m->mesh.textures[1].rgba[2] == HTA_OAL_TEAM_TINT, "a character gets its team-colour mask");
    int32_t idle = hta_oal_clip_find(m, "idle");
    CHECK(idle == 0 && hta_oal_clip_find(m, "death") < 0, "clips found by role");

    static float world[HTA_OAL_MAX_BONES][12];
    hta_vertex out[3];
    const float root[12] = { 1,0,0,10, 0,1,0,0, 0,0,1,0 };
    hta_oal_pose(m, -1, 0, world);
    hta_oal_skin(m, (const float (*)[12])world, root, out);
    CHECK(fabsf(out[0].pos[0] - 11.0f) < 1e-4f && fabsf(out[0].pos[2] - 1.0f) < 1e-4f,
          "bind pose skins to the bind vertices, placed by the root");
    hta_oal_pose(m, idle, 0, world);
    hta_oal_skin(m, (const float (*)[12])world, root, out);
    CHECK(fabsf(out[0].pos[0] - 10.0f) < 1e-4f && fabsf(out[0].pos[1] - 1.0f) < 1e-4f,
          "the idle clip turns the hand's vertices 90 degrees about Z");

    buf w = package("{\"base\":\"assault rifle\",\"crosshair\":\"ring+dot\",\"crosshair_size\":24,\"kind\":\"weapon\",\"name\":\"gun\","
                    "\"stats\":{\"damage_scale\":1.5,\"magazine\":30,\"reload_rounds\":1,\"reload_seconds\":0.8}}", 2, true);
    CHECK(hta_oal_load_memory(w.d, w.n, &wp, err, sizeof(err)), "a weapon package loads");
    CHECK(!strcmp(wp.base, "assault rifle") && wp.magazine == 30 && fabsf(wp.damage_scale - 1.5f) < 1e-6f,
          "weapon numbers read from the manifest");
    CHECK(wp.reload_rounds == 1 && fabsf(wp.reload_seconds - 0.8f) < 1e-6f && !strcmp(wp.crosshair, "ring+dot") &&
          wp.crosshair_size == 24.0f, "a round-at-a-time reload and its own crosshair read too");
    float hold[12];
    CHECK(hta_imported_hold_matrix(m, (const float (*)[12])world, root, &wp.models[0], hold),
          "a weapon sharing the hand's bone name is bone-merged");
    /* The weapon's weapon_bone lands exactly on the body's. */
    float wb[HTA_OAL_MAX_BONES][12], at[12];
    hta_oal_pose(&wp.models[0], -1, 0, wb);
    hta_oal_mul(hold, wb[1], at);
    float body_bone[12];
    hta_oal_mul(root, world[1], body_bone);
    float diff = 0;
    for (int k = 0; k < 12; k++) diff += fabsf(at[k] - body_bone[k]);
    CHECK(diff < 1e-4f, "bone-merge makes the two weapon bones coincide");
    float halo[12], src[12], both[12];
    CHECK(hta_imported_halo_in_source_hand(m, (const float (*)[12])world, root, halo), "a Halo weapon has a hand");
    hta_imported_source_in_halo_hand(&wp.models[0], src);
    hta_oal_mul(halo, src, both);
    diff = 0;
    for (int k = 0; k < 12; k++) if (k % 4 != 3) diff += fabsf(both[k] - hold[k]);
    CHECK(diff < 1e-4f, "Halo and Source weapon conventions cross back to the same aim");

    {
        static hta_oal_asset mir;
        buf mb = package("{\"base\":\"assault rifle\",\"kind\":\"weapon\",\"name\":\"gun\",\"view_model_mirrored\":true}", 2, true);
        CHECK(hta_oal_load_memory(mb.d, mb.n, &mir, err, sizeof(err)) && mir.view_mirrored, "a mirrored view model loads");
        const hta_bsp_mesh *a0 = &wp.models[1].mesh, *a1 = &mir.models[1].mesh;
        CHECK(a1->vertices[1].pos[1] == -a0->vertices[1].pos[1] && a1->indices[1] == a0->indices[2] &&
              a1->indices[2] == a0->indices[1], "left-handed view models are mirrored, winding and all");
        static float wm[HTA_OAL_MAX_BONES][12];
        hta_vertex vo[3];
        const float id[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
        hta_oal_pose(&mir.models[1], 0, 0, wm);
        hta_oal_skin(&mir.models[1], (const float (*)[12])wm, id, vo);
        hta_vertex vn[3];
        hta_oal_pose(&wp.models[1], 0, 0, wm);
        hta_oal_skin(&wp.models[1], (const float (*)[12])wm, id, vn);
        CHECK(fabsf(vo[0].pos[0] - vn[0].pos[0]) < 1e-4f && fabsf(vo[0].pos[1] + vn[0].pos[1]) < 1e-4f &&
              fabsf(vo[0].pos[2] - vn[0].pos[2]) < 1e-4f, "an animated mirrored view poses as the mirror image");
        hta_oal_free(&mir); free(mb.d);
    }
    {
        /* A world model with no weapon bone, only a grip attachment named
         * like the body's weapon bone (HL2's .357): merged by that. */
        static hta_oal_asset pk;
        static const char *const lone[2] = { "MAXSceneRoot", "Cylinder01" };
        buf pb = { 0 };
        const char *mf = "{\"base\":\"pistol\",\"kind\":\"weapon\",\"name\":\"pk\"}";
        put(&pb, "OALA", 4); u32(&pb, 1); u32(&pb, (uint32_t)strlen(mf)); u32(&pb, 2); u32(&pb, 0);
        uint8_t pad[12] = { 0 }; put(&pb, pad, 12); put(&pb, mf, strlen(mf));
        grip_attachment = "ValveBiped.weapon_bone";
        model(&pb, 2, lone, false, true);
        grip_attachment = NULL;
        model(&pb, 2, lone, true, true);
        float h2[12];
        CHECK(hta_oal_load_memory(pb.d, pb.n, &pk, err, sizeof(err)) && pk.models[0].att_count == 1 &&
              hta_imported_hold_matrix(m, (const float (*)[12])world, root, &pk.models[0], h2),
              "a grip attachment loads");
        static float pw[HTA_OAL_MAX_BONES][12];
        float g[12];
        hta_oal_pose(&pk.models[0], -1, 0, pw);
        hta_oal_mul(pw[1], pk.models[0].att[0].local, g);
        float at2[12], want[12];
        hta_oal_mul(h2, g, at2);
        hta_oal_mul(root, world[1], want);
        float d2 = 0;
        for (int k = 0; k < 12; k++) d2 += fabsf(at2[k] - want[k]);
        CHECK(d2 < 1e-4f, "and it lands on the body's weapon bone");
        hta_oal_free(&pk); free(pb.d);
    }
    CHECK(!strcmp(hta_imported_role("crouch rifle move-left"), "crouch_move") &&
          !strcmp(hta_imported_role("stand pistol move-back"), "run_back") &&
          !strcmp(hta_imported_role("stand rifle airborne"), "air") &&
          !strcmp(hta_imported_role("stand rifle idle"), "idle"), "Halo stances map to roles");

    buf k = package("{\"kind\":\"vehicle\",\"name\":\"x\"}", 1, false);
    CHECK(!hta_oal_load_memory(k.d, k.n, &bad, err, sizeof(err)), "an unknown kind is refused");
    buf t = package("{\"kind\":\"character\",\"name\":\"guy\"}", 1, false);
    CHECK(!hta_oal_load_memory(t.d, t.n - 5, &bad, err, sizeof(err)), "a truncated package is refused");
    t.d[4] = 2;
    CHECK(!hta_oal_load_memory(t.d, t.n, &bad, err, sizeof(err)), "another version is refused");

    hta_oal_free(&ch); hta_oal_free(&wp);
    free(c.d); free(w.d); free(k.d); free(t.d);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
