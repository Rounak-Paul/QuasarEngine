#ifndef QS_ASSET_PACK_H
#define QS_ASSET_PACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "qs_asset.h"   /* Qs_ImportResult */

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
bool qs_asset_cook(const Qs_ImportResult *result,
                   const Qs_CookOptions  *options,
                   char                  *out_qproto_abs,
                   size_t                 out_qproto_size);

/* ================================================================
   RUNTIME ASSET CACHE
   ================================================================
   Path-keyed, ref-shared caches for packed mesh / material / texture
   files.  Loaded synchronously on demand from .qsmesh / .qsmat /
   .qstex files.  Used by MeshComp resolution at scene load.
   The cache lives inside the asset system; entries are freed at
   engine shutdown.
   ================================================================ */

/// Get-or-load a GPU mesh from a .qsmesh file.  Returns NULL on failure.
/// Each successful call increments the cache entry's ref count.
Qs_Mesh *qs_asset_cache_mesh(Qs_Engine *engine, const char *abs_path);

/// Get-or-load a PBR material from a .qsmat file.  Texture references
/// inside the material are resolved through qs_asset_cache_texture.
/// Each successful call increments the cache entry's ref count.
Qs_Material *qs_asset_cache_material(Qs_Engine *engine, const char *abs_path);

/// Get-or-load a GPU texture from a .qstex file.
/// Each successful call increments the cache entry's ref count.
Qs_Texture *qs_asset_cache_texture(Qs_Engine *engine, const char *abs_path);

/// Release a ref acquired by qs_asset_cache_mesh.
/// The GPU resource is destroyed when the ref count reaches zero.
void qs_asset_cache_release_mesh(const char *abs_path);

/// Release a ref acquired by qs_asset_cache_material.
/// Also releases the material's internally-acquired texture refs.
/// The GPU resource is destroyed when the ref count reaches zero.
void qs_asset_cache_release_material(const char *abs_path);

/// Release a ref acquired by qs_asset_cache_texture.
/// The GPU resource is destroyed when the ref count reaches zero.
void qs_asset_cache_release_texture(const char *abs_path);

/// Drop and destroy every cached entry.  Called at asset-system shutdown.
void qs_asset_cache_clear(void);

/* ================================================================
   ENGINE BINDING — set by the asset system at init time so internal
   utilities (notably the cook step's progress hooks and any scene-API
   bridges) can reach engine-level services without threading
   `Qs_Engine *` through every call site.
   ================================================================ */

void       qs_pack_set_active_engine(Qs_Engine *engine);
Qs_Engine *qs_pack_active_engine(void);

#endif
