#include "qs_light.h"
#include "qs_system.h"
#include "qs_log.h"

#include <stddef.h>

static const Qs_LightBackend *g_light_backend;
static void                  *g_light_ctx;

void qs_light_backend_register(const Qs_LightBackend *backend)
{
    g_light_backend = backend;
}

typedef struct { void *ctx; } Qs_LightSystemState;

static bool light_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    Qs_LightSystemState *state = (Qs_LightSystemState *)qs_system_data(sys);
    if (!g_light_backend) {
        QS_LOG_ERROR("Light system: no backend registered");
        return false;
    }
    if (!g_light_backend->init(&state->ctx)) {
        QS_LOG_ERROR("Light backend '%s' init failed", g_light_backend->name);
        return false;
    }
    g_light_ctx = state->ctx;
    QS_LOG_INFO("Light backend '%s' initialised", g_light_backend->name);
    return true;
}

static void light_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    Qs_LightSystemState *state = (Qs_LightSystemState *)qs_system_data(sys);
    if (g_light_backend && state->ctx)
        g_light_backend->shutdown(state->ctx);
    g_light_ctx = NULL;
    QS_LOG_INFO("Light backend shut down");
}

Qs_SystemDesc qs_light_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Light",
        .data_size = sizeof(Qs_LightSystemState),
        .init      = light_sys_init,
        .shutdown  = light_sys_shutdown,
        .update    = NULL,
    };
}

Qs_Light *qs_light_create(Qs_Engine *engine, const Qs_LightDesc *desc)
{
    if (!g_light_backend || !g_light_backend->create) return NULL;
    return g_light_backend->create(g_light_ctx, engine, desc);
}

void qs_light_destroy(Qs_Light *light)
{
    if (!light || !g_light_backend || !g_light_backend->destroy) return;
    g_light_backend->destroy(g_light_ctx, light);
}

const char *qs_light_name(const Qs_Light *light)
{
    if (!light || !g_light_backend || !g_light_backend->light_name) return NULL;
    return g_light_backend->light_name(light);
}

float *qs_light_position(Qs_Light *light)
{
    if (!light || !g_light_backend || !g_light_backend->position) return NULL;
    return g_light_backend->position(light);
}

float *qs_light_direction(Qs_Light *light)
{
    if (!light || !g_light_backend || !g_light_backend->direction) return NULL;
    return g_light_backend->direction(light);
}

float *qs_light_color(Qs_Light *light)
{
    if (!light || !g_light_backend || !g_light_backend->color) return NULL;
    return g_light_backend->color(light);
}

void qs_light_set_intensity(Qs_Light *light, float intensity)
{
    if (!light || !g_light_backend || !g_light_backend->set_intensity) return;
    g_light_backend->set_intensity(light, intensity);
}

float qs_light_intensity(const Qs_Light *light)
{
    if (!light || !g_light_backend || !g_light_backend->intensity) return 0.0f;
    return g_light_backend->intensity(light);
}

void qs_light_set_range(Qs_Light *light, float range)
{
    if (!light || !g_light_backend || !g_light_backend->set_range) return;
    g_light_backend->set_range(light, range);
}

void qs_light_set_cone(Qs_Light *light, float inner_deg, float outer_deg)
{
    if (!light || !g_light_backend || !g_light_backend->set_cone) return;
    g_light_backend->set_cone(light, inner_deg, outer_deg);
}

void qs_light_set_enabled(Qs_Light *light, bool enabled)
{
    if (!light || !g_light_backend || !g_light_backend->set_enabled) return;
    g_light_backend->set_enabled(light, enabled);
}

bool qs_light_enabled(const Qs_Light *light)
{
    if (!light || !g_light_backend || !g_light_backend->enabled) return false;
    return g_light_backend->enabled(light);
}
