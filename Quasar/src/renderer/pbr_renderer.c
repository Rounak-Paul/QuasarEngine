/*
 * pbr_renderer.c — PBR renderer backend system lifecycle.
 *
 * Implements the Qs_RendererBackend vtable for the built-in PBR renderer.
 * Registered directly in engine.c; no plugin loading required.
 *
 * The engine owns: per-frame viewport callbacks, camera, clear colour, render
 * node list, depth buffer, declared render attachments, frame_ubo, lights_ubo,
 * and the default material.
 *
 * This backend owns: pipelines, descriptor sets, shadow UBO (CSM data), and
 * MSAA transient resources — all via pbr_forward_attach / detach / on_resize.
 */

#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_log.h"
#include "qs_memory.h"
#include "pbr_internal.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   RENDER SYSTEM DATA
   ================================================================ */

typedef struct {
    Qs_GpuContext   *gpu;
    PbrPassResources  passes;
} VkRenderSystemData;

static VkRenderSystemData *g_render_system;

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool pbr_render_init(Qs_Engine *engine, Qs_GpuContext *gpu, void **out_ctx)
{
    (void)engine;
    VkRenderSystemData *data = qs_calloc(1, sizeof(VkRenderSystemData), QS_MEM_RENDER);
    if (!data) return false;
    data->gpu       = gpu;
    g_render_system = data;
    *out_ctx        = data;
    QS_LOG_INFO("PBR Renderer: render system initialised");
    return true;
}

static void pbr_render_shutdown(void *ctx)
{
    VkRenderSystemData *data = ctx;
    pbr_pass_resources_shutdown(data->gpu, &data->passes);
    g_render_system = NULL;
    qs_free(data);
    QS_LOG_INFO("PBR Renderer: render system shut down");
}

/* ================================================================
   RENDERER INSTANCE LIFECYCLE
   ================================================================ */

static void *pbr_renderer_create(void *ctx, Qs_Engine *engine,
                                 const Qs_RendererDesc *desc, Qs_Renderer *handle)
{
    VkRenderSystemData *sys = ctx;
    if (!sys || !handle) return NULL;

    PbrRenderer *r = qs_calloc(1, sizeof(PbrRenderer), QS_MEM_RENDER);
    if (!r) return NULL;

    r->engine          = engine;
    r->engine_renderer = handle;
    r->gpu             = sys->gpu;

    if (desc && desc->name)
        snprintf(r->name, sizeof(r->name), "%s", desc->name);
    else
        snprintf(r->name, sizeof(r->name), "renderer");

    /* Attach the forward pass — declares attachments and adds render nodes. */
    pbr_forward_attach(engine, r, handle);

    /* Report device maximum MSAA to the engine renderer. */
    PbrPassResources *ps = pbr_renderer_pass_resources();
    if (ps) qs_renderer_set_max_msaa_samples(handle, ps->dev_max_samples);

    QS_LOG_INFO("PBR Renderer: '%s' created (max MSAA %ux)",
                r->name, ps ? ps->dev_max_samples : 1);
    return r;
}

static void pbr_renderer_destroy(void *ctx, void *impl)
{
    (void)ctx;
    PbrRenderer *r = impl;
    if (!r) return;
    pbr_forward_detach(r);
    QS_LOG_INFO("PBR Renderer: '%s' destroyed", r->name);
    qs_free(r);
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
