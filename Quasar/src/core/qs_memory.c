#include "qs_memory.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>

/* ================================================================
   ALLOCATION HEADER
   Each user allocation is preceded by a 16-byte header that records
   the size and tag, enabling qs_free() to update stats without the
   caller having to pass the tag again.
   ================================================================ */

#define QS_MEM_MAGIC 0xA110CA7u

typedef struct {
    size_t   size;    /* user bytes (not counting this header) */
    uint32_t tag;     /* Qs_MemTag */
    uint32_t magic;   /* QS_MEM_MAGIC — detects corruption / double-free */
} QsMemHeader;

_Static_assert(sizeof(QsMemHeader) == 16,
               "QsMemHeader must be 16 bytes to preserve natural alignment");

/* ================================================================
   PER-TAG STATISTICS
   All counters are atomic so that allocations from worker threads
   (job system, asset loader) are safely tracked without a global lock.
   ================================================================ */

typedef struct {
    _Atomic size_t bytes_live;
    _Atomic size_t total_alloc;
    _Atomic size_t total_freed;
    _Atomic size_t count_live;
} QsTagStats;

static QsTagStats    g_stats[QS_MEM_TAG_COUNT];
static _Atomic int   g_initialized;

static const char *const k_tag_names[QS_MEM_TAG_COUNT] = {
    "General",
    "Engine",
    "Render",
    "GPU",
    "Texture",
    "Mesh",
    "Material",
    "Scene",
    "Asset",
    "Job",
    "Event",
    "Log",
    "Plugin",
    "Project",
    "UI",
    "Editor",
};

/* ================================================================
   LIFECYCLE
   ================================================================ */

/* cJSON allocator wrappers — routes all cJSON allocations through QS_MEM_ASSET. */
static void *cjson_malloc(size_t sz)        { return qs_malloc(sz, QS_MEM_ASSET); }
static void  cjson_free  (void *ptr)        { qs_free(ptr); }

void qs_mem_init(void)
{
    if (atomic_exchange(&g_initialized, 1) != 0)
        return;
    memset(g_stats, 0, sizeof(g_stats));

    /* Route cJSON through the memory system so strings returned by
       cJSON_Print() carry a QsMemHeader and are safe to pass to qs_free(). */
    cJSON_InitHooks(&(cJSON_Hooks){ cjson_malloc, cjson_free });
}

void qs_mem_shutdown(void)
{
    if (!atomic_load(&g_initialized))
        return;

    bool any_leaks = false;
    for (int t = 0; t < QS_MEM_TAG_COUNT; t++) {
        size_t live  = atomic_load(&g_stats[t].bytes_live);
        size_t count = atomic_load(&g_stats[t].count_live);
        if (live > 0) {
            if (!any_leaks) {
                fprintf(stderr, "[qs_memory] WARNING: memory leaks detected at shutdown:\n");
                any_leaks = true;
            }
            fprintf(stderr, "  [%-10s] %zu bytes in %zu allocation(s)\n",
                    k_tag_names[t], live, count);
        }
    }
    atomic_store(&g_initialized, 0);
}

/* ================================================================
   CORE ALLOCATORS
   ================================================================ */

void *qs_malloc(size_t size, Qs_MemTag tag)
{
    if (size == 0) return NULL;
    assert((unsigned)tag < QS_MEM_TAG_COUNT);

    QsMemHeader *hdr = malloc(sizeof(QsMemHeader) + size);
    if (!hdr) return NULL;

    hdr->size  = size;
    hdr->tag   = (uint32_t)tag;
    hdr->magic = QS_MEM_MAGIC;

    atomic_fetch_add(&g_stats[tag].bytes_live,  size);
    atomic_fetch_add(&g_stats[tag].total_alloc, size);
    atomic_fetch_add(&g_stats[tag].count_live,  1);

    return hdr + 1;
}

