#ifndef QS_MEMORY_H
#define QS_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/// Memory usage categories used to classify and track allocations.
typedef enum Qs_MemTag {
    QS_MEM_GENERAL  = 0,   ///< General-purpose / unclassified allocations.
    QS_MEM_ENGINE,          ///< Engine internals (Qs_Engine, system manager, etc.).
    QS_MEM_RENDER,          ///< Renderer structures.
    QS_MEM_GPU,             ///< GPU resource wrappers (buffers, images, pipelines).
    QS_MEM_TEXTURE,         ///< Texture CPU-side pixel data.
    QS_MEM_MESH,            ///< Mesh vertex and index CPU-side data.
    QS_MEM_MATERIAL,        ///< Material data.
    QS_MEM_SCENE,           ///< Scene and ECS allocations.
    QS_MEM_ASSET,           ///< Asset pipeline working data.
    QS_MEM_JOB,             ///< Job system allocations.
    QS_MEM_EVENT,           ///< Event bus allocations.
    QS_MEM_LOG,             ///< Log system allocations.
    QS_MEM_PLUGIN,          ///< Plugin manager allocations.
    QS_MEM_PROJECT,         ///< Project system allocations.
    QS_MEM_UI,              ///< Causality UI library allocations.
    QS_MEM_EDITOR,          ///< Editor tool allocations.
    QS_MEM_TAG_COUNT
} Qs_MemTag;

/// Per-tag memory statistics snapshot.
typedef struct Qs_MemStats {
    size_t bytes_allocated;    ///< Current live bytes for this tag.
    size_t total_allocated;    ///< Cumulative bytes ever allocated (all time).
    size_t total_freed;        ///< Cumulative bytes ever freed (all time).
    size_t allocation_count;   ///< Current live allocation count.
} Qs_MemStats;

/// Initializes the memory tracking system. Idempotent — safe to call multiple times.
/// Must be called before any qs_malloc / qs_free calls.
void qs_mem_init(void);

/// Shuts down the memory system. Logs any remaining live allocations as leaks.
void qs_mem_shutdown(void);

/// Allocates @p size bytes tagged with @p tag. Returns NULL on failure.
void *qs_malloc(size_t size, Qs_MemTag tag);

/// Allocates @p count * @p size bytes (zero-initialized) tagged with @p tag. Returns NULL on failure.
void *qs_calloc(size_t count, size_t size, Qs_MemTag tag);

/// Resizes a previously allocated block to @p new_size bytes.
/// @p ptr may be NULL, in which case this behaves like qs_malloc(new_size, tag).
/// The new block is tagged with @p tag; the old block's tag is updated in the stats accordingly.
void *qs_realloc(void *ptr, size_t new_size, Qs_MemTag tag);

/// Frees a previously allocated block. @p ptr may be NULL (no-op).
void  qs_free(void *ptr);

/// Duplicates a C string, returning a new allocation tagged with @p tag.
/// Returns NULL if @p str is NULL.
char *qs_strdup(const char *str, Qs_MemTag tag);

/// Returns a statistics snapshot for the given memory tag.
Qs_MemStats qs_mem_stats(Qs_MemTag tag);

/// Returns the current live byte count for a given tag.
size_t qs_mem_usage(Qs_MemTag tag);

/// Returns the current total live bytes across all tags.
size_t qs_mem_usage_total(void);

/// Returns a human-readable name string for a memory tag.
const char *qs_mem_tag_name(Qs_MemTag tag);

#endif /* QS_MEMORY_H */
