/*
 * pbr_renderer.c  —  PBR renderer backend: builds and runs the render graph.
 *
 * The render graph is a DAG of typed nodes.  Fixed nodes (shadow, pbr, tonemap)
 * are always present.  Dynamic nodes (sky, bloom, vignette, etc.) are contributed
 * by plugins registered at the QS_EXT_RENDER_GRAPH_NODE extension point.
 *
 * Default pipeline (connections set up in pbr_renderer_create):
 *
 *   [SkyNode?]   ──────────────────────────────→ PBR.sky_color
 *   [ShadowNode] → shadow_map_0/1/2, shadow_ubo → PBR.shadow_*
 *   [PBRNode]    → hdr_color → [BloomNode?] → bloom_tex → Tonemap.bloom_tex
 *                → hdr_color ─────────────────────────── → Tonemap.hdr_color
 *   [Tonemap]    → ldr_color → [VignetteNode?] → color_out → swapchain
 *                (or Tonemap → swapchain when no POST_TONEMAP nodes)
 */

#include "qs_render_graph.h"
#include "qs_renderer.h"
#include "qs_ext.h"
#include "qs_gpu.h"
#include "qs_log.h"
#include "qs_memory.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   RENDER SYSTEM DATA  (shared across all renderer instances)
   ================================================================ */

typedef struct {
    Qs_GpuContext *gpu;
} RenderSystemData;

static RenderSystemData *g_render_system;

/* ================================================================
   PER-RENDERER INSTANCE DATA
   ================================================================ */

typedef struct {
    Qs_Engine      *engine;
    Qs_Renderer    *renderer;
    Qs_GpuContext  *gpu;
    Qs_RenderGraph *rg;
    Qs_RenderNode  *engine_node; /* single node registered with engine */
    char            name[64];
} PbrRendererInstance;

/* ================================================================
   GRAPH EXECUTE CALLBACK  (called by engine each frame)
   ================================================================ */

static void graph_execute(const Qs_RenderContext *ctx, void *user_data)
{
    qs_rg_execute((Qs_RenderGraph *)user_data, ctx);
}

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool pbr_render_init(Qs_Engine *engine, Qs_GpuContext *gpu, void **out_ctx)
{
    (void)engine;
    RenderSystemData *data = qs_calloc(1, sizeof(RenderSystemData), QS_MEM_RENDER);
    if (!data) return false;
    data->gpu       = gpu;
    g_render_system = data;
    *out_ctx        = data;
    QS_LOG_INFO("PBR Renderer: render system initialised");
    return true;
}

