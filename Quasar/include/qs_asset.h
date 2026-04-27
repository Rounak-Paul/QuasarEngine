#ifndef QS_ASSET_H
#define QS_ASSET_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Qs_Engine    Qs_Engine;
typedef struct Qs_Scene     Qs_Scene;
typedef struct Qs_Mesh      Qs_Mesh;
typedef struct Qs_Material  Qs_Material;
typedef struct Qs_Texture   Qs_Texture;

#ifndef QS_ENTITY_DEFINED
#define QS_ENTITY_DEFINED
typedef uint32_t Qs_Entity;
#define QS_ENTITY_INVALID UINT32_MAX
#endif

/* ================================================================
   ASSET SYSTEM
   ================================================================

   Central resource manager with:
     - Path-based deduplication: same path always yields same handle.
     - Reference counting: assets freed when all refs released.
     - Background loading: CPU-heavy work (parsing, decompression)
       runs on the job system; GPU upload finalised on main thread.
     - Extensible importers: plugins register importers for file types.

   Lifecycle:
     1. qs_asset_load(engine, path)  → Qs_Asset*  (ref count = 1)
     2. qs_asset_acquire(asset)      → +1 ref
     3. qs_asset_state(asset)        → poll readiness
     4. qs_asset_mesh(asset) / ...   → typed access (NULL until READY)
     5. qs_asset_release(asset)      → -1 ref; destroyed when 0

   Thread safety:
     State transitions are atomic.  Typed getters are safe after READY.
     Only call load/acquire/release from the main thread.
   ================================================================ */

/* ----------------------------------------------------------------
   ASSET STATE
   ---------------------------------------------------------------- */

typedef enum Qs_AssetState {
    QS_ASSET_UNLOADED = 0,   ///< Initial state or after explicit unload.
    QS_ASSET_LOADING,        ///< Background import in progress.
    QS_ASSET_UPLOADING,      ///< CPU data ready; GPU upload pending (main thread).
    QS_ASSET_READY,          ///< Fully loaded; typed getters return valid data.
    QS_ASSET_ERROR,          ///< Import or upload failed.
} Qs_AssetState;

/* ----------------------------------------------------------------
   ASSET TYPE
   ---------------------------------------------------------------- */

typedef enum Qs_AssetType {
    QS_ASSET_TYPE_UNKNOWN = 0,
    QS_ASSET_TYPE_TEXTURE,
    QS_ASSET_TYPE_MESH,
    QS_ASSET_TYPE_MATERIAL,
    QS_ASSET_TYPE_MODEL,      ///< Composite: meshes + materials + textures + entity tree.
} Qs_AssetType;

/* ----------------------------------------------------------------
   ASSET HANDLE
   ---------------------------------------------------------------- */

typedef struct Qs_Asset Qs_Asset;

/// Load (or retrieve cached) asset from a project-relative path.
/// Always returns a valid handle.  Check qs_asset_state() for readiness.
/// The returned handle has ref count = 1.
Qs_Asset *qs_asset_load(Qs_Engine *engine, const char *path);

/// Increment reference count.
void qs_asset_acquire(Qs_Asset *asset);

/// Decrement reference count.  Destroyed when count reaches 0.
void qs_asset_release(Qs_Asset *asset);

/// Current state of the asset.
Qs_AssetState qs_asset_state(const Qs_Asset *asset);

/// Asset type (determined after import begins).
Qs_AssetType qs_asset_type(const Qs_Asset *asset);

/// The project-relative path this asset was loaded from.
const char *qs_asset_path(const Qs_Asset *asset);

/* ----------------------------------------------------------------
   TYPED ACCESSORS — return NULL if asset is not READY or wrong type
   ---------------------------------------------------------------- */

/// For QS_ASSET_TYPE_TEXTURE: the GPU texture.
Qs_Texture *qs_asset_texture(const Qs_Asset *asset);

/// For QS_ASSET_TYPE_MESH: the GPU mesh.
Qs_Mesh *qs_asset_mesh(const Qs_Asset *asset);

/// For QS_ASSET_TYPE_MATERIAL: the PBR material.
Qs_Material *qs_asset_material(const Qs_Asset *asset);

/* ----------------------------------------------------------------
   MODEL ASSET — composite resource from glTF/GLB/FBX etc.
   ---------------------------------------------------------------- */

/// A named mesh inside a model asset.
typedef struct Qs_NamedMesh {
    char      name[128];   ///< Unique name within the loaded file.
    Qs_Mesh  *mesh;
} Qs_NamedMesh;

/// A named material inside a model asset.
typedef struct Qs_NamedMaterial {
    char          name[128];   ///< Unique name within the loaded file.
    Qs_Material  *material;
} Qs_NamedMaterial;

/// For QS_ASSET_TYPE_MODEL: number of sub-meshes.
uint32_t qs_asset_model_mesh_count(const Qs_Asset *asset);

/// For QS_ASSET_TYPE_MODEL: i-th named mesh.
const Qs_NamedMesh *qs_asset_model_mesh(const Qs_Asset *asset, uint32_t index);

/// For QS_ASSET_TYPE_MODEL: number of materials.
uint32_t qs_asset_model_material_count(const Qs_Asset *asset);

