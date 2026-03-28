/*
 * qs_mesh_system.c — Engine mesh system.
 *
 * Manages GPU mesh resources (vertex/index buffers) as a first-class
 * engine system.  No backend indirection — Causality always uses Vulkan.
 */

#include "qs_mesh.h"
#include "qs_system.h"
#include "qs_gpu.h"
#include "qs_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QS_MAX_MESHES 512

struct Qs_Mesh {
    char           name[64];
    bool           in_use;
    Qs_GpuContext *gpu;
    Qs_GpuBuffer  *vertex_buffer;
    uint32_t       vertex_count;
    Qs_GpuBuffer  *index_buffer;
    uint32_t       index_count;
    Qs_IndexType   index_type;
};

typedef struct {
    Qs_GpuContext *gpu;
    Qs_Mesh        meshes[QS_MAX_MESHES];
    uint32_t       count;
} MeshSystemData;

static MeshSystemData *g_mesh_sys;

/* ================================================================
   SYSTEM LIFECYCLE
   ================================================================ */

static void mesh_destroy_one(Qs_Mesh *m)
{
    if (!m || !m->in_use) return;
    if (m->index_buffer)  { qs_gpu_destroy_buffer(m->gpu, m->index_buffer);  m->index_buffer  = NULL; }
    if (m->vertex_buffer) { qs_gpu_destroy_buffer(m->gpu, m->vertex_buffer); m->vertex_buffer = NULL; }
    m->in_use = false;
}

static bool mesh_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    MeshSystemData *data = (MeshSystemData *)qs_system_data(sys);
    data->gpu  = qs_engine_gpu(engine);
    g_mesh_sys = data;
    QS_LOG_INFO("Mesh system initialised");
    return true;
}

static void mesh_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    MeshSystemData *data = (MeshSystemData *)qs_system_data(sys);
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++)
        mesh_destroy_one(&data->meshes[i]);
    g_mesh_sys = NULL;
    QS_LOG_INFO("Mesh system shut down");
}

Qs_SystemDesc qs_mesh_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Mesh",
        .data_size = sizeof(MeshSystemData),
        .init      = mesh_sys_init,
        .shutdown  = mesh_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Mesh *qs_mesh_create(Qs_Engine *engine, const Qs_MeshDesc *desc)
{
    (void)engine;
    if (!g_mesh_sys || !desc || !desc->vertices || desc->vertex_count == 0) return NULL;

    Qs_Mesh *m = NULL;
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++) {
        if (!g_mesh_sys->meshes[i].in_use) { m = &g_mesh_sys->meshes[i]; break; }
    }
    if (!m) {
        QS_LOG_ERROR("Mesh system: mesh limit reached (%d)", QS_MAX_MESHES);
        return NULL;
    }

    memset(m, 0, sizeof(*m));
    m->in_use       = true;
    m->gpu          = g_mesh_sys->gpu;
    m->vertex_count = desc->vertex_count;
    m->index_count  = desc->index_count;
    m->index_type   = desc->index_type;

    if (desc->name) snprintf(m->name, sizeof(m->name), "%s", desc->name);
    else            snprintf(m->name, sizeof(m->name), "mesh_%u", g_mesh_sys->count);

    const uint64_t vb_size = (uint64_t)desc->vertex_count * sizeof(Qs_Vertex);
    m->vertex_buffer = qs_gpu_create_buffer_from_data(g_mesh_sys->gpu,
        QS_GPU_BUFFER_VERTEX, desc->vertices, vb_size);
    if (!m->vertex_buffer) {
        QS_LOG_ERROR("Mesh system: failed to create vertex buffer for '%s'", m->name);
        m->in_use = false;
        return NULL;
    }

    if (desc->indices && desc->index_count > 0) {
        const uint64_t stride  = (desc->index_type == QS_INDEX_TYPE_UINT16) ? 2u : 4u;
        const uint64_t ib_size = (uint64_t)desc->index_count * stride;
        m->index_buffer = qs_gpu_create_buffer_from_data(g_mesh_sys->gpu,
            QS_GPU_BUFFER_INDEX, desc->indices, ib_size);
        if (!m->index_buffer) {
            QS_LOG_ERROR("Mesh system: failed to create index buffer for '%s'", m->name);
            mesh_destroy_one(m);
            return NULL;
        }
    }

    g_mesh_sys->count++;
    QS_LOG_INFO("Mesh system: '%s' created (%u verts, %u idx)",
                m->name, m->vertex_count, m->index_count);
    return m;
}

void qs_mesh_destroy(Qs_Mesh *mesh)
{
    if (!mesh || !mesh->in_use) return;
    QS_LOG_INFO("Mesh system: '%s' destroyed", mesh->name);
    mesh_destroy_one(mesh);
    if (g_mesh_sys && g_mesh_sys->count > 0)
        g_mesh_sys->count--;
}

const char   *qs_mesh_name        (const Qs_Mesh *m) { return m ? m->name         : NULL; }
uint32_t      qs_mesh_vertex_count(const Qs_Mesh *m) { return m ? m->vertex_count : 0; }
uint32_t      qs_mesh_index_count (const Qs_Mesh *m) { return m ? m->index_count  : 0; }
Qs_GpuBuffer *qs_mesh_vertex_buffer(const Qs_Mesh *m) { return m ? m->vertex_buffer : NULL; }
Qs_GpuBuffer *qs_mesh_index_buffer (const Qs_Mesh *m) { return m ? m->index_buffer  : NULL; }
Qs_IndexType  qs_mesh_index_type   (const Qs_Mesh *m) { return m ? m->index_type : QS_INDEX_TYPE_UINT32; }

void qs_mesh_bind(const Qs_Mesh *mesh, Qs_GpuCmd *cmd)
{
    if (!mesh || !cmd) return;
    qs_cmd_bind_vertex_buffer(cmd, 0, mesh->vertex_buffer, 0);
    if (mesh->index_buffer)
        qs_cmd_bind_index_buffer(cmd, mesh->index_buffer,
                                  mesh->index_type == QS_INDEX_TYPE_UINT16);
}

void qs_mesh_draw(const Qs_Mesh *mesh, Qs_GpuCmd *cmd)
{
    if (!mesh || !cmd) return;
    qs_mesh_bind(mesh, cmd);
    if (mesh->index_count > 0)
        qs_cmd_draw_indexed(cmd, mesh->index_count, 0, 0);
    else
        qs_cmd_draw(cmd, mesh->vertex_count, 0);
}
