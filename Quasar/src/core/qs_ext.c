#include "qs_ext.h"
#include "quasar.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
   INTERNAL DATA STRUCTURES
   ================================================================ */

#define MAX_POINT_NAME  64
#define MAX_ENTRIES     32
#define MAX_POINTS      64

typedef struct ExtEntry {
    const void *iface;
    void       *data;
    uint32_t    point_idx;   /* index into registry's points array */
    uint32_t    entry_idx;   /* index within the point's entries   */
} ExtEntry;

typedef struct ExtPoint {
    char      name[MAX_POINT_NAME];
    ExtEntry  entries[MAX_ENTRIES];
    uint32_t  count;
} ExtPoint;

struct Qs_ExtRegistry {
    ExtPoint  points[MAX_POINTS];
    uint32_t  point_count;
};

/* Qs_Extension is just an alias for ExtEntry so the handle can reach
   back into the registry for unregistration. */
struct Qs_Extension {
    Qs_ExtRegistry *registry;
    uint32_t        point_idx;
    uint32_t        entry_idx;
};

/* Pool of extension handles — avoids per-handle malloc. */
#define MAX_HANDLES (MAX_POINTS * MAX_ENTRIES)
static struct Qs_Extension s_handles[MAX_HANDLES];
static uint32_t            s_handle_count;

/* ================================================================
   HELPERS
   ================================================================ */

static ExtPoint *find_point(const Qs_ExtRegistry *reg, const char *name)
{
    for (uint32_t i = 0; i < reg->point_count; i++) {
        if (strcmp(reg->points[i].name, name) == 0)
            return (ExtPoint *)&reg->points[i];
    }
    return NULL;
}

static ExtPoint *find_or_create_point(Qs_ExtRegistry *reg, const char *name)
{
    ExtPoint *p = find_point(reg, name);
    if (p) return p;
    if (reg->point_count >= MAX_POINTS) return NULL;
    p = &reg->points[reg->point_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    return p;
}

/* ================================================================
   REGISTRY LIFECYCLE
   ================================================================ */

Qs_ExtRegistry *qs_ext_registry_create(void)
{
    Qs_ExtRegistry *reg = calloc(1, sizeof(Qs_ExtRegistry));
    return reg;
}

void qs_ext_registry_destroy(Qs_ExtRegistry *reg)
{
    if (!reg) return;
    /* Invalidate any outstanding handles pointing at this registry */
    for (uint32_t i = 0; i < s_handle_count; i++) {
        if (s_handles[i].registry == reg)
            s_handles[i].registry = NULL;
    }
    free(reg);
}

/* ================================================================
   REGISTER / UNREGISTER
   ================================================================ */

Qs_Extension *qs_ext_register(Qs_ExtRegistry *reg, const char *point,
                              const void *iface, void *data)
{
    if (!reg || !point || !iface) return NULL;

    ExtPoint *p = find_or_create_point(reg, point);
    if (!p || p->count >= MAX_ENTRIES) return NULL;

    uint32_t idx = p->count++;
    p->entries[idx].iface     = iface;
    p->entries[idx].data      = data;
    p->entries[idx].point_idx = (uint32_t)(p - reg->points);
    p->entries[idx].entry_idx = idx;

    /* Allocate a handle */
    if (s_handle_count >= MAX_HANDLES) return NULL;
    struct Qs_Extension *h = &s_handles[s_handle_count++];
    h->registry  = reg;
    h->point_idx = p->entries[idx].point_idx;
    h->entry_idx = idx;
    return h;
}

void qs_ext_unregister(Qs_Extension *ext)
{
    if (!ext || !ext->registry) return;

    Qs_ExtRegistry *reg = ext->registry;
    if (ext->point_idx >= reg->point_count) return;

    ExtPoint *p = &reg->points[ext->point_idx];
    if (ext->entry_idx >= p->count) return;

    /* Swap-remove the entry */
    uint32_t last = p->count - 1;
    if (ext->entry_idx != last) {
        p->entries[ext->entry_idx] = p->entries[last];
        /* Update any handle that pointed at the moved entry */
        for (uint32_t i = 0; i < s_handle_count; i++) {
            if (s_handles[i].registry == reg &&
                s_handles[i].point_idx == ext->point_idx &&
                s_handles[i].entry_idx == last)
            {
                s_handles[i].entry_idx = ext->entry_idx;
                break;
            }
        }
    }
    p->count--;
    ext->registry = NULL;
}

/* ================================================================
   QUERIES
   ================================================================ */

uint32_t qs_ext_count(const Qs_ExtRegistry *reg, const char *point)
{
    if (!reg || !point) return 0;
    const ExtPoint *p = find_point(reg, point);
    return p ? p->count : 0;
}

const void *qs_ext_interface(const Qs_ExtRegistry *reg,
                             const char *point, uint32_t index)
{
    if (!reg || !point) return NULL;
    const ExtPoint *p = find_point(reg, point);
    if (!p || index >= p->count) return NULL;
    return p->entries[index].iface;
}

void *qs_ext_data(const Qs_ExtRegistry *reg,
                  const char *point, uint32_t index)
{
    if (!reg || !point) return NULL;
    const ExtPoint *p = find_point(reg, point);
    if (!p || index >= p->count) return NULL;
    return p->entries[index].data;
}

/* ================================================================
   ENGINE CONVENIENCE WRAPPERS
   ================================================================ */

Qs_ExtRegistry *qs_engine_ext_registry(const Qs_Engine *engine);

Qs_Extension *qs_engine_ext_register(Qs_Engine *engine, const char *point,
                                     const void *iface, void *data)
{
    Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_register(reg, point, iface, data) : NULL;
}

uint32_t qs_engine_ext_count(const Qs_Engine *engine, const char *point)
{
    const Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_count(reg, point) : 0;
}

const void *qs_engine_ext_interface(const Qs_Engine *engine,
                                   const char *point, uint32_t index)
{
    const Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_interface(reg, point, index) : NULL;
}

void *qs_engine_ext_data(const Qs_Engine *engine,
                         const char *point, uint32_t index)
{
    const Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_data(reg, point, index) : NULL;
}
