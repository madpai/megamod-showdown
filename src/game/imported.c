#include "imported.h"
#include <math.h>
#include <string.h>

/* The body's hand, in order of preference: Source's weapon bone, its
 * right-hand attachment, its hand bone. Skeleton-family names, not models. */
static const char *const HAND_BONES[] = { "ValveBiped.weapon_bone", "ValveBiped.Bip01_R_Hand" };
static const char *const HAND_ATTACHMENTS[] = { "anim_attachment_RH" };

bool hta_imported_hand_frame(const hta_oal_model *body, const float (*world)[12],
                             const float root[12], float out[12])
{
    if (!body) return false;
    for (unsigned i = 0; i < sizeof(HAND_BONES) / sizeof(HAND_BONES[0]); i++) {
        int32_t b = hta_oal_bone_find(body, HAND_BONES[i]);
        if (b >= 0) { hta_oal_mul(root, world[b], out); return true; }
    }
    for (unsigned i = 0; i < sizeof(HAND_ATTACHMENTS) / sizeof(HAND_ATTACHMENTS[0]); i++) {
        int32_t a = hta_oal_attachment_find(body, HAND_ATTACHMENTS[i]);
        if (a >= 0) {
            float t[12];
            hta_oal_mul(world[body->att[a].bone], body->att[a].local, t);
            hta_oal_mul(root, t, out);
            return true;
        }
    }
    return false;
}

bool hta_imported_hold_matrix(const hta_oal_model *body, const float (*world)[12],
                              const float root[12], const hta_oal_model *weapon, float out[12])
{
    if (!body || !weapon) return false;
    static float wbind[HTA_OAL_MAX_BONES][12];
    hta_oal_pose(weapon, -1, 0.0f, wbind);
    /* Bone-merge: the first weapon bone the body also has (as a bone, or
     * as an attachment Asset Lab gave a body of the same skeleton family),
     * root excepted. */
    for (uint32_t wb = 1; wb < weapon->bone_count; wb++) {
        float at[12];
        int32_t bb = hta_oal_bone_find(body, weapon->bone_name[wb]);
        int32_t ba = bb < 0 ? hta_oal_attachment_find(body, weapon->bone_name[wb]) : -1;
        if (bb >= 0) memcpy(at, world[bb], sizeof(at));
        else if (ba >= 0) hta_oal_mul(world[body->att[ba].bone], body->att[ba].local, at);
        else continue;
        float inv[12], t[12];
        hta_oal_invert(wbind[wb], inv);
        hta_oal_mul(at, inv, t);
        hta_oal_mul(root, t, out);
        return true;
    }
    /* Or a weapon attachment by that name: a grip Asset Lab gave a world
     * model that has no weapon bone of its own (HL2's .357). */
    for (uint32_t wa = 0; wa < weapon->att_count; wa++) {
        const hta_oal_attachment *w = &weapon->att[wa];
        if (w->bone < 0 || (uint32_t)w->bone >= weapon->bone_count) continue;
        float at[12];
        int32_t bb = hta_oal_bone_find(body, w->name);
        int32_t ba = bb < 0 ? hta_oal_attachment_find(body, w->name) : -1;
        if (bb >= 0) memcpy(at, world[bb], sizeof(at));
        else if (ba >= 0) hta_oal_mul(world[body->att[ba].bone], body->att[ba].local, at);
        else continue;
        float grip[12], inv[12], t[12];
        hta_oal_mul(wbind[w->bone], w->local, grip);
        hta_oal_invert(grip, inv);
        hta_oal_mul(at, inv, t);
        hta_oal_mul(root, t, out);
        return true;
    }
    /* No shared bone: the weapon model's origin at the body's hand, as
     * Source does when it cannot bone-merge. */
    return hta_imported_hand_frame(body, world, root, out);
}

const char *hta_imported_role(const char *b)
{
    if (!b) return "idle";
    bool crouch = strstr(b, "crouch") != NULL;
    if (strstr(b, "airborne") || strstr(b, "land")) return "air";
    bool moving = strstr(b, "move-") != NULL;
    if (crouch) return moving ? "crouch_move" : "crouch_idle";
    if (strstr(b, "move-front")) return "run_front";
    if (strstr(b, "move-back")) return "run_back";
    if (strstr(b, "move-left")) return "run_left";
    if (strstr(b, "move-right")) return "run_right";
    return "idle";
}

