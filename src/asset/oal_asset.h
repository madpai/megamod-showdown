/* Open Asset Lab .oalasset v1: an imported character or weapon.
 *
 * A character is one skinned model with clips named by ROLE ("idle",
 * "run_front", "crouch_move", "death", ...). A weapon is a world model
 * (static, held and dropped) and a first-person view model with its own
 * clips ("idle", "fire", "reload", "draw"), plus the numbers it plays by,
 * expressed against a BASE Halo weapon whose tag supplies everything the
 * package does not (projectile, impact effects, HUD).
 *
 * Posing is the caller's: hta_oal_pose gives every bone in model space at a
 * time in a clip, hta_oal_skin writes posed vertices through a root
 * transform, the same CPU skinning the Halo bodies use.
 *
 * Portable: bytes in, meshes and matrices out.
 */
#ifndef HTA_OAL_ASSET_H
#define HTA_OAL_ASSET_H

#include "bsp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_OAL_MAX_MODELS 2u
#define HTA_OAL_MAX_SOUNDS 8u
#define HTA_OAL_MAX_BONES  256u
/* An imported body has no multipurpose map to say where its team colour
 * goes, so the loader gives it a uniform one: in a team game the whole body
 * leans this far toward red or blue (of 255). Ours. */
#define HTA_OAL_TEAM_TINT  110u

typedef struct { float q[4]; float p[3]; } hta_oal_key;   /* local: rotation (x,y,z,w), position */

typedef struct {
    char        role[16];
    float       fps;
    uint32_t    frames;
    bool        loop;
    hta_oal_key *keys;          /* frames x bone_count */
} hta_oal_clip;

typedef struct {
    char    name[64];
    int32_t bone;
    float   local[12];          /* 3x4 row-major, in the bone's space */
} hta_oal_attachment;

typedef struct {
    hta_bsp_mesh mesh;          /* bind pose, runtime units; owns its textures */
    uint8_t    (*vbone)[4];     /* per vertex: three bones (fourth unused) */
    float      (*vweight)[3];
    uint32_t     bone_count;
    char       (*bone_name)[64];
    int32_t     *parent;
    float      (*inv_bind)[12]; /* model space -> bone space, 3x4 row-major */
    hta_oal_key *bind;          /* local bind pose */
    hta_oal_attachment *att;
    uint32_t     att_count;
    hta_oal_clip *clips;
    uint32_t     clip_count;
} hta_oal_model;

typedef struct {
    char     role[16];
    uint32_t rate, channels, frames;
    int16_t *samples;
} hta_oal_sound;

typedef struct {
    char     kind[16];          /* "character", "weapon" or "sounds" (no models) */
    char     name[48];          /* the package's id, e.g. "ak47" */
    char     display[48];       /* for menus; the name when the manifest gives none */
    /* Weapons: the Halo weapon it is built on (a tag path fragment, e.g.
     * "assault rifle") and the numbers it overrides; 0 keeps the base's. */
    char     base[64];
    float    rounds_per_second, damage_scale, spread_scale;
    int      magazine, reserve;
    /* The view model is authored left-handed and drawn mirrored (CS:S's
     * cl_righthand): the loader mirrors it across its own Y. */
    bool     view_mirrored;
    /* A reload that puts back `reload_rounds` at a time (TF2's rocket
     * launcher and scattergun load one) over `reload_seconds` each; 0
     * keeps the base weapon's. */
    int      reload_rounds;
    float    reload_seconds;
    /* Its own crosshair: "arms", "dot", "ring", joined with '+', and its
     * size on the 640x480 HUD canvas. Empty keeps the base weapon's. */
    char     crosshair[32];
    float    crosshair_size;
    /* Characters: the default class, two weapon names as the game shows
     * them ("AK-47", "pistol"); empty when the package names none. */
    char     loadout[2][48];
    hta_oal_model models[HTA_OAL_MAX_MODELS];   /* weapon: [0] world, [1] view */
    uint32_t model_count;
    hta_oal_sound sounds[HTA_OAL_MAX_SOUNDS];
    uint32_t sound_count;
    bool     loaded;
} hta_oal_asset;

bool hta_oal_load(const char *path, hta_oal_asset *out, char *err, size_t errlen);
bool hta_oal_load_memory(const uint8_t *data, size_t size, hta_oal_asset *out,
                         char *err, size_t errlen);
void hta_oal_free(hta_oal_asset *a);

int32_t hta_oal_clip_find(const hta_oal_model *m, const char *role);
int32_t hta_oal_bone_find(const hta_oal_model *m, const char *name);        /* case-insensitive */
int32_t hta_oal_attachment_find(const hta_oal_model *m, const char *name);
const hta_oal_sound *hta_oal_sound_find(const hta_oal_asset *a, const char *role);

/* Clip length in seconds (0 for none). */
float hta_oal_clip_length(const hta_oal_model *m, int32_t clip);

/* Every bone in model space at `t` seconds into `clip` (-1: the bind pose).
 * Looping clips wrap; others hold their last frame. `world` holds
 * bone_count 3x4 matrices. */
void hta_oal_pose(const hta_oal_model *m, int32_t clip, float t, float (*world)[12]);

/* Posed vertices: skinned by `world`, then placed by `root` (3x4, model
 * space to world). `out` holds mesh.vertex_count vertices; uv carried. */
void hta_oal_skin(const hta_oal_model *m, const float (*world)[12], const float root[12],
                  hta_vertex *out);

/* 3x4 helpers, row-major. */
void hta_oal_mul(const float a[12], const float b[12], float out[12]);
void hta_oal_invert(const float a[12], float out[12]);   /* rigid transforms only */
/* To the renderer's column-major 4x4. */
void hta_oal_to_mat4(const float a[12], float out[16]);

#endif
