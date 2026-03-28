/*
 * vk_renderer.c — Vulkan/PBR renderer backend system lifecycle.
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
 *     descriptor sets, shadow UBO) via vk_forward_attach / detach
 *   - Re-writing descriptor sets after resize via vk_forward_on_resize
 */

#include "qs_renderer.h"
#include "qs_log.h"
#include "vk_renderer_internal.h"

#include <string.h>
#include <stdlib.h>

/* ================================================================
   RENDER SYSTEM DATA
   ================================================================ */

typedef struct {
    Qs_GpuContext   *gpu;
    VkPassResources  passes;  /* shared pipelines / samplers / layouts */
} VkRenderSystemData;

static VkRenderSystemData *g_render_system;

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool vk_render_init(Qs_Engine *engine, Qs_GpuContext *gpu, void **out_ctx)
{
    (void)engine;
    VkRenderSystemData *data = calloc(1, sizeof(VkRenderSystemData));
    if (!data) return false;
    data->gpu      = gpu;
    g_render_system = data;
    *out_ctx        = data;
    QS_LOG_INFO("VkRenderer: render system initialised");
    return true;
}

static void vk_render_shutdown(void *ctx)
{
    VkRenderSystemData *data = ctx;
    /* All renderer instances are destroyed before shutdown is called */
    vk_pass_resources_shutdown(data->gpu, &data->passes);
    g_render_system = NULL;
    free(data);
    QS_LOG_INFO("VkRenderer: render system shut down");
}

/* ================================================================
   RENDERER LIFECYCLE
   ================================================================ */

static void *vk_renderer_create(void *ctx, Qs_Engine *engine,
                                 const Qs_RendererDesc *desc, Qs_Renderer *handle)
{
    VkRenderSystemData *sys = ctx;
    if (!sys || !desc || !handle) return NULL;

    VkRenderer *r = calloc(1, sizeof(VkRenderer));
    if (!r) return NULL;

    r->engine          = engine;
    r->engine_renderer = handle;
    r->gpu             = sys->gpu;

    if (desc->name)
        snprintf(r->name, sizeof(r->name), "%s", desc->name);
    else
        snprintf(r->name, sizeof(r->name), "renderer");

    QS_LOG_INFO("VkRenderer: '%s' created", r->name);

    /* Attach the forward pass — declares attachments and adds render nodes. */
    vk_forward_attach(engine, r, handle);
    return r;
}

static void vk_renderer_destroy(void *ctx, void *impl)
{
    (void)ctx;
    VkRenderer *r = impl;
    if (!r) return;

    vk_forward_detach(r);

    QS_LOG_INFO("VkRenderer: '%s' destroyed", r->name);
    free(r);
}

static void vk_renderer_on_resize(void *ctx, void *impl, uint32_t w, uint32_t h)
{
    (void)ctx;
    vk_forward_on_resize((VkRenderer *)impl, w, h);
}

/* ================================================================
   PASS RESOURCES ACCESSOR
   ================================================================ */

VkPassResources *vk_renderer_pass_resources(void)
{
    return g_render_system ? &g_render_system->passes : NULL;
}

/* ================================================================
   BACKEND STRUCT
   ================================================================ */

const Qs_RendererBackend vk_renderer_backend = {
    .name               = "VulkanRenderer",
    .init               = vk_render_init,
    .shutdown           = vk_render_shutdown,
    .update             = NULL,
    .renderer_create    = vk_renderer_create,
    .renderer_destroy   = vk_renderer_destroy,
    .renderer_on_resize = vk_renderer_on_resize,
};