static void pbr_render_shutdown(void *ctx)
{
    RenderSystemData *data = ctx;
    (void)data;
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
    RenderSystemData *sys = ctx;
    if (!sys || !handle) return NULL;

    PbrRendererInstance *inst = qs_calloc(1, sizeof(PbrRendererInstance), QS_MEM_RENDER);
    if (!inst) return NULL;
    inst->engine   = engine;
    inst->renderer = handle;
    inst->gpu      = sys->gpu;
    if (desc && desc->name)
        snprintf(inst->name, sizeof(inst->name), "%s", desc->name);
    else
        snprintf(inst->name, sizeof(inst->name), "renderer");

    /* --- Create the render graph --- */
    Qs_RenderGraph *rg = qs_rg_create(engine, sys->gpu);
    if (!rg) {
        QS_LOG_ERROR("PBR Renderer: render graph creation failed");
        qs_free(inst);
        return NULL;
    }
    inst->rg = rg;

    /* --- Fixed nodes --- */
    Qs_RgNode *shadow_node = qs_rg_add_node(rg, &qs_rg_shadow_node_type);
    Qs_RgNode *pbr_node    = qs_rg_add_node(rg, &qs_rg_pbr_node_type);
    Qs_RgNode *tonemap_node = qs_rg_add_node(rg, &qs_rg_tonemap_node_type);
    if (!shadow_node || !pbr_node || !tonemap_node) {
        QS_LOG_ERROR("PBR Renderer: fixed node creation failed");
        qs_rg_destroy(rg);
        qs_free(inst);
        return NULL;
    }

    /* Shadow → PBR */
    qs_rg_connect_named(rg, shadow_node, "shadow_map_0", pbr_node, "shadow_map_0");
    qs_rg_connect_named(rg, shadow_node, "shadow_map_1", pbr_node, "shadow_map_1");
    qs_rg_connect_named(rg, shadow_node, "shadow_map_2", pbr_node, "shadow_map_2");
    qs_rg_connect_named(rg, shadow_node, "shadow_ubo",   pbr_node, "shadow_ubo");

    /* PBR → Tonemap */
    qs_rg_connect_named(rg, pbr_node, "hdr_color", tonemap_node, "hdr_color");

    /* --- Dynamic nodes from plugins --- */
    uint32_t ext_count = qs_engine_ext_count(engine, QS_EXT_RENDER_GRAPH_NODE);
    Qs_RgNode *last_post_tonemap = tonemap_node;

    for (uint32_t i = 0; i < ext_count; i++) {
        const Qs_RgNodeTypeExt *ext = qs_engine_ext_interface(
            engine, QS_EXT_RENDER_GRAPH_NODE, i);
        if (!ext || !ext->type) continue;

        Qs_RgNode *node = qs_rg_add_node(rg, ext->type);
        if (!node) {
            QS_LOG_ERROR("PBR Renderer: dynamic node '%s' creation failed", ext->type->name);
            continue;
        }
        QS_LOG_INFO("PBR Renderer: adding dynamic node '%s' (stage %d)",
                    ext->type->name, (int)ext->stage);

        switch (ext->stage) {
        case QS_RG_STAGE_PRE_GEOMETRY:
            /* Sky: node.color_out → PBR.sky_color */
            if (!qs_rg_connect_named(rg, node, "color_out", pbr_node, "sky_color"))
                QS_LOG_ERROR("PBR Renderer: failed to connect PRE_GEOMETRY node '%s'",
                             ext->type->name);
            break;

        case QS_RG_STAGE_POST_GEOMETRY:
            /* Bloom: PBR.hdr_color → node.color_in; node.bloom_tex → tonemap.bloom_tex */
            qs_rg_connect_named(rg, pbr_node, "hdr_color", node, "color_in");
            qs_rg_connect_named(rg, node, "bloom_tex", tonemap_node, "bloom_tex");
            break;

        case QS_RG_STAGE_POST_TONEMAP: {
            /* Chain: prev.ldr_color|color_out → node.color_in */
            const char *prev_out = (last_post_tonemap == tonemap_node)
                                 ? "ldr_color" : "color_out";
            if (!qs_rg_connect_named(rg, last_post_tonemap, prev_out, node, "color_in"))
                QS_LOG_ERROR("PBR Renderer: failed to chain POST_TONEMAP node '%s'",
                             ext->type->name);
            last_post_tonemap = node;
            break;
        }
        }
    }

    /* --- Compile the graph --- */
    if (!qs_rg_compile(rg)) {
        QS_LOG_ERROR("PBR Renderer: render graph compilation failed");
        qs_rg_destroy(rg);
        qs_free(inst);
        return NULL;
    }

    /* --- Register the graph as a single engine render node --- */
    inst->engine_node = qs_renderer_add_node(handle, &(Qs_RenderNodeDesc){
        .name     = "render_graph",
        .priority = 0,
        .execute  = graph_execute,
        .user_data = rg,
    });
    if (!inst->engine_node) {
        QS_LOG_ERROR("PBR Renderer: failed to register render node with engine");
        qs_rg_destroy(rg);
        qs_free(inst);
        return NULL;
    }

    QS_LOG_INFO("PBR Renderer: '%s' created", inst->name);
    return inst;
}

static void pbr_renderer_destroy(void *ctx, void *impl)
{
    (void)ctx;
    PbrRendererInstance *inst = impl;
    if (!inst) return;
    if (inst->engine_node)
        qs_renderer_remove_node(inst->renderer, inst->engine_node);
    qs_rg_destroy(inst->rg);
    QS_LOG_INFO("PBR Renderer: '%s' destroyed", inst->name);
    qs_free(inst);
}

static void pbr_renderer_on_resize(void *ctx, void *impl, uint32_t w, uint32_t h)
{
    (void)ctx;
    PbrRendererInstance *inst = impl;
    if (!inst || !inst->rg) return;
    qs_rg_on_resize(inst->rg, w, h);

    /* Report max MSAA to engine renderer for the settings panel */
    Qs_RgNode *pbr_node = qs_rg_find_node(inst->rg, "forward_pbr");
    if (pbr_node) {
        /* PBR node stores dev_max_samples; expose it through the renderer API.
           We access it via a getter exported by the PBR node type. */
        extern uint32_t qs_rg_pbr_node_dev_max_samples(Qs_RgNode *node);
        uint32_t max_s = qs_rg_pbr_node_dev_max_samples(pbr_node);
        qs_renderer_set_max_msaa_samples(inst->renderer, max_s);
    }
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