/* Source weapon model space -> Halo weapon space: barrel -Y -> +X. */
static const float HALO_FROM_SOURCE[12] = { 0, -1, 0, 0,   1, 0, 0, 0,   0, 0, 1, 0 };
/* A Source weapon bone's frame in its model, grip at the origin. */
static const float SOURCE_WEAPON_BONE[12] = { 1, 0, 0, 0,   0, 0, -1, 0,   0, 1, 0, 0 };

void hta_imported_source_in_halo_hand(const hta_oal_model *weapon, float out[12])
{
    float grip[12] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0 };
    int32_t wb = weapon ? hta_oal_bone_find(weapon, "ValveBiped.weapon_bone") : -1;
    if (wb >= 0) {
        static float w[HTA_OAL_MAX_BONES][12];
        hta_oal_pose(weapon, -1, 0.0f, w);
        grip[3] = -w[wb][3]; grip[7] = -w[wb][7]; grip[11] = -w[wb][11];
    }
    hta_oal_mul(HALO_FROM_SOURCE, grip, out);
}

bool hta_imported_halo_in_source_hand(const hta_oal_model *body, const float (*world)[12],
                                      const float root[12], float out[12])
{
    float at[12];
    int32_t bb = hta_oal_bone_find(body, "ValveBiped.weapon_bone");
    int32_t ba = bb < 0 ? hta_oal_attachment_find(body, "ValveBiped.weapon_bone") : -1;
    if (bb >= 0) hta_oal_mul(root, world[bb], at);
    else if (ba >= 0) {
        float t[12];
        hta_oal_mul(world[body->att[ba].bone], body->att[ba].local, t);
        hta_oal_mul(root, t, at);
    } else if (!hta_imported_hand_frame(body, world, root, at)) return false;
    float inv_bone[12], inv_hfs[12], t[12];
    hta_oal_invert(SOURCE_WEAPON_BONE, inv_bone);
    hta_oal_invert(HALO_FROM_SOURCE, inv_hfs);
    hta_oal_mul(at, inv_bone, t);
    hta_oal_mul(t, inv_hfs, out);
    return true;
}

/* Framing, ours: the body fills about 70% of the view's height and stands
 * at 72% of its width, the camera a touch above its middle. */
#define PREVIEW_FOV    0.70f
#define PREVIEW_FILL   0.80f
#define PREVIEW_X      0.44f    /* screen x in NDC, -1 left .. 1 right */

void hta_preview_frame(float height, float aspect, float yaw, float root[12], hta_camera *cam)
{
    if (height < 0.05f) height = 0.6f;
    if (aspect < 0.2f) aspect = 16.0f / 9.0f;
    float cy = cosf(yaw), sy = sinf(yaw);
    const float r[12] = { cy, -sy, 0, 0,   sy, cy, 0, 0,   0, 0, 1, 0 };
    memcpy(root, r, sizeof(r));
    hta_camera_init(cam);
    cam->fov_y = PREVIEW_FOV;
    cam->aspect = aspect;
    cam->znear = 0.02f; cam->zfar = 50.0f;
    float t = tanf(PREVIEW_FOV * 0.5f);
    float d = height * 0.5f / PREVIEW_FILL / t;
    /* Looking down -X, +Y is screen right: step left so the body sits right. */
    cam->pos[0] = d;
    cam->pos[1] = -PREVIEW_X * d * t * aspect;
    cam->pos[2] = height * 0.52f;
    cam->yaw = 3.14159265f;
    cam->pitch = 0.0f;
}

void hta_imported_mount_matrix(const float root[12], const hta_oal_asset *a, float out[12])
{
    float y = a->mount_yaw / 57.2957795f, p = a->mount_pitch / 57.2957795f;
    float cy = cosf(y), sy = sinf(y), cp = cosf(p), sp = sinf(p);
    /* Yaw about +Z after pitch about +Y, then the offset. */
    const float local[12] = { cy*cp, -sy, cy*sp, a->mount_offset[0],
                              sy*cp,  cy, sy*sp, a->mount_offset[1],
                              -sp,   0.0f, cp,   a->mount_offset[2] };
    hta_oal_mul(root, local, out);
}
