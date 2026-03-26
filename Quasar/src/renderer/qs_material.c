#include "qs_material.h"
#include "qs_system.h"
#include "qs_log.h"

#include <stddef.h>

static const Qs_MaterialBackend *g_material_backend;
static void                     *g_material_ctx;

void qs_material_backend_register(const Qs_MaterialBackend *backend)
{
    g_material_backend = backend;
}

typedef struct { void *ctx; } Qs_MaterialSystemState;

typedef struct Ca_Instance Ca_Instance;
Ca_Instance *qs_engine_ca_instance(Qs_Engine *engine);

static bool material_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    Qs_MaterialSystemState *state = (Qs_MaterialSystemState *)qs_system_data(sys);
    if (!g_material_backend) {
        QS_LOG_ERROR("Material system: no backend registered");
        return false;
    }
    Ca_Instance *ca = qs_engine_ca_instance(engine);
    if (!g_material_backend->init(ca, &state->ctx)) {
        QS_LOG_ERROR("Material backend '%s' init failed", g_material_backend->name);
        return false;
    }
    g_material_ctx = state->ctx;
    QS_LOG_INFO("Material backend '%s' initialised", g_material_backend->name);
    return true;
}

static void material_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    Qs_MaterialSystemState *state = (Qs_MaterialSystemState *)qs_system_data(sys);
    if (g_material_backend && state->ctx)
        g_material_backend->shutdown(state->ctx);
    g_material_ctx = NULL;
    QS_LOG_INFO("Material backend shut down");
}

Qs_SystemDesc qs_material_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Material",
        .data_size = sizeof(Qs_MaterialSystemState),
        .init      = material_sys_init,
        .shutdown  = material_sys_shutdown,
        .update    = NULL,
    };
}

Qs_Material *qs_material_create(Qs_Engine *engine, const Qs_MaterialDesc *desc)
{
    if (!g_material_backend || !g_material_backend->create) return NULL;
    return g_material_backend->create(g_material_ctx, engine, desc);
}

void qs_material_destroy(Qs_Material *material)
{
    if (!material || !g_material_backend || !g_material_backend->destroy) return;
    g_material_backend->destroy(g_material_ctx, material);
}

const char *qs_material_name(const Qs_Material *material)
{
    if (!material || !g_material_backend || !g_material_backend->mat_name) return NULL;
    return g_material_backend->mat_name(material);
}

VkDescriptorSet qs_material_descriptor_set(const Qs_Material *material)
{
    if (!material || !g_material_backend || !g_material_backend->descriptor_set) return VK_NULL_HANDLE;
    return g_material_backend->descriptor_set(material);
}

VkDescriptorSetLayout qs_material_set_layout(void)
{
    if (!g_material_backend || !g_material_backend->set_layout) return VK_NULL_HANDLE;
    return g_material_backend->set_layout(g_material_ctx);
}

const Qs_PBRParams *qs_material_params(const Qs_Material *material)
{
    if (!material || !g_material_backend || !g_material_backend->params) return NULL;
    return g_material_backend->params(material);
}

Qs_AlphaMode qs_material_alpha_mode(const Qs_Material *material)
{
    if (!material || !g_material_backend || !g_material_backend->alpha_mode) return QS_ALPHA_MODE_OPAQUE;
    return g_material_backend->alpha_mode(material);
}

bool qs_material_double_sided(const Qs_Material *material)
{
    if (!material || !g_material_backend || !g_material_backend->double_sided) return false;
    return g_material_backend->double_sided(material);
}