/// For QS_ASSET_TYPE_MODEL: i-th named material.
const Qs_NamedMaterial *qs_asset_model_material(const Qs_Asset *asset, uint32_t index);

/// For QS_ASSET_TYPE_MODEL: number of textures.
uint32_t qs_asset_model_texture_count(const Qs_Asset *asset);

/// For QS_ASSET_TYPE_MODEL: i-th texture.
Qs_Texture *qs_asset_model_texture(const Qs_Asset *asset, uint32_t index);

typedef struct Qs_Renderer Qs_Renderer;

/* ----------------------------------------------------------------
   ASSET STREAMING
   ----------------------------------------------------------------

   Model assets become renderable immediately after meshes and materials
   are uploaded.  Textures stream in progressively over subsequent frames
   within a configurable per-frame byte budget.  Materials initially use
   engine-default fallback textures (white, normal-up, black) and are
   updated in-place as each texture finishes uploading.
   ---------------------------------------------------------------- */

/// Set the maximum bytes of GPU texture data uploaded per frame.
/// Default: 16 MB.  A budget of 0 disables throttling (upload all at once).
void qs_asset_set_upload_budget(Qs_Engine *engine, uint64_t bytes_per_frame);

/// Returns the current per-frame upload budget in bytes.
uint64_t qs_asset_upload_budget(Qs_Engine *engine);

/// Returns the texture streaming progress for a model asset.
/// 0.0 = no textures uploaded yet, 1.0 = all textures uploaded.
/// Non-model or non-READY assets return 0.0; assets with no textures return 1.0.
float qs_asset_stream_progress(const Qs_Asset *asset);

/* ----------------------------------------------------------------
   ASSET IMPORTER EXTENSION POINT
   ================================================================

   Plugins register importers to handle specific file formats.
   The asset system calls the matching importer in a background job.

   Importers produce an Qs_ImportResult which the asset system
   consumes on the main thread to finalise GPU upload.
   ================================================================ */

#define QS_EXT_ASSET_IMPORTER  "asset.importer"

/// Per-mesh staging data produced by an importer (CPU-side, pre-upload).
typedef struct Qs_ImportMesh {
    char     name[128];
    void    *vertices;       ///< Qs_Vertex array (caller frees after upload).
    uint32_t vertex_count;
    void    *indices;        ///< uint32_t array (caller frees after upload).
    uint32_t index_count;
    uint32_t material_index; ///< Index into materials array, or UINT32_MAX.
} Qs_ImportMesh;

/// Per-material staging data.
typedef struct Qs_ImportMaterial {
    char     name[128];
    float    base_color[4];
    float    metallic;
    float    roughness;
    float    normal_scale;
    float    occlusion_strength;
    float    emissive[3];
    uint32_t alpha_mode;       ///< 0=opaque, 1=mask, 2=blend.
    float    alpha_cutoff;
    bool     double_sided;
    uint32_t base_color_tex;       ///< Index into textures, or UINT32_MAX.
    uint32_t metallic_roughness_tex;
    uint32_t normal_tex;
    uint32_t occlusion_tex;
    uint32_t emissive_tex;
} Qs_ImportMaterial;

/// Per-texture staging data (decoded pixels, ready for GPU upload).
typedef struct Qs_ImportTexture {
    char     name[128];
    uint32_t width;
    uint32_t height;
    uint32_t format;       ///< Qs_TextureFormat.
    void    *pixels;       ///< RGBA8 pixel data (caller frees after upload).
    bool     generate_mips;
    uint32_t min_filter;   ///< Qs_TextureFilter.
    uint32_t mag_filter;
    uint32_t wrap_u;       ///< Qs_TextureWrap.
    uint32_t wrap_v;
    bool     srgb;
} Qs_ImportTexture;

/// Entity node in the imported scene graph.
typedef struct Qs_ImportNode {
    char     name[128];
    float    position[3];
    float    rotation[4];     ///< Quaternion xyzw.
    float    scale[3];
    int32_t  parent_index;    ///< -1 for root nodes.
    uint32_t mesh_index;      ///< Index into meshes, or UINT32_MAX.
} Qs_ImportNode;

/// Complete import result — all data needed to create engine resources.
/// Owned by the importer; freed by the asset system after GPU upload.
typedef struct Qs_ImportResult {
    Qs_ImportTexture  *textures;
    uint32_t           texture_count;
    Qs_ImportMaterial *materials;
    uint32_t           material_count;
    Qs_ImportMesh     *meshes;
    uint32_t           mesh_count;
    Qs_ImportNode     *nodes;
    uint32_t           node_count;
} Qs_ImportResult;

/// Free all staging data inside an import result.
void qs_import_result_free(Qs_ImportResult *result);

/// Interface for "asset.importer" extensions.
typedef struct Qs_AssetImporterExt {
    /// Comma-separated file extensions (e.g. ".gltf,.glb").
    const char *extensions;

    /// Import a file.  Called on a worker thread.
    /// Must not call any GPU or engine API — only parse + decode.
    /// Returns true on success, false on failure.
    bool (*import)(void *data, const char *path, Qs_ImportResult *out_result);
} Qs_AssetImporterExt;

#endif
