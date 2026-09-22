/* The main menu, out of the Trial's own ui.map.
 *
 * Halo's shell is a 3D scene -- the ring, `scenery\halo\halo`, under the
 * `sky_ui` sky -- seen from named cutscene camera points (`uicam`,
 * `multiplayer`, `settings`, ...), with the HALO logo and the menu words
 * drawn over it as bitmaps: `ui\shell\main_menu\halo_logo` and one bitmap
 * per entry, each with a plain frame and a glowing selected frame. All of
 * it is the owner's own data; the layout follows the PC Trial's screen.
 *
 * Portable: meshes, an overlay of clip-space quads and a camera out, and
 * touches in. The platform uploads, draws and plays the sounds.
 */
#ifndef HTA_MENU_H
#define HTA_MENU_H

#include <stdbool.h>
#include <stdint.h>
#include "../asset/cache.h"
#include "../asset/bsp.h"
#include "../asset/bitmap.h"
#include "../engine/camera.h"
#include "../gfx/gfx.h"

typedef enum {
    HTA_MENU_CAMPAIGN = 0,
    HTA_MENU_MULTIPLAYER,
    HTA_MENU_PROFILES,
    HTA_MENU_SETTINGS,
    HTA_MENU_CREDITS,
    HTA_MENU_QUIT,
    HTA_MENU_ITEMS
} hta_menu_item;

#define HTA_MENU_MAX_CAMS 16

typedef struct {
    char  name[32];
    float pos[3];
    float yaw, pitch, roll;
    float fov;
} hta_menu_cam;

typedef struct {
    hta_bsp_mesh scene;     /* the ring, world space */
    hta_bsp_mesh sky;       /* drawn about the camera */
    hta_bsp_mesh overlay;   /* logo + menu words, clip space */
    hta_scene    light;

    hta_menu_cam cams[HTA_MENU_MAX_CAMS];
    uint32_t     cam_count;
    int32_t      home_cam;  /* `uicam`, or 0 */

    /* Overlay quads: the logo, then per item a plain and a selected quad,
     * then a full-screen still for when the 3D scene is missing. */
    uint32_t     logo_quad;
    int32_t      item_quad[HTA_MENU_ITEMS][2];
    bool         item_enabled[HTA_MENU_ITEMS];
    float        item_rect[HTA_MENU_ITEMS][4];  /* x0 y0 x1 y1, pixels */
    int32_t      backdrop_quad;
    bool         have_scene;

    int          selected;      /* which glows */
    float        time;
    uint32_t     width, height;

    /* The shell's own sounds and the title music, 0 where absent. */
    uint32_t     snd_cursor, snd_forward, snd_back, music;
    bool         loaded;
} hta_menu;

bool hta_menu_load(hta_menu *m, const hta_cache *ui, const hta_resource_map *bitmaps,
                   char *err, size_t errlen);
void hta_menu_free(hta_menu *m);

/* Position the overlay for a screen of this size. Cheap; call every frame. */
void hta_menu_layout(hta_menu *m, uint32_t w, uint32_t h);

/* The slow drift of the camera. */
void hta_menu_update(hta_menu *m, float dt);
void hta_menu_camera(const hta_menu *m, hta_camera *out, float aspect);

/* Which enabled item is under a touch at pixel (x, y), or -1. */
int hta_menu_hit(const hta_menu *m, float x, float y);

#endif
