#ifndef QS_MESH_H
#define QS_MESH_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_Engine     Qs_Engine;
typedef struct Qs_Mesh       Qs_Mesh;

/* ================================================================
   VERTEX FORMAT — PBR-ready interleaved vertex
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
   MESH API
   ================================================================ */

/// Creates a GPU mesh from vertex and index data. Returns NULL on failure.
Qs_Mesh *qs_mesh_create(Qs_Engine *engine, const Qs_MeshDesc *desc);

/// Destroys a mesh and frees its GPU resources.
void qs_mesh_destroy(Qs_Mesh *mesh);

/// Returns the debug name.
const char *qs_mesh_name(const Qs_Mesh *mesh);

/// Returns the vertex buffer handle.
VkBuffer qs_mesh_vertex_buffer(const Qs_Mesh *mesh);

/// Returns the index buffer handle (VK_NULL_HANDLE if no indices).
VkBuffer qs_mesh_index_buffer(const Qs_Mesh *mesh);

/// Returns the number of vertices.
uint32_t qs_mesh_vertex_count(const Qs_Mesh *mesh);

/// Returns the number of indices (0 if non-indexed).
uint32_t qs_mesh_index_count(const Qs_Mesh *mesh);

/// Returns the Vulkan index type (VK_INDEX_TYPE_UINT16 or VK_INDEX_TYPE_UINT32).
VkIndexType qs_mesh_vk_index_type(const Qs_Mesh *mesh);

/// Binds vertex and index buffers to the command buffer, ready for drawing.
void qs_mesh_bind(const Qs_Mesh *mesh, VkCommandBuffer cmd);

/// Binds and issues the draw call (indexed or non-indexed as appropriate).
void qs_mesh_draw(const Qs_Mesh *mesh, VkCommandBuffer cmd);

#endif
