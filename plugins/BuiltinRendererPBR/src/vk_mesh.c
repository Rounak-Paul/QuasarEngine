#include "qs_mesh.h"
#include "qs_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QS_MAX_MESHES 512

struct Qs_Mesh {
    char              name[64];
    bool              in_use;

    Qs_GpuContext    *gpu;
    Qs_GpuBuffer     *vertex_buffer;
    uint32_t          vertex_count;
    Qs_GpuBuffer     *index_buffer;
    uint32_t          index_count;
    Qs_IndexType      index_type;
};

typedef struct {
    Qs_GpuContext    *gpu;
    Qs_Mesh           meshes[QS_MAX_MESHES];
    uint32_t          count;
} VkMeshSystemData;

static VkMeshSystemData *g_mesh_system;

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool vk_mesh_init(Qs_GpuContext *gpu, void **out_ctx)
{
    VkMeshSystemData *data = calloc(1, sizeof(VkMeshSystemData));
    if (!data) return false;
    data->gpu = gpu;
    g_mesh_system = data;
    *out_ctx = data;
    QS_LOG_INFO("VkMesh: mesh system initialised");
    return true;
}

static void vk_mesh_shutdown_impl(Qs_Mesh *m)
{
    if (!m || !m->in_use) return;
    if (m->index_buffer)  { qs_gpu_destroy_buffer(m->gpu, m->index_buffer);  m->index_buffer  = NULL; }
    if (m->vertex_buffer) { qs_gpu_destroy_buffer(m->gpu, m->vertex_buffer); m->vertex_buffer = NULL; }
    m->in_use = false;
}

static void vk_mesh_shutdown(void *ctx)
{
    VkMeshSystemData *data = ctx;
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++)
        vk_mesh_shutdown_impl(&data->meshes[i]);
    g_mesh_system = NULL;
    free(data);
    QS_LOG_INFO("VkMesh: mesh system shut down");
}

/* ================================================================
   MESH LIFECYCLE
   ================================================================ */

static Qs_Mesh *vk_mesh_create(void *ctx, Qs_Engine *engine,
                                const Qs_MeshDesc *desc)
{
    (void)engine;
    VkMeshSystemData *sys = ctx;
    if (!sys || !desc || !desc->vertices || desc->vertex_count == 0) return NULL;

    Qs_Mesh *m = NULL;
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++) {
        if (!sys->meshes[i].in_use) { m = &sys->meshes[i]; break; }
    }
    if (!m) {
        QS_LOG_ERROR("VkMesh: mesh limit reached (%d)", QS_MAX_MESHES);
        return NULL;
    }

    memset(m, 0, sizeof(*m));
    m->in_use       = true;
    m->gpu          = sys->gpu;
    m->vertex_count = desc->vertex_count;
    m->index_count  = desc->index_count;
    m->index_type   = desc->index_type;

    if (desc->name) snprintf(m->name, sizeof(m->name), "%s", desc->name);
    else            snprintf(m->name, sizeof(m->name), "mesh_%u", sys->count);

    uint64_t vb_size = (uint64_t)(desc->vertex_count * sizeof(Qs_Vertex));
    m->vertex_buffer = qs_gpu_create_buffer_from_data(sys->gpu,
        QS_GPU_BUFFER_VERTEX, desc->vertices, vb_size);
    if (!m->vertex_buffer) {
        QS_LOG_ERROR("VkMesh: failed to create vertex buffer for '%s'", m->name);
        m->in_use = false; return NULL;
    }

    if (desc->indices && desc->index_count > 0) {
        uint64_t stride  = (desc->index_type == QS_INDEX_TYPE_UINT16) ? 2 : 4;
        uint64_t ib_size = (uint64_t)(desc->index_count * stride);
        m->index_buffer = qs_gpu_create_buffer_from_data(sys->gpu,
            QS_GPU_BUFFER_INDEX, desc->indices, ib_size);
        if (!m->index_buffer) {
            QS_LOG_ERROR("VkMesh: failed to create index buffer for '%s'", m->name);
            vk_mesh_shutdown_impl(m); return NULL;
        }
    }

    sys->count++;
    QS_LOG_INFO("VkMesh: '%s' created (%u verts, %u idx)", m->name, m->vertex_count, m->index_count);
    return m;
}

static void vk_mesh_destroy(void *ctx, Qs_Mesh *mesh)
{
    VkMeshSystemData *sys = ctx;
    if (!mesh || !mesh->in_use) return;
    QS_LOG_INFO("VkMesh: '%s' destroyed", mesh->name);
    vk_mesh_shutdown_impl(mesh);
    if (sys && sys->count > 0) sys->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

static const char *vk_mesh_name(const Qs_Mesh *m)   { return m ? m->name         : NULL; }
static uint32_t    vk_mesh_vcount(const Qs_Mesh *m)  { return m ? m->vertex_count : 0; }
static uint32_t    vk_mesh_icount(const Qs_Mesh *m)  { return m ? m->index_count  : 0; }

static void vk_mesh_bind(const Qs_Mesh *m, Qs_GpuCmd *cmd)
{
    if (!m || !cmd) return;
    qs_cmd_bind_vertex_buffer(cmd, 0, m->vertex_buffer, 0);
    if (m->index_buffer)
        qs_cmd_bind_index_buffer(cmd, m->index_buffer,
                                  m->index_type == QS_INDEX_TYPE_UINT16);
}

static void vk_mesh_draw(const Qs_Mesh *m, Qs_GpuCmd *cmd)
{
    if (!m || !cmd) return;
    vk_mesh_bind(m, cmd);
    if (m->index_count > 0)
        qs_cmd_draw_indexed(cmd, m->index_count, 0, 0);
    else
        qs_cmd_draw(cmd, m->vertex_count, 0);
}

/* ================================================================
   BACKEND STRUCT
   ================================================================ */

const Qs_MeshBackend vk_mesh_backend = {
    .name         = "Vulkan/PBR",
    .init         = vk_mesh_init,
    .shutdown     = vk_mesh_shutdown,
    .create       = vk_mesh_create,
    .destroy      = vk_mesh_destroy,
    .mesh_name    = vk_mesh_name,
    .vertex_count = vk_mesh_vcount,
    .index_count  = vk_mesh_icount,
    .bind         = vk_mesh_bind,
    .draw         = vk_mesh_draw,
};

#include <stdio.h>
