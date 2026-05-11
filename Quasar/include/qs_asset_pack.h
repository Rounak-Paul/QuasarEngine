#ifndef QS_ASSET_PACK_H
#define QS_ASSET_PACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "qs_api.h"
#include "qs_asset.h"   /* Qs_ImportResult */
#include "qs_job.h"     /* Qs_JobSystem */
#include "qs_mesh.h"    /* Qs_Vertex */

typedef struct Qs_Engine   Qs_Engine;
typedef struct Qs_Mesh     Qs_Mesh;
typedef struct Qs_Material Qs_Material;
typedef struct Qs_Texture  Qs_Texture;
typedef struct Qs_Project  Qs_Project;

/* ================================================================
   PACKED ASSET FORMATS

   .qstex   — binary texture: header + mip-0 RGBA8 (or RG8 / R8) bytes.
              Mip chain is regenerated on GPU at upload time (mip_count
              field is reserved for future on-disk mip storage).
   .qsmesh  — binary mesh: header + Qs_Vertex[] + uint32_t[] indices.
   .qsmat   — JSON material: PBR factors + relative .qstex paths.
   .qproto  — JSON scene file (uses qs_scene_to_json/from_json).

   All packed files live under the project's "Assets/<name>/" tree:

       Assets/<Name>/<Name>.qproto
       Assets/<Name>/meshes/<surface>.qsmesh
       Assets/<Name>/materials/<material>.qsmat
       Assets/<Name>/textures/<texture>.qstex
   ================================================================ */

#define QS_TEX_MAGIC   0x58545351u   /* "QSTX" little-endian */
#define QS_MESH_MAGIC  0x534D5351u   /* "QSMS" little-endian */

#define QS_TEX_VERSION   1u
#define QS_MESH_VERSION  1u

#define QS_TEX_FLAG_SRGB     (1u << 0)

typedef struct Qs_TexFileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;        /* Qs_TextureFormat */
    uint32_t mip_count;     /* 1 for v1 (runtime regenerates mips) */
    uint32_t flags;         /* QS_TEX_FLAG_* */
    uint8_t  min_filter;
    uint8_t  mag_filter;
    uint8_t  wrap_u;
    uint8_t  wrap_v;
    /* Followed by mip_count records: { uint32 byte_size; bytes[byte_size]; } */
} Qs_TexFileHeader;

typedef struct Qs_MeshFileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t vertex_count;
    uint32_t index_count;
    float    aabb_min[3];
    float    aabb_max[3];
    char     surface_name[64];
    /* Followed by:
         Qs_Vertex vertices[vertex_count];
         uint32_t  indices [index_count];
     */
} Qs_MeshFileHeader;

/* ================================================================
   IMPORT OPTIONS — passed to the cook step
   ================================================================ */

/// Per-texture cook options.
typedef struct Qs_TexImportOpts {
    bool include;          ///< If false, the texture is skipped entirely.
    bool srgb;              ///< Final color space.
    bool generate_mips;     ///< Hint for runtime mip generation (default true).
} Qs_TexImportOpts;

/// Per-material cook options (currently informational / future-proofing).
typedef struct Qs_MatImportOpts {
    bool include;
} Qs_MatImportOpts;

/// Per-mesh (per-surface) cook options.
typedef struct Qs_MeshImportOpts {
    bool include;
    bool optimize;          ///< Run meshoptimizer vertex-cache + remap passes.
} Qs_MeshImportOpts;

/// Whole-import cook configuration.
typedef struct Qs_CookOptions {
    /// Project to register the resulting .qproto into.  May be NULL (won't register).
    Qs_Project           *project;

    /// Output directory (absolute).  The cook writes:
    ///   <out_dir>/<asset_name>.qproto
    ///   <out_dir>/meshes/...      <out_dir>/materials/...      <out_dir>/textures/...
    const char           *out_dir;

    /// Display name of the imported asset (used as .qproto filename + scene name).
    const char           *asset_name;

    /// Per-item options (length = corresponding *_count in import result).
    /// May be NULL — defaults are used.
    const Qs_TexImportOpts  *tex_opts;
    const Qs_MatImportOpts  *mat_opts;
    const Qs_MeshImportOpts *mesh_opts;

    /// Optional progress callback.  Called from the cook thread.
    /// `stage` is a short label, `done`/`total` are step counters.
    void (*progress)(void *user, const char *stage, uint32_t done, uint32_t total);
    void *progress_user;
} Qs_CookOptions;

/* ================================================================
   COOK — write a Qs_ImportResult to packed files + .qproto
   ================================================================ */

/// Synchronously cook an import result to disk.
/// Returns true on success.  On success, `out_qproto_abs` (if non-NULL)
/// receives the absolute path of the written .qproto file.
/// The caller still owns and must free `result` afterwards
/// (typically via qs_import_result_free).
QS_API bool qs_asset_cook(const Qs_ImportResult *result,
                   const Qs_CookOptions  *options,
                   char                  *out_qproto_abs,
                   size_t                 out_qproto_size);

/* ================================================================
   RUNTIME ASSET CACHE
   ================================================================
   Path-keyed, ref-counted cache for mesh / material / texture GPU
   resources.  All loading is async (see ASYNC / STREAMING CACHE).
   Release functions are the only public cache API; loading is done
   exclusively through the _async variants below.
   ================================================================ */

