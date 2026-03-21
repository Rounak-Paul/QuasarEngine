#include "qs_mesh.h"
#include "qs_log.h"
#include "qs_system.h"
#include "causality.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
   LIMITS
   ================================================================ */

#define QS_MAX_MESHES 512

/* ================================================================
   INTERNAL TYPES
   ================================================================ */

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

typedef struct Qs_MeshSystemData {
    Ca_Instance      *ca_instance;
    VkDevice          device;
    VkPhysicalDevice  physical_device;
    Qs_Mesh           meshes[QS_MAX_MESHES];
    uint32_t          count;
} Qs_MeshSystemData;

static Qs_MeshSystemData *g_mesh_system;

/* ================================================================
   VULKAN HELPERS
   ================================================================ */

static uint32_t find_memory_type(VkPhysicalDevice pd, uint32_t type_bits,
                                  VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return UINT32_MAX;
}

/// Creates a device-local buffer and uploads data via a staging buffer.
static bool upload_buffer(Ca_Instance *ca, VkDevice device, VkPhysicalDevice pd,
                          VkBufferUsageFlags usage, const void *data,
                          VkDeviceSize size, VkBuffer *out_buf, VkDeviceMemory *out_mem)
{
    /* --- Staging (host-visible) --- */
    VkBuffer staging;
    VkDeviceMemory staging_mem;

    VkBufferCreateInfo staging_ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &staging_ci, NULL, &staging) != VK_SUCCESS)
        return false;

    VkMemoryRequirements staging_req;
    vkGetBufferMemoryRequirements(device, staging, &staging_req);
    uint32_t staging_mi = find_memory_type(pd, staging_req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (staging_mi == UINT32_MAX) {
        vkDestroyBuffer(device, staging, NULL);
        return false;
    }

    VkMemoryAllocateInfo staging_ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = staging_req.size,
        .memoryTypeIndex = staging_mi,
    };
    if (vkAllocateMemory(device, &staging_ai, NULL, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(device, staging, NULL);
        return false;
    }
    vkBindBufferMemory(device, staging, staging_mem, 0);

    void *mapped;
    vkMapMemory(device, staging_mem, 0, size, 0, &mapped);
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(device, staging_mem);

    /* --- Device-local buffer --- */
    VkBufferCreateInfo buf_ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &buf_ci, NULL, out_buf) != VK_SUCCESS) {
        vkDestroyBuffer(device, staging, NULL);
        vkFreeMemory(device, staging_mem, NULL);
        return false;
    }

    VkMemoryRequirements buf_req;
    vkGetBufferMemoryRequirements(device, *out_buf, &buf_req);
    uint32_t buf_mi = find_memory_type(pd, buf_req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (buf_mi == UINT32_MAX) {
        vkDestroyBuffer(device, *out_buf, NULL);
        vkDestroyBuffer(device, staging, NULL);
        vkFreeMemory(device, staging_mem, NULL);
        return false;
    }

    VkMemoryAllocateInfo buf_ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = buf_req.size,
        .memoryTypeIndex = buf_mi,
    };
    if (vkAllocateMemory(device, &buf_ai, NULL, out_mem) != VK_SUCCESS) {
        vkDestroyBuffer(device, *out_buf, NULL);
        vkDestroyBuffer(device, staging, NULL);
        vkFreeMemory(device, staging_mem, NULL);
        return false;
    }
    vkBindBufferMemory(device, *out_buf, *out_mem, 0);

    /* --- Copy via one-shot command --- */
    VkCommandBuffer cmd = ca_gpu_begin_transfer(ca);

    VkBufferCopy copy = { .size = size };
    vkCmdCopyBuffer(cmd, staging, *out_buf, 1, &copy);

    ca_gpu_end_transfer(ca, cmd);

    vkDestroyBuffer(device, staging, NULL);
    vkFreeMemory(device, staging_mem, NULL);
    return true;
}

/* ================================================================
   MESH LIFECYCLE
   ================================================================ */

Qs_Mesh *qs_mesh_create(Qs_Engine *engine, const Qs_MeshDesc *desc)
{
    (void)engine;
    if (!g_mesh_system || !desc || !desc->vertices || desc->vertex_count == 0)
        return NULL;

    Qs_Mesh *m = NULL;
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++) {
        if (!g_mesh_system->meshes[i].in_use) {
            m = &g_mesh_system->meshes[i];
            break;
        }
    }
    if (!m) {
        QS_LOG_ERROR("Mesh limit reached (%d)", QS_MAX_MESHES);
        return NULL;
    }

    memset(m, 0, sizeof(*m));
    m->in_use          = true;
    m->device          = g_mesh_system->device;
    m->physical_device = g_mesh_system->physical_device;
    m->ca_instance     = g_mesh_system->ca_instance;
    m->vertex_count    = desc->vertex_count;
    m->index_count     = desc->index_count;
    m->index_type      = desc->index_type;

    if (desc->name)
        snprintf(m->name, sizeof(m->name), "%s", desc->name);
    else
        snprintf(m->name, sizeof(m->name), "mesh_%u", g_mesh_system->count);

    /* --- Vertex buffer --- */
    VkDeviceSize vb_size = (VkDeviceSize)(desc->vertex_count * sizeof(Qs_Vertex));
    if (!upload_buffer(m->ca_instance, m->device, m->physical_device,
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       desc->vertices, vb_size,
                       &m->vertex_buffer, &m->vertex_memory)) {
        QS_LOG_ERROR("Failed to create vertex buffer for mesh '%s'", m->name);
        m->in_use = false;
        return NULL;
    }

    /* --- Index buffer (optional) --- */
    if (desc->indices && desc->index_count > 0) {
        uint32_t stride = (desc->index_type == QS_INDEX_TYPE_UINT16) ? 2 : 4;
        VkDeviceSize ib_size = (VkDeviceSize)(desc->index_count * stride);
        if (!upload_buffer(m->ca_instance, m->device, m->physical_device,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           desc->indices, ib_size,
                           &m->index_buffer, &m->index_memory)) {
            QS_LOG_ERROR("Failed to create index buffer for mesh '%s'", m->name);
            qs_mesh_destroy(m);
            return NULL;
        }
    }

    g_mesh_system->count++;
    QS_LOG_INFO("Mesh '%s' created (%u verts, %u indices)",
                m->name, m->vertex_count, m->index_count);
    return m;
}

