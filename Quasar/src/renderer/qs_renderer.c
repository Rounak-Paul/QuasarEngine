#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_system.h"
#include "qs_log.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
   ENGINE-OWNED RENDERER HANDLE
   ================================================================ */

/* Full definition of the opaque Qs_Renderer.  The engine dispatch layer
   owns handle allocation; the backend owns the impl allocation. */
struct Qs_Renderer {
    const Qs_RendererBackend *backend;  ///< Which backend created this renderer.
    void                     *ctx;      ///< That backend's system-level context.
    void                     *impl;     ///< Backend-internal renderer pointer.
};

/* ================================================================
   BACKEND REGISTRY
   ================================================================ */

#define QS_MAX_RENDERER_BACKENDS 8

typedef struct {
    const Qs_RendererBackend *backend;
    void                     *ctx;       /* Non-NULL after successful init */
} BackendEntry;

static BackendEntry  g_backends[QS_MAX_RENDERER_BACKENDS];
static uint32_t      g_backend_count;
static const char   *g_default_backend_name; /* NULL = first registered */
static bool          g_system_running;       /* True while render system is up */
static Qs_Engine    *g_engine_ref;           /* Kept for late-register hot-init */
static Qs_GpuContext *g_gpu_ref;

/* Returns the BackendEntry for the given name, or NULL.  NULL name returns
   the default backend (first registered unless overridden). */
static BackendEntry *find_backend(const char *name)
{
    if (!name) name = g_default_backend_name;

    if (name) {
        for (uint32_t i = 0; i < g_backend_count; i++) {
            if (g_backends[i].backend &&
                strcmp(g_backends[i].backend->name, name) == 0)
                return &g_backends[i];
        }
        return NULL;
    }

    /* No explicit name and no default set: use the first available */
    return (g_backend_count > 0) ? &g_backends[0] : NULL;
}

void qs_renderer_backend_register(const Qs_RendererBackend *backend)
{
    if (!backend) return;

    if (g_backend_count >= QS_MAX_RENDERER_BACKENDS) {
        QS_LOG_ERROR("Render backend registry full (max %d)", QS_MAX_RENDERER_BACKENDS);
        return;
    }

    /* Ignore duplicates */
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (g_backends[i].backend &&
            strcmp(g_backends[i].backend->name, backend->name) == 0) {
            QS_LOG_WARN("Render backend '%s' already registered", backend->name);
            return;
        }
    }

    g_backends[g_backend_count].backend = backend;
    g_backends[g_backend_count].ctx     = NULL;

    /* If the render system is already running, initialize this backend now */
    if (g_system_running && g_engine_ref && g_gpu_ref) {
        if (!backend->init(g_engine_ref, g_gpu_ref, &g_backends[g_backend_count].ctx)) {
            QS_LOG_ERROR("Late-registered render backend '%s' init failed", backend->name);
            g_backends[g_backend_count].backend = NULL;
            return;
        }
        QS_LOG_INFO("Render backend '%s' hot-registered", backend->name);
    }

    /* First registered backend becomes the default unless one is already set */
    if (g_backend_count == 0 && !g_default_backend_name)
        g_default_backend_name = backend->name;

    g_backend_count++;
    QS_LOG_INFO("Render backend '%s' registered (%u total)", backend->name, g_backend_count);
}

void qs_renderer_backend_unregister(const char *name)
{
    if (!name) return;

    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (!g_backends[i].backend) continue;
        if (strcmp(g_backends[i].backend->name, name) != 0) continue;

        if (g_system_running && g_backends[i].ctx) {
            g_backends[i].backend->shutdown(g_backends[i].ctx);
            QS_LOG_INFO("Render backend '%s' shut down", name);
        }

        /* If this was the default, transfer default to the next entry */
        if (g_default_backend_name &&
            strcmp(g_default_backend_name, name) == 0) {
            g_default_backend_name = NULL;
            for (uint32_t j = 0; j < g_backend_count; j++) {
                if (j != i && g_backends[j].backend) {
                    g_default_backend_name = g_backends[j].backend->name;
                    break;
                }
            }
        }

        /* Compact the array */
        for (uint32_t j = i; j + 1 < g_backend_count; j++)
            g_backends[j] = g_backends[j + 1];
        g_backends[g_backend_count - 1].backend = NULL;
        g_backends[g_backend_count - 1].ctx     = NULL;
        g_backend_count--;
        QS_LOG_INFO("Render backend '%s' unregistered", name);
        return;
    }

    QS_LOG_WARN("Render backend '%s' not found for unregister", name);
}

void qs_renderer_backend_set_default(const char *name)
{
    g_default_backend_name = name;
}

/* ================================================================
   ENGINE SYSTEM
   ================================================================ */

static bool render_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    (void)sys;
    if (g_backend_count == 0) {
        QS_LOG_ERROR("Render system: no backends registered");
        return false;
    }

    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    g_engine_ref = engine;
    g_gpu_ref    = gpu;

    bool any_ok = false;
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (!g_backends[i].backend) continue;
        if (!g_backends[i].backend->init(engine, gpu, &g_backends[i].ctx)) {
            QS_LOG_ERROR("Render backend '%s' init failed", g_backends[i].backend->name);
            g_backends[i].ctx = NULL;
            continue;
        }
        QS_LOG_INFO("Render backend '%s' initialised", g_backends[i].backend->name);
        any_ok = true;
    }

    if (!any_ok) {
        QS_LOG_ERROR("Render system: all backends failed to initialise");
        return false;
    }

    g_system_running = true;
    return true;
}

