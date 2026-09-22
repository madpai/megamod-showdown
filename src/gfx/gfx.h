/* Renderer interface. Vulkan lives entirely behind this; the engine and the
 * asset layer never include a Vulkan header.
 *
 * Two backends share one implementation:
 *   - swapchain mode  (Android: presents to an ANativeWindow)
 *   - offscreen mode  (host: renders to an image you can read back as RGBA)
 */
#ifndef HTA_GFX_H
#define HTA_GFX_H

#include <stdbool.h>
#include <stdint.h>
#include "../engine/engine.h"
#include "../engine/camera.h"
#include "../asset/bsp.h"

typedef struct hta_gfx      hta_gfx;
typedef struct hta_gfx_mesh hta_gfx_mesh;

typedef struct {
    float light_dir[3];
    float light_color[3];
    float ambient[3];
    float clear[3];
} hta_scene;

/* native_window is an ANativeWindow* */
hta_gfx *hta_gfx_create_window(void *native_window, char *err, size_t errlen);
/* headless; render target is width x height */
hta_gfx *hta_gfx_create_offscreen(uint32_t w, uint32_t h, char *err, size_t errlen);
void     hta_gfx_destroy(hta_gfx *g);

const char *hta_gfx_device_name(const hta_gfx *g);
void        hta_gfx_extent(const hta_gfx *g, uint32_t *w, uint32_t *h);

/* Uploads BSP geometry plus any decoded textures. NULL on failure. */
hta_gfx_mesh *hta_gfx_mesh_upload(hta_gfx *g, const hta_bsp_mesh *mesh,
                                  char *err, size_t errlen);
/* Same, but the vertex buffer can be rewritten every frame -- for the skinned
 * viewmodel or HUD, with no mipmaps. Topology, submeshes and textures are
 * still uploaded once. */
hta_gfx_mesh *hta_gfx_mesh_upload_dynamic(hta_gfx *g, const hta_bsp_mesh *mesh,
                                          char *err, size_t errlen);
/* Animated world geometry, with the same mipmap chain as static geometry.
 * Textures are uploaded once, not regenerated when vertices change. */
hta_gfx_mesh *hta_gfx_mesh_upload_dynamic_world(hta_gfx *g, const hta_bsp_mesh *mesh,
                                                char *err, size_t errlen);
void hta_gfx_mesh_free(hta_gfx *g, hta_gfx_mesh *m);

/* The first-person view. `vertices` (optional) replaces the mesh's vertex data
 * for this frame; the copy happens behind the frame fence. `offset` shifts the
 * whole thing in Halo FP axes (+X forward, +Y left, +Z up) and is the weapon
 * tag's own value -- the Trial leaves it (0,0,0) and animates the hold. */
typedef struct {
    hta_gfx_mesh     *mesh;
    const hta_vertex *vertices;
    uint32_t          vertex_count;
    float             offset[3];
} hta_gfx_viewmodel;

/* A screen-space overlay drawn last, with no depth and real alpha. Vertex
 * positions are already in clip space (x and y in -1..1), so the HUD needs no
 * matrix; each submesh carries the colour its tag asked for. */
typedef struct {
    hta_gfx_mesh     *mesh;
    const hta_vertex *vertices;
    uint32_t          vertex_count;
    const hta_submesh *submeshes;   /* tints, parallel to the mesh's own */
    uint32_t          submesh_count;
} hta_gfx_overlay;

/* Renders one frame. In swapchain mode this also presents. Returns false if the
 * surface needs rebuilding (caller should recreate). */
/* World-space geometry whose vertices change every frame: projectiles in
 * flight. Same arrangement as hta_gfx_viewmodel -- a mesh from
 * hta_gfx_mesh_upload_dynamic plus this frame's vertices -- but drawn with
 * the world camera rather than in view space. */
typedef struct {
    hta_gfx_mesh     *mesh;
    const hta_vertex *vertices;
    uint32_t          vertex_count;
    /* Light it from the scene rather than from a lightmap it does not have.
     *
     * Everything in the world pass is drawn against its lightmap, and a
     * mesh without one falls back to mid-grey -- which comes out as raw
     * albedo, flat and washed out. That is right for a particle, which
     * glows, and wrong for a BODY: the same reason the first-person weapon
     * needed lighting, and the same path. */
    bool              lit;
} hta_gfx_dynamic;

/* `dyn` is an array: projectiles in flight and the particles they throw
 * are separate meshes with separate textures, and both change every frame.
 *
 * Six were all in use -- projectiles, grenades, particles, the body, the
 * items and the corpse -- which left no room for the two things that want
 * one next: weapons dropped on the ground, and powerups spinning where they
 * lie. Both need their own mesh rather than a share of an existing one.
 * Eight is a bigger stack array in the draw path and nothing else. */
#define HTA_GFX_MAX_DYNAMIC 10u

bool hta_gfx_draw(hta_gfx *g, const hta_camera *cam, const hta_scene *scene,
                  hta_gfx_mesh *mesh, hta_gfx_mesh *sky, hta_gfx_mesh *fx,
                  const hta_gfx_dynamic *dyn, uint32_t dyn_count,
                  const hta_gfx_viewmodel *vm, const hta_gfx_overlay *hud);

/* Offscreen only: copies the last rendered frame out as tightly packed RGBA8.
 * `dst` must hold w*h*4 bytes. */
bool hta_gfx_readback(hta_gfx *g, uint8_t *dst, size_t dst_size);

/* Total bytes of device memory this context has allocated (diagnostics). */
uint64_t hta_gfx_device_memory_used(const hta_gfx *g);

#endif
