#include "qs_mesh.h"
#include "qs_log.h"
#include "vk_common.h"

#include <stdlib.h>
#include <string.h>

#define QS_MAX_MESHES 512

struct Qs_Mesh {
    char              name[64];
    bool              in_use;

    VkDevice          device;
    VkPhysicalDevice  physical_device;
    Ca_Instance      *ca_instance;

    VkBuffer          vertex_buffer;
    VkDeviceMemory    vertex_memory;
    uint32_t          vertex_count;

    VkBuffer          index_buffer;
    VkDeviceMemory    index_memory;
    uint32_t          index_count;
    Qs_IndexType      index_type;
};

typedef struct {
    Ca_Instance      *ca_instance;
    VkDevice          device;
    VkPhysicalDevice  physical_device;
    Qs_Mesh           meshes[QS_MAX_MESHES];
    uint32_t          count;
} VkMeshSystemData;

static VkMeshSystemData *g_mesh_system;

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool vk_mesh_init(Ca_Instance *ca, void **out_ctx)
{
    VkMeshSystemData *data = calloc(1, sizeof(VkMeshSystemData));
    if (!data) return false;

    data->ca_instance     = ca;
    data->device          = ca_gpu_device(ca);
    data->physical_device = ca_gpu_physical_device(ca);
    if (!data->device || !data->physical_device) { free(data); return false; }

    g_mesh_system = data;
    *out_ctx = data;
    QS_LOG_INFO("VkMesh: mesh system initialised (device %p)", (void *)data->device);
    return true;
}

static void vk_mesh_shutdown_impl(Qs_Mesh *m, VkDevice dev)
{
    if (!m || !m->in_use) return;
    vkDeviceWaitIdle(dev);
    if (m->index_buffer)  vkDestroyBuffer(dev, m->index_buffer, NULL);
    if (m->index_memory)  vkFreeMemory(dev, m->index_memory, NULL);
    if (m->vertex_buffer) vkDestroyBuffer(dev, m->vertex_buffer, NULL);
    if (m->vertex_memory) vkFreeMemory(dev, m->vertex_memory, NULL);
    m->in_use = false;
}

static void vk_mesh_shutdown(void *ctx)
{
    VkMeshSystemData *data = ctx;
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++)
        vk_mesh_shutdown_impl(&data->meshes[i], data->device);
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
        if (!sys->meshes[i].in_use) {
            m = &sys->meshes[i];
            break;
        }
    }
    if (!m) {
        QS_LOG_ERROR("VkMesh: mesh limit reached (%d)", QS_MAX_MESHES);
        return NULL;
    }

    memset(m, 0, sizeof(*m));
    m->in_use          = true;
    m->device          = sys->device;
    m->physical_device = sys->physical_device;
    m->ca_instance     = sys->ca_instance;
    m->vertex_count    = desc->vertex_count;
    m->index_count     = desc->index_count;
    m->index_type      = desc->index_type;

    if (desc->name)
        snprintf(m->name, sizeof(m->name), "%s", desc->name);
    else
        snprintf(m->name, sizeof(m->name), "mesh_%u", sys->count);

    VkDeviceSize vb_size = (VkDeviceSize)(desc->vertex_count * sizeof(Qs_Vertex));
    if (!vk_upload_buffer(m->ca_instance, m->device, m->physical_device,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           desc->vertices, vb_size,
                           &m->vertex_buffer, &m->vertex_memory)) {
        QS_LOG_ERROR("VkMesh: failed to create vertex buffer for '%s'", m->name);
        m->in_use = false;
        return NULL;
    }

    if (desc->indices && desc->index_count > 0) {
        uint32_t stride = (desc->index_type == QS_INDEX_TYPE_UINT16) ? 2 : 4;
        VkDeviceSize ib_size = (VkDeviceSize)(desc->index_count * stride);
        if (!vk_upload_buffer(m->ca_instance, m->device, m->physical_device,
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               desc->indices, ib_size,
                               &m->index_buffer, &m->index_memory)) {
            QS_LOG_ERROR("VkMesh: failed to create index buffer for '%s'", m->name);
            vk_mesh_shutdown_impl(m, m->device);
            return NULL;
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
    vk_mesh_shutdown_impl(mesh, mesh->device);
    if (sys && sys->count > 0) sys->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

static const char  *vk_mesh_name(const Qs_Mesh *m)  { return m ? m->name : NULL; }
static VkBuffer     vk_mesh_vbuf(const Qs_Mesh *m)   { return m ? m->vertex_buffer : VK_NULL_HANDLE; }
static VkBuffer     vk_mesh_ibuf(const Qs_Mesh *m)   { return m ? m->index_buffer  : VK_NULL_HANDLE; }
static uint32_t     vk_mesh_vcount(const Qs_Mesh *m) { return m ? m->vertex_count  : 0; }
static uint32_t     vk_mesh_icount(const Qs_Mesh *m) { return m ? m->index_count   : 0; }

static VkIndexType vk_mesh_index_type(const Qs_Mesh *m)
{
    if (!m) return VK_INDEX_TYPE_UINT32;
    return (m->index_type == QS_INDEX_TYPE_UINT16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
}

static void vk_mesh_bind(const Qs_Mesh *m, VkCommandBuffer cmd)
{
    if (!m || !cmd) return;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m->vertex_buffer, &off);
    if (m->index_buffer)
        vkCmdBindIndexBuffer(cmd, m->index_buffer, 0, vk_mesh_index_type(m));
}

static void vk_mesh_draw(const Qs_Mesh *m, VkCommandBuffer cmd)
{
    if (!m || !cmd) return;
    vk_mesh_bind(m, cmd);
    if (m->index_count > 0)
        vkCmdDrawIndexed(cmd, m->index_count, 1, 0, 0, 0);
    else
        vkCmdDraw(cmd, m->vertex_count, 1, 0, 0);
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
    .vertex_buffer = vk_mesh_vbuf,
    .index_buffer  = vk_mesh_ibuf,
    .vertex_count  = vk_mesh_vcount,
    .index_count   = vk_mesh_icount,
    .vk_index_type = vk_mesh_index_type,
    .bind          = vk_mesh_bind,
    .draw          = vk_mesh_draw,
};