static void render_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)sys;
    (void)engine;
    g_system_running = false;

    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (g_backends[i].backend && g_backends[i].ctx) {
            g_backends[i].backend->shutdown(g_backends[i].ctx);
            g_backends[i].ctx = NULL;
            QS_LOG_INFO("Render backend '%s' shut down", g_backends[i].backend->name);
        }
    }

    g_engine_ref = NULL;
    g_gpu_ref    = NULL;
}

static void render_sys_update(Qs_System *sys, Qs_Engine *engine, float dt)
{
    (void)sys;
    (void)engine;
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (g_backends[i].backend && g_backends[i].ctx &&
            g_backends[i].backend->update)
            g_backends[i].backend->update(g_backends[i].ctx, dt);
    }
}

/* Internal — called from engine.c, not part of the public header. */
Qs_SystemDesc qs_render_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Render",
        .data_size = 0,
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
    BackendEntry *entry = find_backend(desc ? desc->backend : NULL);
    if (!entry || !entry->ctx || !entry->backend->renderer_create) {
        QS_LOG_ERROR("qs_renderer_create: backend '%s' not available",
                     desc && desc->backend ? desc->backend : "(default)");
        return NULL;
    }

    void *impl = entry->backend->renderer_create(entry->ctx, engine, desc);
    if (!impl) return NULL;

    Qs_Renderer *r = calloc(1, sizeof(Qs_Renderer));
    if (!r) {
        entry->backend->renderer_destroy(entry->ctx, impl);
        return NULL;
    }

    r->backend = entry->backend;
    r->ctx     = entry->ctx;
    r->impl    = impl;

    /* Give the backend a chance to store the engine handle back-reference */
    if (entry->backend->renderer_post_create)
        entry->backend->renderer_post_create(impl, r);

    return r;
}

void qs_renderer_destroy(Qs_Renderer *renderer)
{
    if (!renderer) return;
    if (renderer->backend && renderer->backend->renderer_destroy)
        renderer->backend->renderer_destroy(renderer->ctx, renderer->impl);
    free(renderer);
}

void qs_renderer_bind(Qs_Renderer *renderer, Qs_Viewport *viewport)
{
    if (!renderer || !viewport || !renderer->backend->renderer_bind) return;
    renderer->backend->renderer_bind(renderer->ctx, renderer->impl, viewport);
}

Qs_Camera *qs_renderer_camera(Qs_Renderer *renderer)
{
    if (!renderer || !renderer->backend->renderer_camera) return NULL;
    return renderer->backend->renderer_camera(renderer->impl);
}

void qs_renderer_set_clear_color(Qs_Renderer *renderer, const float color[4])
{
    if (!renderer || !renderer->backend->renderer_set_clear_color) return;
    renderer->backend->renderer_set_clear_color(renderer->impl, color);
}

Qs_RenderNode *qs_renderer_add_node(Qs_Renderer *renderer, const Qs_RenderNodeDesc *desc)
{
    if (!renderer || !desc || !renderer->backend->renderer_add_node) return NULL;
    return renderer->backend->renderer_add_node(renderer->impl, desc);
}

void qs_renderer_remove_node(Qs_Renderer *renderer, Qs_RenderNode *node)
{
    if (!renderer || !node || !renderer->backend->renderer_remove_node) return;
    renderer->backend->renderer_remove_node(renderer->impl, node);
}

const char *qs_renderer_name(const Qs_Renderer *renderer)
{
    if (!renderer || !renderer->backend->renderer_name) return NULL;
    return renderer->backend->renderer_name(renderer->impl);
}

void qs_renderer_extents(const Qs_Renderer *renderer,
                          uint32_t *out_width, uint32_t *out_height)
{
    if (out_width)  *out_width  = 0;
    if (out_height) *out_height = 0;
    if (!renderer || !renderer->backend->renderer_extents) return;
    renderer->backend->renderer_extents(renderer->impl, out_width, out_height);
}

void qs_renderer_submit_light(Qs_Renderer *renderer, Qs_Light *light)
{
    if (!renderer || !light || !renderer->backend->submit_light) return;
    renderer->backend->submit_light(renderer->impl, light);
}

void qs_renderer_clear_lights(Qs_Renderer *renderer)
{
    if (!renderer || !renderer->backend->clear_lights) return;
    renderer->backend->clear_lights(renderer->impl);
}

const Qs_LightGPU *qs_renderer_lights(const Qs_Renderer *renderer, uint32_t *out_count)
{
    if (out_count) *out_count = 0;
    if (!renderer || !renderer->backend->get_lights) return NULL;
    return renderer->backend->get_lights(renderer->impl, out_count);
}

/* ================================================================
   RENDERABLE SUBMISSION DISPATCHERS
   ================================================================ */

void qs_renderer_submit_renderable(Qs_Renderer *renderer, const Qs_Renderable *renderable)
{
    if (!renderer || !renderable || !renderer->backend->submit_renderable) return;
    renderer->backend->submit_renderable(renderer->impl, renderable);
}

void qs_renderer_clear_renderables(Qs_Renderer *renderer)
{
    if (!renderer || !renderer->backend->clear_renderables) return;
    renderer->backend->clear_renderables(renderer->impl);
}

const Qs_Renderable *qs_renderer_renderables(const Qs_Renderer *renderer, uint32_t *out_count)
{
    if (out_count) *out_count = 0;
    if (!renderer || !renderer->backend->get_renderables) return NULL;
    return renderer->backend->get_renderables(renderer->impl, out_count);
}



