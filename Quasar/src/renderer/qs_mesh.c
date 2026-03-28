#include "qs_mesh.h"
#include "qs_gpu.h"
#include "qs_system.h"
#include "qs_log.h"

#include <stddef.h>

static const Qs_MeshBackend *g_mesh_backend;
static void                 *g_mesh_ctx;

void qs_mesh_backend_register(const Qs_MeshBackend *backend)
{
    g_mesh_backend = backend;
}

typedef struct { void *ctx; } Qs_MeshSystemState;

static bool mesh_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    Qs_MeshSystemState *state = (Qs_MeshSystemState *)qs_system_data(sys);
    if (!g_mesh_backend) {
        QS_LOG_ERROR("Mesh system: no backend registered");
        return false;
    }
    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    if (!g_mesh_backend->init(gpu, &state->ctx)) {
        QS_LOG_ERROR("Mesh backend '%s' init failed", g_mesh_backend->name);
        return false;
    }
    g_mesh_ctx = state->ctx;
    QS_LOG_INFO("Mesh backend '%s' initialised", g_mesh_backend->name);
    return true;
}

static void mesh_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    Qs_MeshSystemState *state = (Qs_MeshSystemState *)qs_system_data(sys);
    if (g_mesh_backend && state->ctx)
        g_mesh_backend->shutdown(state->ctx);
    g_mesh_ctx = NULL;
    QS_LOG_INFO("Mesh backend shut down");
}

Qs_SystemDesc qs_mesh_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Mesh",
        .data_size = sizeof(Qs_MeshSystemState),
        .init      = mesh_sys_init,
        .shutdown  = mesh_sys_shutdown,
        .update    = NULL,
    };
}

Qs_Mesh *qs_mesh_create(Qs_Engine *engine, const Qs_MeshDesc *desc)
{
    if (!g_mesh_backend || !g_mesh_backend->create) return NULL;
    return g_mesh_backend->create(g_mesh_ctx, engine, desc);
}

void qs_mesh_destroy(Qs_Mesh *mesh)
{
    if (!mesh || !g_mesh_backend || !g_mesh_backend->destroy) return;
    g_mesh_backend->destroy(g_mesh_ctx, mesh);
}

const char *qs_mesh_name(const Qs_Mesh *mesh)
{
    if (!mesh || !g_mesh_backend || !g_mesh_backend->mesh_name) return NULL;
    return g_mesh_backend->mesh_name(mesh);
}

uint32_t qs_mesh_vertex_count(const Qs_Mesh *mesh)
{
    if (!mesh || !g_mesh_backend || !g_mesh_backend->vertex_count) return 0;
    return g_mesh_backend->vertex_count(mesh);
}

uint32_t qs_mesh_index_count(const Qs_Mesh *mesh)
{
    if (!mesh || !g_mesh_backend || !g_mesh_backend->index_count) return 0;
    return g_mesh_backend->index_count(mesh);
}

void qs_mesh_bind(const Qs_Mesh *mesh, Qs_GpuCmd *cmd)
{
    if (!mesh || !cmd || !g_mesh_backend || !g_mesh_backend->bind) return;
    g_mesh_backend->bind(mesh, cmd);
}

void qs_mesh_draw(const Qs_Mesh *mesh, Qs_GpuCmd *cmd)
{
    if (!mesh || !cmd || !g_mesh_backend || !g_mesh_backend->draw) return;
    g_mesh_backend->draw(mesh, cmd);
}
