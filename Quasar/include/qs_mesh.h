#ifndef QS_MESH_H
#define QS_MESH_H

#include "qs_gpu.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_Engine     Qs_Engine;
typedef struct Qs_Mesh       Qs_Mesh;   ///< Opaque — defined by the mesh backend.

/* ================================================================
   VERTEX FORMAT â€” PBR-ready interleaved vertex
   ================================================================ */

/// Standard PBR vertex layout: position, normal, tangent, UV.
typedef struct Qs_Vertex {
    float position[3];    ///< World-space position.
    float normal[3];      ///< Unit normal vector.
    float tangent[4];     ///< Tangent (xyz) + bitangent sign (w).
    float uv[2];          ///< Texture coordinates.
} Qs_Vertex;

/// Index type for mesh indices.
typedef enum Qs_IndexType {
    QS_INDEX_TYPE_UINT16 = 0,
    QS_INDEX_TYPE_UINT32 = 1,
} Qs_IndexType;

/* ================================================================
   MESH DESCRIPTOR
   ================================================================ */

/// Configuration for creating a GPU mesh.
typedef struct Qs_MeshDesc {
    const char       *name;           ///< Debug label.
    const Qs_Vertex  *vertices;       ///< Vertex array.
    uint32_t          vertex_count;
    const void       *indices;        ///< Index array (uint16_t or uint32_t).
    uint32_t          index_count;
    Qs_IndexType      index_type;     ///< Default: UINT32.
} Qs_MeshDesc;

/* ================================================================
   MESH BACKEND
   ================================================================ */

typedef struct Qs_MeshBackend {
    const char *name;

    bool        (*init)(Qs_GpuContext *gpu, void **out_ctx);
    void        (*shutdown)(void *ctx);

    Qs_Mesh    *(*create)(void *ctx, Qs_Engine *engine, const Qs_MeshDesc *desc);
    void        (*destroy)(void *ctx, Qs_Mesh *mesh);

    /* Accessors */
    const char  *(*mesh_name)(const Qs_Mesh *mesh);
    uint32_t     (*vertex_count)(const Qs_Mesh *mesh);
    uint32_t     (*index_count)(const Qs_Mesh *mesh);
    void         (*bind)(const Qs_Mesh *mesh, Qs_GpuCmd *cmd);
    void         (*draw)(const Qs_Mesh *mesh, Qs_GpuCmd *cmd);
} Qs_MeshBackend;

/// Registers the mesh backend.  Must be called before the Mesh
/// system initialises (i.e. in the pluginâ€™s on_load callback).
void qs_mesh_backend_register(const Qs_MeshBackend *backend);

/* ================================================================
   PUBLIC MESH API
   ================================================================ */

/// Creates a GPU mesh. Destroy with qs_mesh_destroy.
Qs_Mesh *qs_mesh_create(Qs_Engine *engine, const Qs_MeshDesc *desc);

/// Destroys a mesh and frees its GPU resources.
void qs_mesh_destroy(Qs_Mesh *mesh);

/// Returns the debug name.
const char *qs_mesh_name(const Qs_Mesh *mesh);

/// Returns the number of vertices.
uint32_t qs_mesh_vertex_count(const Qs_Mesh *mesh);

/// Returns the number of indices (0 if non-indexed).
uint32_t qs_mesh_index_count(const Qs_Mesh *mesh);

/// Binds vertex and index buffers to a command buffer.
void qs_mesh_bind(const Qs_Mesh *mesh, Qs_GpuCmd *cmd);

/// Binds and issues the draw call.
void qs_mesh_draw(const Qs_Mesh *mesh, Qs_GpuCmd *cmd);

#endif