void *qs_calloc(size_t count, size_t size, Qs_MemTag tag)
{
    size_t total = count * size;
    if (total == 0) return NULL;
    assert((unsigned)tag < QS_MEM_TAG_COUNT);

    QsMemHeader *hdr = calloc(1, sizeof(QsMemHeader) + total);
    if (!hdr) return NULL;

    hdr->size  = total;
    hdr->tag   = (uint32_t)tag;
    hdr->magic = QS_MEM_MAGIC;

    atomic_fetch_add(&g_stats[tag].bytes_live,  total);
    atomic_fetch_add(&g_stats[tag].total_alloc, total);
    atomic_fetch_add(&g_stats[tag].count_live,  1);

    return hdr + 1;
}

void *qs_realloc(void *ptr, size_t new_size, Qs_MemTag tag)
{
    if (!ptr)        return qs_malloc(new_size, tag);
    if (new_size == 0) { qs_free(ptr); return NULL; }
    assert((unsigned)tag < QS_MEM_TAG_COUNT);

    QsMemHeader *old_hdr = (QsMemHeader *)ptr - 1;
    assert(old_hdr->magic == QS_MEM_MAGIC);

    size_t    old_size = old_hdr->size;
    Qs_MemTag old_tag  = (Qs_MemTag)old_hdr->tag;

    QsMemHeader *new_hdr = realloc(old_hdr, sizeof(QsMemHeader) + new_size);
    if (!new_hdr) return NULL;

    /* Retire old stats */
    atomic_fetch_sub(&g_stats[old_tag].bytes_live,  old_size);
    atomic_fetch_add(&g_stats[old_tag].total_freed, old_size);
    atomic_fetch_sub(&g_stats[old_tag].count_live,  1);

    /* Register new stats (tag may differ from old) */
    new_hdr->size  = new_size;
    new_hdr->tag   = (uint32_t)tag;
    new_hdr->magic = QS_MEM_MAGIC;

    atomic_fetch_add(&g_stats[tag].bytes_live,  new_size);
    atomic_fetch_add(&g_stats[tag].total_alloc, new_size);
    atomic_fetch_add(&g_stats[tag].count_live,  1);

    return new_hdr + 1;
}

void qs_free(void *ptr)
{
    if (!ptr) return;

    QsMemHeader *hdr = (QsMemHeader *)ptr - 1;
    assert(hdr->magic == QS_MEM_MAGIC);

    Qs_MemTag tag  = (Qs_MemTag)hdr->tag;
    size_t    size = hdr->size;

    hdr->magic = 0; /* poison to catch use-after-free / double-free */

    atomic_fetch_sub(&g_stats[tag].bytes_live,  size);
    atomic_fetch_add(&g_stats[tag].total_freed, size);
    atomic_fetch_sub(&g_stats[tag].count_live,  1);

    free(hdr);
}

char *qs_strdup(const char *str, Qs_MemTag tag)
{
    if (!str) return NULL;
    size_t len  = strlen(str) + 1;
    char  *copy = qs_malloc(len, tag);
    if (copy) memcpy(copy, str, len);
    return copy;
}

/* ================================================================
   QUERY
   ================================================================ */

Qs_MemStats qs_mem_stats(Qs_MemTag tag)
{
    assert((unsigned)tag < QS_MEM_TAG_COUNT);
    Qs_MemStats s;
    s.bytes_allocated  = atomic_load(&g_stats[tag].bytes_live);
    s.total_allocated  = atomic_load(&g_stats[tag].total_alloc);
    s.total_freed      = atomic_load(&g_stats[tag].total_freed);
    s.allocation_count = atomic_load(&g_stats[tag].count_live);
    return s;
}

size_t qs_mem_usage(Qs_MemTag tag)
{
    assert((unsigned)tag < QS_MEM_TAG_COUNT);
    return atomic_load(&g_stats[tag].bytes_live);
}

size_t qs_mem_usage_total(void)
{
    size_t total = 0;
    for (int t = 0; t < QS_MEM_TAG_COUNT; t++)
        total += atomic_load(&g_stats[t].bytes_live);
    return total;
}

const char *qs_mem_tag_name(Qs_MemTag tag)
{
    if ((unsigned)tag < QS_MEM_TAG_COUNT)
        return k_tag_names[tag];
    return "Unknown";
}