/// Release a ref acquired by qs_asset_cache_mesh_async.
/// The GPU resource is destroyed when the ref count reaches zero.
QS_API void qs_asset_cache_release_mesh(const char *abs_path);

/// Release a ref acquired by qs_asset_cache_material.
/// Also releases the material's internally-acquired texture refs.
/// The GPU resource is destroyed when the ref count reaches zero.
QS_API void qs_asset_cache_release_material(const char *abs_path);

/// Release a ref acquired by qs_asset_cache_texture.
/// The GPU resource is destroyed when the ref count reaches zero.
QS_API void qs_asset_cache_release_texture(const char *abs_path);

/// Swap one texture slot of a cached material.  Releases the ref for the old
/// texture at that slot, acquires one for the new texture, updates the material's
/// internal tracking, and calls qs_material_set_texture.
/// abs_mat_path must be the exact path passed to qs_asset_cache_material.
/// Pass NULL/empty abs_new_tex_path to clear the slot (reverts to default fallback).
/// Slot indices: 0=base_color, 1=metallic_roughness, 2=normal, 3=occlusion, 4=emissive.
QS_API Qs_Texture *qs_asset_cache_material_swap_texture(Qs_Engine  *engine,
                                                  const char *abs_mat_path,
                                                  uint32_t    slot,
                                                  const char *abs_new_tex_path);

/// Drop and destroy every cached entry.  Called at asset-system shutdown.
QS_API void qs_asset_cache_clear(void);

/* ================================================================
   ASYNC / STREAMING CACHE
   ================================================================
   Background-thread disk reads with main-thread GPU uploads.

   qs_asset_cache_mesh_async — on a cache miss, dispatches a worker job
   to read the .qsmesh file.  Returns NULL while loading; the entity is
   silently skipped from rendering until the mesh is ready.  On the frame
   after the background read completes, the pump (called automatically by
   the asset-system update) uploads the geometry to GPU and places the
   mesh in the main cache; the next call returns the live Qs_Mesh*.

   qs_asset_cache_material_async — parses the tiny .qsmat JSON on the
   calling thread (sub-millisecond) and creates the Qs_Material
   immediately with NULL / fallback textures.  Each referenced .qstex is
   dispatched as a separate background job.  The material is returned on
   the SAME frame it is first requested; textures stream in progressively
   via qs_material_set_texture calls made by the pump.

   The pump (qs_asset_cache_pump) is called once per frame by the built-in
   asset-system update callback — no manual wiring is needed.
   ================================================================ */

/// Get-or-request a GPU mesh asynchronously.
/// Returns NULL while the background job is running; call again next frame.
/// Returns NULL permanently if the file cannot be read (load error).
/// On success, increments the cache ref count — release with
/// qs_asset_cache_release_mesh when done.
QS_API Qs_Mesh *qs_asset_cache_mesh_async(Qs_Engine    *engine,
                                   Qs_JobSystem *jobs,
                                   const char   *abs_path);

/// Get-or-request a PBR material asynchronously.
/// The material is created on the calling thread (same frame) with default
/// fallback textures; each .qstex referenced by the .qsmat is loaded in the
/// background and swapped into the material's slots as they arrive.
/// Returns NULL only if the .qsmat file cannot be read or the GPU material
/// creation fails.
/// On success, increments the cache ref count — release with
/// qs_asset_cache_release_material when done.
QS_API Qs_Material *qs_asset_cache_material_async(Qs_Engine    *engine,
                                           Qs_JobSystem *jobs,
                                           const char   *abs_path);

/// Promote any CPU-complete async load requests to GPU and insert them into
/// the main cache.  Called automatically by the asset system's update hook —
/// no manual invocation needed.
QS_API void qs_asset_cache_pump(Qs_Engine *engine);

/* ================================================================
   ENGINE BINDING — set by the asset system at init time so internal
   utilities (notably the cook step's progress hooks and any scene-API
   bridges) can reach engine-level services without threading
   `Qs_Engine *` through every call site.
   ================================================================ */

QS_API void       qs_pack_set_active_engine(Qs_Engine *engine);
QS_API Qs_Engine *qs_pack_active_engine(void);

/* ================================================================
   RAW ASSET FILE READERS
   Low-level helpers that read packed binary asset files directly from
   disk without going through the runtime cache.  Useful for editor
   tools (e.g. thumbnail generation) that need raw pixel / geometry
   data without creating GPU resources.

   Callers are responsible for freeing the returned heap allocations:
     - qs_asset_pack_read_texture: free(*out_pixels)
     - qs_asset_pack_read_mesh:    free(*out_verts), free(*out_indices)
   ================================================================ */

/// Reads a .qstex file and returns its header plus heap-allocated pixel data.
/// Returns false on failure (bad magic, I/O error, out of memory).
QS_API bool qs_asset_pack_read_texture(const char       *path,
                                 Qs_TexFileHeader  *out_header,
                                 void             **out_pixels,
                                 uint32_t          *out_size);

/// Reads a .qsmesh file and returns its header plus heap-allocated vertex and
/// index arrays.  Returns false on failure.
QS_API bool qs_asset_pack_read_mesh(const char        *path,
                              Qs_MeshFileHeader  *out_header,
                              Qs_Vertex         **out_verts,
                              uint32_t          **out_indices);

#endif
