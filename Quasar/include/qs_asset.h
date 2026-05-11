#ifndef QS_ASSET_H
#define QS_ASSET_H

#include <stdint.h>
#include <stdbool.h>
#include "qs_api.h"

/* ================================================================
   ASSET IMPORT TYPES
   ================================================================
   Produced by asset importer plugins (e.g. GltfImporter) and
   consumed by qs_asset_cook() to write packed .qsmesh / .qsmat /
   .qstex files.
   ================================================================ */

/// Per-mesh staging data produced by an importer (CPU-side, pre-upload).
typedef struct Qs_ImportMesh {
    char     name[128];
    void    *vertices;       ///< Qs_Vertex array (freed after cook).
    uint32_t vertex_count;
    void    *indices;        ///< uint32_t array (freed after cook).
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
    void    *pixels;       ///< RGBA8 pixel data (freed after cook).
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
    float    rotation[4];   ///< Quaternion xyzw.
    float    scale[3];
    int32_t  parent_index;  ///< -1 for root nodes.
    uint32_t mesh_index;    ///< Index into meshes, or UINT32_MAX.
} Qs_ImportNode;

/// Complete import result — all data needed to cook packed assets.
/// Owned by the importer; freed by qs_import_result_free() after cooking.
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
QS_API void qs_import_result_free(Qs_ImportResult *result);

/* ================================================================
   ASSET IMPORTER EXTENSION POINT
   ================================================================
   Plugins register importers at the "asset.importer" extension point.
   The import dialog queries registered importers to convert source
   files (e.g. .gltf/.glb) into a Qs_ImportResult for cooking.
   ================================================================ */

#define QS_EXT_ASSET_IMPORTER  "asset.importer"

/// Interface for "asset.importer" extensions.
typedef struct Qs_AssetImporterExt {
    /// Comma-separated file extensions handled (e.g. ".gltf,.glb").
    const char *extensions;

    /// Import a file into staging data.  May be called on any thread.
    /// Must not call GPU or engine APIs — parse and decode only.
    /// Returns true on success.
    bool (*import)(void *data, const char *path, Qs_ImportResult *out_result);
} Qs_AssetImporterExt;

#endif
