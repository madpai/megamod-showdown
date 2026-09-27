/* Resource identity (X4, docs/RESOURCES.md): the one grammar, the one
 * registry of resource types, the table of every field that references a
 * resource, and a bounded set that resolves typed references to indices
 * once, at load.
 *
 *   resource ID   namespace:type/name     x4:script/open_door
 *   package ID    segment(.segment)*      x4.resource_lab
 *
 * A RESOURCE is a named item some package provides (a script, a mover
 * definition, a placed entity, a world). A PACKAGE is the unit that is
 * distributed, declared, required and loaded (package.h). The two
 * grammars cannot be confused: a package ID has no ':' or '/'.
 *
 * Grammar (version HTA_RID_GRAMMAR): every segment is [a-z][a-z0-9_]*,
 * lowercase ASCII only, compared byte for byte, never folded or
 * normalised -- any other spelling (capitals, hyphens, dots, spaces,
 * escapes, empty or doubled separators, "..") is refused, not rewritten.
 * So two IDs that differ at all are different resources on every
 * filesystem and host. Limits: namespace 40, type 24, name 48, whole 96
 * bytes (the same as X1-X3). The type must be one this registry knows.
 *
 * Everything here is load-time only: gameplay holds indices and handles.
 * The registry and the reference table are also what megamod-resources
 * prints (and Open Asset Lab consumes), so the contract cannot drift from
 * the code. Portable C11, no allocation. */
#ifndef HTA_RESOURCE_H
#define HTA_RESOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_RID_GRAMMAR   1u
#define HTA_RID_MAX       96u
#define HTA_RID_NS_MAX    40u
#define HTA_RID_TYPE_MAX  24u
#define HTA_RID_NAME_MAX  48u
#define HTA_PKG_ID_MAX    64u
#define HTA_PKG_SEG_MAX   8u      /* dot-separated segments in a package ID */

/* The registered resource types. The numbers are internal (never in a
 * package or on the wire); packages spell types by name. */
typedef enum {
    HTA_RT_NONE = 0,
    HTA_RT_WORLD, HTA_RT_ENTITY, HTA_RT_MOVER, HTA_RT_SCRIPT,
    HTA_RT_CHARACTER, HTA_RT_WEAPON, HTA_RT_SOUNDS,
    HTA_RT_MODEL, HTA_RT_MATERIAL, HTA_RT_TEXTURE, HTA_RT_SOUND, HTA_RT_ANIMATION,
    HTA_RT_PREFAB, HTA_RT_RULESET,
    HTA_RT_COUNT
} hta_rtype;

/* supported: this engine loads it (as a resource or a package's content);
 * reserved: the name is claimed for a future engine so packages and tools
 * agree on it, but nothing may provide or reference one yet. */
enum { HTA_RT_SUPPORTED = 1, HTA_RT_RESERVED = 2 };

/* package:    identifies what a whole package is (a world, a character);
 * placement:  a placed thing inside one world -- never listed in a
 *             package's provides, never taken from another package;
 * definition: reusable content a package defines and lists. */
enum { HTA_RS_PACKAGE = 1, HTA_RS_PLACEMENT, HTA_RS_DEFINITION };

typedef struct {
    const char *name;        /* canonical, as in IDs */
    const char *noun;        /* in messages: "placed entity" */
    uint8_t     status;      /* HTA_RT_SUPPORTED / HTA_RT_RESERVED */
    uint8_t     scope;       /* HTA_RS_* */
    bool        runtime;     /* references to it are resolved by the engine at load */
    bool        importable;  /* another package may require it (package.h) */
    const char *since;       /* the milestone that registered it */
    const char *doc;
} hta_rtype_info;

const hta_rtype_info *hta_rtype_get(uint8_t type);          /* NULL out of range */
/* A type by name (length-delimited), HTA_RT_NONE when unknown. */
uint8_t hta_rtype_find(const char *name, size_t len);

