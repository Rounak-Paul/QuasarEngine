/*
 * pbr_renderer.c — PBR renderer backend system lifecycle.
 *
 * The engine now owns:
 *   - Per-frame viewport callbacks (on_render / on_resize)
 *   - Camera, clear colour, render node list
 *   - Depth buffer, engine-declared render attachments
 *   - frame_ubo and lights_ubo (written by engine each frame)
 *
 * The backend is responsible for:
 *   - Initialising the Vulkan render system (GPU context cache)
 *   - Creating / destroying per-renderer GPU resources (pipelines,
 *     descriptor sets, shadow UBO) via pbr_forward_attach / detach
 *   - Re-writing descriptor sets after resize via pbr_forward_on_resize
 */

#include "qs_renderer.h"
#include "qs_log.h"
#include "pbr_internal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
   RENDER SYSTEM DATA
   ================================================================ */

typedef struct {
    Qs_GpuContext   *gpu;
    PbrPassResources  passes;  /* shared pipelines / samplers / layouts */
} VkRenderSystemData;

static VkRenderSystemData *g_render_system;

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool pbr_render_init(Qs_Engine *engine, Qs_GpuContext *gpu, void **out_ctx)
{
    (void)engine;
    VkRenderSystemData *data = calloc(1, sizeof(VkRenderSystemData));
    if (!data) return false;
    data->gpu      = gpu;
    g_render_system = data;
    *out_ctx        = data;
    QS_LOG_INFO("PBR Renderer: render system initialised");
    return true;
}

static void pbr_render_shutdown(void *ctx)
{
    VkRenderSystemData *data = ctx;
    /* All renderer instances are destroyed before shutdown is called */
    pbr_pass_resources_shutdown(data->gpu, &data->passes);
    g_render_system = NULL;
    free(data);
    QS_LOG_INFO("PBR Renderer: render system shut down");
}

/* ================================================================
   RENDERER LIFECYCLE
   ================================================================ */

static void *pbr_renderer_create(void *ctx, Qs_Engine *engine,
                                 const Qs_RendererDesc *desc, Qs_Renderer *handle)
{
    VkRenderSystemData *sys = ctx;
    if (!sys || !desc || !handle) return NULL;

    PbrRenderer *r = calloc(1, sizeof(PbrRenderer));
    if (!r) return NULL;

    r->engine          = engine;
    r->engine_renderer = handle;
    r->gpu             = sys->gpu;

    if (desc->name)
        snprintf(r->name, sizeof(r->name), "%s", desc->name);
    else
        snprintf(r->name, sizeof(r->name), "renderer");

    QS_LOG_INFO("PBR Renderer: '%s' created", r->name);

    /* Attach the forward pass — declares attachments and adds render nodes. */
    pbr_forward_attach(engine, r, handle);
    return r;
}

static void pbr_renderer_destroy(void *ctx, void *impl)
{
    (void)ctx;
    PbrRenderer *r = impl;
    if (!r) return;

    pbr_forward_detach(r);

    QS_LOG_INFO("PBR Renderer: '%s' destroyed", r->name);
    free(r);
}

static void pbr_renderer_on_resize(void *ctx, void *impl, uint32_t w, uint32_t h)
{
    (void)ctx;
    pbr_forward_on_resize((PbrRenderer *)impl, w, h);
}

/* ================================================================
   PASS RESOURCES ACCESSOR
   ================================================================ */

PbrPassResources *pbr_renderer_pass_resources(void)
{
    return g_render_system ? &g_render_system->passes : NULL;
}

/* ================================================================
   BACKEND STRUCT
   ================================================================ */

const Qs_RendererBackend pbr_renderer_backend = {
    .name               = "PBRRenderer",
    .init               = pbr_render_init,
    .shutdown           = pbr_render_shutdown,
    .update             = NULL,
    .renderer_create    = pbr_renderer_create,
    .renderer_destroy   = pbr_renderer_destroy,
    .renderer_on_resize = pbr_renderer_on_resize,
};
