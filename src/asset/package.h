/* Packages (X4, docs/RESOURCES.md): what a package is, what it provides,
 * what it requires, and the set of packages a world loads with.
 *
 * A package declares itself in its manifest's "package" member:
 *
 *   "package": {"id": "x4.resource_lab", "schema": 1,
 *               "provides": ["x4:mover/basic_slide_door", "x4:script/open_door",
 *                            "x4:world/resource_lab"],
 *               "requires": [{"package": "x4.shared",
 *                             "resources": ["x4shared:script/pulse_ability"]}]}
 *
 * - id: a package ID (resource.h), distinct from every resource ID.
 * - provides: every resource the package defines except placements, in
 *   canonical (byte) order, each once. The loader checks it equals what
 *   the content actually defines -- a declaration cannot lie.
 * - requires: the packages it needs, in canonical order by package ID,
 *   each with the resources it takes from that package (its imports), in
 *   canonical order. A cross-package reference must name an import; an
 *   import must be provided by the package it is taken from.
 *
 * Two kinds: a WORLD (an OALMAP) and a LIBRARY (an OALASSET v1 of kind
 * "library": a manifest of scripts other packages import). Only libraries
 * can be required. A package without a "package" member is IMPLICIT: a
 * pre-X4 world, which provides what it defines and requires nothing, and
 * loads exactly as before.
 *
 * The package graph must be acyclic (a package is validated after what it
 * needs; a cycle has no first package), at most HTA_PKG_MAX_DEPTH deep and
 * HTA_PKG_MAX_SET packages in all. Resource references inside a package
 * may be cyclic unless a relationship forbids it (entity links do: a
 * zero-delay cycle would loop in one tick).
 *
 * Deterministic: a set is loaded by package ID, never by directory
 * listing, and kept in canonical order (sorted by package ID) whatever
 * order the requires were followed in. */
#ifndef HTA_PACKAGE_H
#define HTA_PACKAGE_H

#include "resource.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_PKG_SCHEMA        1u
#define HTA_PKG_MAX_PROVIDES  256u
#define HTA_PKG_MAX_REQUIRES  16u
#define HTA_PKG_MAX_IMPORTS   64u   /* resources taken from others, all requirements together */
#define HTA_PKG_MAX_SET       16u   /* a package and everything it needs, transitively */
#define HTA_PKG_MAX_DEPTH     8u    /* requirement links from the root to the deepest package */
#define HTA_PKG_LIBRARY_DIR   "packages"   /* where a library is looked for: packages/<id>.oalasset */

enum { HTA_PKG_WORLD = 1, HTA_PKG_LIBRARY = 2 };

typedef struct {
    char    id[HTA_RID_MAX + 1];
    uint8_t type;            /* hta_rtype */
} hta_pkg_rid;

typedef struct {
    char     package[HTA_PKG_ID_MAX + 1];
    uint16_t first, count;   /* its imports in hta_package.imports */
} hta_pkg_require;

typedef struct {
    bool     declared;       /* has a "package" member; false: implicit (pre-X4) */
    uint8_t  kind;           /* HTA_PKG_WORLD / HTA_PKG_LIBRARY */
    char     id[HTA_PKG_ID_MAX + 1];
    char     content_id[HTA_RID_MAX + 1];   /* a world's manifest "id" (its world resource), if any */
    hta_pkg_rid     provides[HTA_PKG_MAX_PROVIDES];
    uint32_t        provide_count;
    hta_pkg_require requires[HTA_PKG_MAX_REQUIRES];
    uint32_t        require_count;
    hta_pkg_rid     imports[HTA_PKG_MAX_IMPORTS];
    uint32_t        import_count;
} hta_package;

/* The "package" member of a manifest (canonical JSON), strictly: every
 * field present, nothing unknown, IDs valid, lists canonical. No member:
 * true with declared = false. `kind` says what holds the manifest. */
bool hta_package_parse(const uint8_t *manifest, size_t len, uint8_t kind, hta_package *out,
                       char *err, size_t errlen);
/* "package x4.shared", or a pre-X4 package's description. */
const char *hta_package_name(const hta_package *p, char *buf, size_t n);
/* Is `id` in its provides? */
const hta_pkg_rid *hta_package_provides(const hta_package *p, const char *id);

/* Where the bytes of a required package come from (a bundle directory,
 * the APK): by package ID only. `where` names the place for messages
 * ("packages/x4.shared.oalasset"). The file name is where to look, never
 * identity: the package found must declare the ID asked for. */
typedef struct {
    bool (*open)(void *ctx, const char *package_id, const uint8_t **data, size_t *size, void **handle,
                 char *where, size_t wherelen);
    void (*close)(void *ctx, void *handle);
    void *ctx;
} hta_pkg_source;

struct hta_world_defs;

typedef struct {
    hta_package decl;
    bool        direct;      /* the root requires it */
    uint64_t    digest;      /* its played bytes (hta_library_digest) */
    struct hta_world_defs *scripts;   /* a library's scripts (heap) */
} hta_pkg_dep;

/* A root package and every package it needs, loaded and checked. */
typedef struct hta_pkg_set_s {
    hta_package root;
    hta_pkg_dep dep[HTA_PKG_MAX_SET - 1];   /* canonical order: by package ID */
    uint32_t    dep_count;
} hta_pkg_set;

/* Loads everything `root` requires, transitively, through `src`: each
 * package once, checked (a library with the ID asked for, its provides
 * equal to its scripts, every import provided by the package it is taken
 * from), the graph acyclic and within its limits. False with a message
 * naming the package, the requirement and why -- a missing package, a
 * cycle (with its path), an import nobody provides. `src` may be NULL
 * when the root requires nothing. Free with hta_pkg_set_free. */
bool hta_pkg_set_load(hta_pkg_set *set, const hta_package *root, const hta_pkg_source *src,
                      char *err, size_t errlen);
void hta_pkg_set_free(hta_pkg_set *set);

/* The resources of a loaded set's dependencies (providers 1..dep_count,
 * canonical order) into `rs`, with the root's imports; the caller adds the
 * root's own (provider 0). `imports` holds HTA_PKG_MAX_IMPORTS entries. */
bool hta_pkg_set_resources(const hta_pkg_set *set, hta_res_set *rs, hta_res_import *imports,
                           char *err, size_t errlen);

/* A library (OALASSET v1, kind "library") from its bytes: its declaration
 * and scripts, checked; `digest` is what it adds to a world key. */
bool hta_library_load(const uint8_t *data, size_t size, hta_pkg_dep *out, char *err, size_t errlen);

/* The library manifest members its digest covers, by index (NULL past the end). */
const char *hta_library_key_played(uint32_t i);

/* The manifest of an OALASSET v1 (header checked). */
bool hta_oalasset_manifest(const uint8_t *data, size_t size, const uint8_t **manifest, size_t *len);

#endif