/* Namespaces no package may provide into (built-in content). */
bool hta_rid_reserved_namespace(const char *ns, size_t len);
extern const char *const HTA_RID_RESERVED_NS[];
extern const size_t HTA_RID_RESERVED_NS_COUNT;

/* A parsed resource ID: spans into the original text. */
typedef struct {
    uint8_t  type;           /* hta_rtype */
    uint8_t  ns_len, type_len, name_len;
    const char *text;        /* the ID as given (NUL-terminated) */
} hta_rid;

enum {
    HTA_RID_OK = 0,
    HTA_RID_MALFORMED,       /* not namespace:type/name in the grammar */
    HTA_RID_UNKNOWN_TYPE,    /* well formed, type not registered */
    HTA_RID_RESERVED_TYPE,   /* well formed, type reserved (not supported) */
};

/* Parses `text`. HTA_RID_OK, or the failure with `why` saying which rule
 * ("namespace 'X4' is not [a-z][a-z0-9_]*"). `out` is filled whenever the
 * shape was readable (also for unknown/reserved types). */
int hta_rid_parse(const char *text, hta_rid *out, char *why, size_t whylen);
/* Well formed, supported, and of `type`. */
bool hta_rid_is(const char *text, uint8_t type);
/* Same namespace (the part before ':'). */
bool hta_rid_same_namespace(const char *a, const char *b);

/* A package ID: dot-separated [a-z][a-z0-9_]* segments, at most
 * HTA_PKG_ID_MAX bytes and HTA_PKG_SEG_MAX segments. */
bool hta_package_id_valid(const char *id, char *why, size_t whylen);

/* ---- fields that reference resources ------------------------------------ */

/* self:   the reference must resolve inside the package that holds it;
 * import: it may also resolve to a resource the package imports from a
 *         package it requires (package.h);
 * named:  it resolves in the one package its requirement names (package.c
 *         checks it when the set loads). */
enum { HTA_REF_SELF = 1, HTA_REF_IMPORT = 2, HTA_REF_NAMED = 3 };

typedef enum {
    HTA_REF_LINK_TARGET = 0,  /* world_entities.entities[].links[].target */
    HTA_REF_MOVER_DEF,        /* world_entities.entities[].definition */
    HTA_REF_SCRIPT,           /* world_entities.entities[].script */
    HTA_REF_ABILITY_SCRIPT,   /* world_entities.ability_script */
    HTA_REF_LUA_ENTITY,       /* world.entity(id) while a script loads */
    HTA_REF_REQUIRE,          /* package.requires[].resources[] */
    HTA_REF_MATERIAL_TEXTURE, /* assets.materials[].texture (X5) */
    HTA_REF_MODEL_MATERIAL,   /* assets.models[].materials[] (X5) */
    HTA_REF_PROP_MODEL,       /* world_entities.entities[].model (X5) */
    HTA_REF_MOVER_SOUND,      /* world_entities.mover_definitions[].sound (X5) */
    HTA_REF_PREFAB_MODEL,     /* prefabs.prefabs[].children[].model (X6) */
    HTA_REF_PREFAB_SOUND,     /* prefabs.prefabs[].children[].sound (X6) */
    HTA_REF_PREFAB_SCRIPT,    /* prefabs.prefabs[].children[].script (X6) */
    HTA_REF_PREFAB_INSTANCE,  /* world_entities.prefab_instances[].prefab (X6) */
    HTA_REF_BIND_SOURCE,      /* world_entities.bindings[].source (X7) */
    HTA_REF_BIND_CONDITION,   /* world_entities.bindings[].conditions[].entity (X7) */
    HTA_REF_BIND_TARGET,      /* world_entities.bindings[].actions[].target (X7) */
    HTA_REF_BIND_AT,          /* world_entities.bindings[].actions[].at (X7) */
    HTA_REF_BIND_SOUND,       /* world_entities.bindings[].actions[].sound (X7) */
    HTA_REF_PREFAB_BIND_SOUND,/* prefabs.prefabs[].bindings[].actions[].sound (X7) */
    HTA_REF_FIELD_COUNT
} hta_ref_field;