void qs_mesh_destroy(Qs_Mesh *mesh)
{
    if (!mesh || !mesh->in_use) return;

    vkDeviceWaitIdle(mesh->device);

    if (mesh->index_buffer)  vkDestroyBuffer(mesh->device, mesh->index_buffer, NULL);
    if (mesh->index_memory)  vkFreeMemory(mesh->device, mesh->index_memory, NULL);
    if (mesh->vertex_buffer) vkDestroyBuffer(mesh->device, mesh->vertex_buffer, NULL);
    if (mesh->vertex_memory) vkFreeMemory(mesh->device, mesh->vertex_memory, NULL);

    QS_LOG_INFO("Mesh '%s' destroyed", mesh->name);
    mesh->in_use = false;

    if (g_mesh_system && g_mesh_system->count > 0)
        g_mesh_system->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

const char *qs_mesh_name(const Qs_Mesh *mesh)
{
    return mesh ? mesh->name : NULL;
}

VkBuffer qs_mesh_vertex_buffer(const Qs_Mesh *mesh)
{
    return mesh ? mesh->vertex_buffer : VK_NULL_HANDLE;
}

VkBuffer qs_mesh_index_buffer(const Qs_Mesh *mesh)
{
    return mesh ? mesh->index_buffer : VK_NULL_HANDLE;
}

uint32_t qs_mesh_vertex_count(const Qs_Mesh *mesh)
{
    return mesh ? mesh->vertex_count : 0;
}

uint32_t qs_mesh_index_count(const Qs_Mesh *mesh)
{
    return mesh ? mesh->index_count : 0;
}

VkIndexType qs_mesh_vk_index_type(const Qs_Mesh *mesh)
{
    if (!mesh) return VK_INDEX_TYPE_UINT32;
    return (mesh->index_type == QS_INDEX_TYPE_UINT16)
        ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
}

/* ================================================================
   DRAW HELPERS
   ================================================================ */

void qs_mesh_bind(const Qs_Mesh *mesh, VkCommandBuffer cmd)
{
    if (!mesh || !cmd) return;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertex_buffer, &offset);
    if (mesh->index_buffer)
        vkCmdBindIndexBuffer(cmd, mesh->index_buffer, 0, qs_mesh_vk_index_type(mesh));
}

void qs_mesh_draw(const Qs_Mesh *mesh, VkCommandBuffer cmd)
{
    if (!mesh || !cmd) return;
    qs_mesh_bind(mesh, cmd);
    if (mesh->index_count > 0)
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
    else
        vkCmdDraw(cmd, mesh->vertex_count, 1, 0, 0);
}

/* ================================================================
   MESH SYSTEM — engine system callbacks
   ================================================================ */

static bool mesh_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_MeshSystemData *data = (Qs_MeshSystemData *)qs_system_data(system);
    if (!data->ca_instance) return false;

    data->device          = ca_gpu_device(data->ca_instance);
    data->physical_device = ca_gpu_physical_device(data->ca_instance);
    if (!data->device || !data->physical_device) return false;

    g_mesh_system = data;

    QS_LOG_INFO("Mesh system initialized (device %p)", (void *)data->device);
    return true;
}

static void mesh_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_MeshSystemData *data = (Qs_MeshSystemData *)qs_system_data(system);

    for (uint32_t i = 0; i < QS_MAX_MESHES; i++) {
        if (data->meshes[i].in_use)
            qs_mesh_destroy(&data->meshes[i]);
    }

    g_mesh_system = NULL;
    QS_LOG_INFO("Mesh system shut down");
}

static Ca_Instance *s_pending_mesh_instance;

static bool mesh_system_init_wrapper(Qs_System *sys, Qs_Engine *eng)
{
    Qs_MeshSystemData *d = (Qs_MeshSystemData *)qs_system_data(sys);
    d->ca_instance = s_pending_mesh_instance;
    return mesh_system_init(sys, eng);
}

Qs_SystemDesc qs_mesh_system_desc(Ca_Instance *ca_instance)
{
    s_pending_mesh_instance = ca_instance;

    return (Qs_SystemDesc){
        .name      = "Mesh",
        .data_size = sizeof(Qs_MeshSystemData),
        .init      = mesh_system_init_wrapper,
        .shutdown  = mesh_system_shutdown,
        .update    = NULL,
    };
}
