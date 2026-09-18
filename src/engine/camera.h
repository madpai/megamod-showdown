/* Portable camera + matrix math. No platform, no GPU, no Vulkan.
 *
 * Coordinate system: Halo is RIGHT-HANDED with +Z UP (x=east, y=north, z=up).
 * We keep the game's convention all the way to the GPU and fix up the
 * projection instead of rotating the world, so BSP coordinates stay readable
 * when debugging.
 */
#ifndef HTA_CAMERA_H
#define HTA_CAMERA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { float m[16]; } hta_mat4;   /* column-major, as Vulkan wants */

typedef struct {
    float pos[3];
    float yaw;      /* radians, rotation about +Z; 0 = looking along +X */
    float pitch;    /* radians, clamped to +-~89 deg */
    float fov_y;    /* radians */
    float aspect;
    float znear, zfar;
} hta_camera;

void hta_camera_init(hta_camera *c);

/* Unit vectors derived from yaw/pitch. */
void hta_camera_forward(const hta_camera *c, float out[3]);
void hta_camera_right(const hta_camera *c, float out[3]);

/* Applies a look delta in radians, clamping pitch. */
void hta_camera_look(hta_camera *c, float dyaw, float dpitch);

/* view * projection, ready to hand to a shader. */
hta_mat4 hta_camera_view_proj(const hta_camera *c);

/* building blocks (exposed for tests) */
hta_mat4 hta_mat4_identity(void);
hta_mat4 hta_mat4_mul(const hta_mat4 *a, const hta_mat4 *b);
hta_mat4 hta_mat4_look_at_zup(const float eye[3], const float target[3]);
/* Reverse-Z-free, Vulkan-style perspective for a +Z-up right-handed world. */
hta_mat4 hta_mat4_perspective_zup(float fov_y, float aspect, float znear, float zfar);
void     hta_mat4_transform(const hta_mat4 *m, const float in[4], float out[4]);

#endif