typedef struct {
    const char *field;       /* where it is, in the package */
    const char *label;       /* the word messages use: "link target" */
    uint8_t     expects;     /* hta_rtype; HTA_RT_NONE: any importable type */
    uint8_t     from;        /* HTA_REF_SELF / HTA_REF_IMPORT / HTA_REF_NAMED */
    const char *since;
    const char *doc;
} hta_ref_info;

const hta_ref_info *hta_ref_get(uint8_t field);

/* ---- a set of provided resources, and typed resolution ------------------- */

#define HTA_RES_MAX 512u              /* resources in one loaded package set */
#define HTA_RES_MAX_PROVIDERS 16u     /* packages in it (package.h) */

typedef struct {
    char     id[HTA_RID_MAX + 1];
    uint8_t  type;
    uint8_t  provider;       /* the package: 0 is the one being loaded */
    uint16_t index;          /* in the provider's own table (entity, definition, script...) */
} hta_res_entry;

/* What a package may take from others: (provider, resource ID) pairs it
 * declared in its requires. */
typedef struct {
    const char *id;
    uint8_t     provider;
} hta_res_import;

typedef struct {
    hta_res_entry e[HTA_RES_MAX];
    uint32_t      count;
    /* The providers' package IDs, for messages ("" for a package that
     * declares none: a pre-X4 world). */
    char          provider[HTA_RES_MAX_PROVIDERS][HTA_PKG_ID_MAX + 1];
    uint32_t      provider_count;
    const hta_res_import *imports;   /* of provider 0; may be NULL */
    uint32_t      import_count;
    uint32_t      required_mask;     /* bit p: provider 0 requires provider p */
} hta_res_set;

void hta_res_init(hta_res_set *s);
/* A provider's display name for messages: "package x4.shared", or "this
 * package" / "the world" for an undeclared provider 0. */
const char *hta_res_provider_name(const hta_res_set *s, uint8_t provider, char *buf, size_t n);
/* Adds a provided resource. False on a full set, or a duplicate: the same
 * ID from two providers (or twice from one) is refused, naming both;
 * nothing is picked. */
bool hta_res_add(hta_res_set *s, const char *id, uint8_t type, uint8_t provider, uint16_t index,
                 char *err, size_t n);
/* The entry with exactly this ID, or NULL. */
const hta_res_entry *hta_res_find(const hta_res_set *s, const char *id);

/* Resolves `ref`, found in `field` of `who` (a placed ID, "ability_script"),
 * to the entry it names -- by provider 0 or, for an HTA_REF_IMPORT field,
 * by a package it imports it from. Every refusal names who, the field,
 * the reference and why: malformed, unknown or reserved type, the wrong
 * type (and what it is, if anything provides it), missing (with a same-
 * name resource of another type as a hint), provided by a package that is
 * not required or not imported, or only in another package for a
 * same-package field. */
const hta_res_entry *hta_res_resolve(const hta_res_set *s, uint8_t field, const char *who, const char *ref,
                                     char *err, size_t n);

/* ---- the contract, printed ---------------------------------------------- */

/* megamod-resources --json: the grammar, the types, the reference fields,
 * the package schema and every limit, from the tables above and
 * package.h. Returns the length (snprintf semantics). */
size_t hta_resource_contract_json(char *buf, size_t cap);
/* The type table as the Markdown docs/RESOURCES.md carries (checked by
 * test_resource). */
size_t hta_resource_types_markdown(char *buf, size_t cap);
/* The grammar conformance corpus with this engine's verdicts (megamod-
 * resources --conformance), which Open Asset Lab must reproduce. */
size_t hta_resource_conformance_json(char *buf, size_t cap);

#endif
