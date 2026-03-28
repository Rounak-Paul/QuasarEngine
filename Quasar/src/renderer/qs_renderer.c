#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_system.h"
#include "qs_log.h"

#include <stddef.h>

/* ================================================================
   REGISTERED BACKEND
   ================================================================ */

const Qs_RendererBackend *g_renderer_backend;
void                     *g_render_ctx;

void qs_renderer_backend_register(const Qs_RendererBackend *backend)
{
    g_renderer_backend = backend;
}

/* ================================================================
   ENGINE SYSTEM
   ================================================================ */

typedef struct { void *ctx; } Qs_RenderSystemState;

static bool render_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    Qs_RenderSystemState *state = (Qs_RenderSystemState *)qs_system_data(sys);
    if (!g_renderer_backend) {
        QS_LOG_ERROR("Render system: no backend registered");
        return false;
    }
    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    if (!g_renderer_backend->init(engine, gpu, &state->ctx)) {
        QS_LOG_ERROR("Render backend '%s' init failed", g_renderer_backend->name);
        return false;
    }
    g_render_ctx = state->ctx;
    QS_LOG_INFO("Render backend '%s' initialised", g_renderer_backend->name);
    return true;
}

static void render_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    Qs_RenderSystemState *state = (Qs_RenderSystemState *)qs_system_data(sys);
    if (g_renderer_backend && state->ctx)
        g_renderer_backend->shutdown(state->ctx);
    g_render_ctx = NULL;
    QS_LOG_INFO("Render backend shut down");
}

static void render_sys_update(Qs_System *sys, Qs_Engine *engine, float dt)
{
    (void)engine;
    Qs_RenderSystemState *state = (Qs_RenderSystemState *)qs_system_data(sys);
    if (g_renderer_backend && g_renderer_backend->update && state->ctx)
        g_renderer_backend->update(state->ctx, dt);
}

/* Internal — called from engine.c, not part of the public header. */
Qs_SystemDesc qs_render_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Render",
        .data_size = sizeof(Qs_RenderSystemState),
        .init      = render_sys_init,
        .shutdown  = render_sys_shutdown,
        .update    = render_sys_update,
    };
}

/* ================================================================
   PUBLIC API DISPATCHERS
   ================================================================ */

Qs_Renderer *qs_renderer_create(Qs_Engine *engine, const Qs_RendererDesc *desc)
{
    if (!g_renderer_backend || !g_renderer_backend->renderer_create) return NULL;
    return g_renderer_backend->renderer_create(g_render_ctx, engine, desc);
}

void qs_renderer_destroy(Qs_Renderer *renderer)
{
    if (!renderer || !g_renderer_backend || !g_renderer_backend->renderer_destroy) return;
    g_renderer_backend->renderer_destroy(g_render_ctx, renderer);
}

void qs_renderer_bind(Qs_Renderer *renderer, Qs_Viewport *viewport)
{
    if (!renderer || !viewport || !g_renderer_backend || !g_renderer_backend->renderer_bind) return;
    g_renderer_backend->renderer_bind(g_render_ctx, renderer, viewport);
}

Qs_Camera *qs_renderer_camera(Qs_Renderer *renderer)
{
    if (!renderer || !g_renderer_backend || !g_renderer_backend->renderer_camera) return NULL;
    return g_renderer_backend->renderer_camera(renderer);
}

void qs_renderer_set_clear_color(Qs_Renderer *renderer, const float color[4])
{
    if (!renderer || !g_renderer_backend || !g_renderer_backend->renderer_set_clear_color) return;
    g_renderer_backend->renderer_set_clear_color(renderer, color);
}

Qs_RenderNode *qs_renderer_add_node(Qs_Renderer *renderer, const Qs_RenderNodeDesc *desc)
{
    if (!renderer || !desc || !g_renderer_backend || !g_renderer_backend->renderer_add_node) return NULL;
    return g_renderer_backend->renderer_add_node(renderer, desc);
}

void qs_renderer_remove_node(Qs_Renderer *renderer, Qs_RenderNode *node)
{
    if (!renderer || !node || !g_renderer_backend || !g_renderer_backend->renderer_remove_node) return;
    g_renderer_backend->renderer_remove_node(renderer, node);
}

const char *qs_renderer_name(const Qs_Renderer *renderer)
{
    if (!renderer || !g_renderer_backend || !g_renderer_backend->renderer_name) return NULL;
    return g_renderer_backend->renderer_name(renderer);
}

void qs_renderer_extents(const Qs_Renderer *renderer,
                          uint32_t *out_width, uint32_t *out_height)
{
    if (out_width)  *out_width  = 0;
    if (out_height) *out_height = 0;
    if (!renderer || !g_renderer_backend || !g_renderer_backend->renderer_extents) return;
    g_renderer_backend->renderer_extents(renderer, out_width, out_height);
}

void qs_renderer_submit_light(Qs_Renderer *renderer, Qs_Light *light)
{
    if (!renderer || !light || !g_renderer_backend || !g_renderer_backend->submit_light) return;
    g_renderer_backend->submit_light(renderer, light);
}

void qs_renderer_clear_lights(Qs_Renderer *renderer)
{
    if (!renderer || !g_renderer_backend || !g_renderer_backend->clear_lights) return;
    g_renderer_backend->clear_lights(renderer);
}

const Qs_LightGPU *qs_renderer_lights(const Qs_Renderer *renderer, uint32_t *out_count)
{
    if (out_count) *out_count = 0;
    if (!renderer || !g_renderer_backend || !g_renderer_backend->get_lights) return NULL;
    return g_renderer_backend->get_lights(renderer, out_count);
}
