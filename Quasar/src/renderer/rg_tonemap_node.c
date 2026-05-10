/*
 * rg_tonemap_node.c  —  ACES tonemap + gamma correction fixed render graph node.
 *
 * Ports
 *   Inputs:
 *     [0] hdr_color  TEXTURE           — HDR scene colour (from forward_pbr)
 *     [1] bloom_tex  TEXTURE (optional) — bloom contribution (from bloom plugin)
 *   Outputs:
 *     [0] ldr_color  TEXTURE            — gamma-corrected LDR colour (BGRA8)
 *
 * When this node is the terminal node (ctx->swapchain_view != NULL) it renders
 * directly into the swapchain image rather than the intermediate ldr_color texture.
 */

#include "qs_render_graph.h"
#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_memory.h"
#include "qs_log.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ================================================================
   GLSL SHADERS
   ================================================================ */

static const char *FULLSCREEN_VERT =
    "#version 450\n"
    "void main() {\n"
    "    vec2 pos=vec2((gl_VertexIndex==2)?3.0:-1.0,(gl_VertexIndex==1)?3.0:-1.0);\n"
    "    gl_Position=vec4(pos,0.0,1.0);\n"
    "}\n";

static const char *TONEMAP_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_hdr;\n"
    "layout(set=0,binding=1) uniform sampler2D u_bloom;\n"
    "layout(push_constant) uniform PC { vec2 inv_size; float bloom_str; float _pad; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "vec3 aces(vec3 x) { return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }\n"
    "void main() {\n"
    "    vec2 uv=gl_FragCoord.xy*pc.inv_size;\n"
    "    vec3 color=texture(u_hdr,uv).rgb + texture(u_bloom,uv).rgb*pc.bloom_str;\n"
    "    color=aces(color);\n"
    "    color=pow(color,vec3(1.0/2.2));\n"
    "    out_color=vec4(color,1.0);\n"
    "}\n";

/* ================================================================
   NODE STATE
   ================================================================ */

typedef struct {
    Qs_GpuContext *gpu;

    /* Intermediate LDR output (used when non-terminal) */
    Qs_GpuImage     *ldr_image;
    Qs_GpuImageView *ldr_view;

    /* Tonemap pipeline — BGRA8 format, works for both swapchain and intermediate */
    Qs_GpuPipeline            *pipeline;
    Qs_GpuPipelineLayout      *layout;
    Qs_GpuDescriptorSetLayout *set_layout; /* 2 combined image samplers */
    Qs_GpuDescriptorPool      *pool;
    Qs_GpuDescriptorSet       *desc_set;
    Qs_GpuSampler             *sampler;

    /* Null bloom texture (1×1 black) used when bloom port is unconnected */
    Qs_GpuImage     *null_bloom_image;
    Qs_GpuImageView *null_bloom_view;

    /* Track which views are currently bound to detect changes */
    Qs_GpuImageView *bound_hdr_view;
    Qs_GpuImageView *bound_bloom_view;

    bool ok;
} TonemapNodeState;

/* ================================================================
   NODE VTABLE
   ================================================================ */

static void *tonemap_create(Qs_Engine *engine, Qs_GpuContext *gpu)
{
    (void)engine;
    TonemapNodeState *s = qs_calloc(1, sizeof(TonemapNodeState), QS_MEM_RENDER);
    if (!s) return NULL;
    s->gpu = gpu;

    /* Descriptor set layout: 2 combined image samplers (hdr + bloom) */
    {
        Qs_GpuDescriptorBinding b[2] = {
            {0,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
            {1,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
        };
        s->set_layout = qs_gpu_create_descriptor_set_layout(gpu, b, 2);
        if (!s->set_layout) { QS_LOG_ERROR("TonemapNode: set layout failed"); return s; }
    }

    /* Pipeline layout */
    {
        Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_FRAGMENT, 0, 16};
        Qs_GpuDescriptorSetLayout *sets[] = {s->set_layout};
        s->layout = qs_gpu_create_pipeline_layout(gpu,
            &(Qs_GpuPipelineLayoutDesc){sets, 1, &pc, 1});
        if (!s->layout) { QS_LOG_ERROR("TonemapNode: pipeline layout failed"); return s; }
    }

    /* Pipeline — BGRA8 target (matches swapchain and ldr intermediate) */
    {
        Qs_GpuShader *fv = qs_gpu_compile_shader(gpu, FULLSCREEN_VERT, QS_GPU_SHADER_VERTEX);
        Qs_GpuShader *ff = qs_gpu_compile_shader(gpu, TONEMAP_FRAG,    QS_GPU_SHADER_FRAGMENT);
        if (!fv || !ff) {
            if (fv) qs_gpu_destroy_shader(gpu,fv);
            if (ff) qs_gpu_destroy_shader(gpu,ff);
            QS_LOG_ERROR("TonemapNode: shader compilation failed");
            return s;
        }
        s->pipeline = qs_gpu_create_graphics_pipeline(gpu,
            &(Qs_GpuGraphicsPipelineDesc){
                s->layout,fv,ff,NULL,0,
                QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_NONE,false,false,
                QS_GPU_FORMAT_BGRA8_UNORM,QS_GPU_FORMAT_DEPTH_AUTO});
        qs_gpu_destroy_shader(gpu,fv);
        qs_gpu_destroy_shader(gpu,ff);
        if (!s->pipeline) { QS_LOG_ERROR("TonemapNode: pipeline creation failed"); return s; }
    }

    /* Sampler */
    s->sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR,.mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,.mip_levels=1});
    if (!s->sampler) { QS_LOG_ERROR("TonemapNode: sampler creation failed"); return s; }

    /* Null bloom: 1×1 black RGBA16F image */
    s->null_bloom_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width=1,.height=1,.mip_levels=1,
        .format=QS_GPU_FORMAT_RGBA16_SFLOAT,
        .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT|QS_GPU_IMAGE_SAMPLED,
        .sample_count=1});
    if (s->null_bloom_image)
        s->null_bloom_view = qs_gpu_create_image_view_for(
            gpu, s->null_bloom_image, QS_GPU_IMAGE_ASPECT_COLOR);

    /* Descriptor pool + set */
    {
        Qs_GpuDescriptorPoolSize ps = {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 4};
        s->pool = qs_gpu_create_descriptor_pool(gpu,
            &(Qs_GpuDescriptorPoolDesc){&ps, 1, 2});
        if (!s->pool) { QS_LOG_ERROR("TonemapNode: pool failed"); return s; }
        s->desc_set = qs_gpu_alloc_descriptor_set(gpu, s->pool, s->set_layout);
        if (!s->desc_set) { QS_LOG_ERROR("TonemapNode: desc set alloc failed"); return s; }
    }

    QS_LOG_INFO("TonemapNode: created");
    return s;
}

static void tonemap_destroy(void *state, Qs_GpuContext *gpu)
{
    TonemapNodeState *s = state;
    if (!s) return;
    if (s->desc_set)       qs_gpu_free_descriptor_set(gpu, s->pool, s->desc_set);
    if (s->pool)           qs_gpu_destroy_descriptor_pool(gpu, s->pool);
    if (s->sampler)        qs_gpu_destroy_sampler(gpu, s->sampler);
    if (s->pipeline)       qs_gpu_destroy_pipeline(gpu, s->pipeline);
    if (s->layout)         qs_gpu_destroy_pipeline_layout(gpu, s->layout);
    if (s->set_layout)     qs_gpu_destroy_descriptor_set_layout(gpu, s->set_layout);
    if (s->null_bloom_view)  qs_gpu_destroy_image_view(gpu, s->null_bloom_view);
    if (s->null_bloom_image) qs_gpu_destroy_image(gpu, s->null_bloom_image);
    if (s->ldr_view)       qs_gpu_destroy_image_view(gpu, s->ldr_view);
    if (s->ldr_image)      qs_gpu_destroy_image(gpu, s->ldr_image);
    qs_free(s);
}

static void tonemap_on_resize(void *state, Qs_GpuContext *gpu, uint32_t w, uint32_t h)
{
    TonemapNodeState *s = state;
    if (!s) return;

    if (s->ldr_view)  { qs_gpu_destroy_image_view(gpu, s->ldr_view);  s->ldr_view  = NULL; }
    if (s->ldr_image) { qs_gpu_destroy_image(gpu, s->ldr_image);      s->ldr_image = NULL; }

    /* LDR output texture (BGRA8 — same format as swapchain for pipeline compat) */
    s->ldr_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width=w,.height=h,.mip_levels=1,
        .format=QS_GPU_FORMAT_BGRA8_UNORM,
        .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT|QS_GPU_IMAGE_SAMPLED,
        .sample_count=1});
    if (!s->ldr_image) { QS_LOG_ERROR("TonemapNode: ldr image failed"); return; }
    s->ldr_view = qs_gpu_create_image_view_for(gpu, s->ldr_image, QS_GPU_IMAGE_ASPECT_COLOR);

    /* Invalidate descriptor cache */
    s->bound_hdr_view   = NULL;
    s->bound_bloom_view = NULL;
    s->ok = (s->ldr_view != NULL);
}

static void tonemap_execute(void *state, const Qs_RgExecCtx *ctx)
{
    TonemapNodeState *s = state;
    if (!s) return;

    Qs_RgTexture hdr_tex   = ctx->inputs[0].texture;
    Qs_RgTexture bloom_tex = ctx->inputs[1].texture;

    if (!hdr_tex.view) return; /* required input missing */

    /* Fall back to 1×1 black when bloom is not connected */
    Qs_GpuImageView *bloom_view = bloom_tex.view ? bloom_tex.view : s->null_bloom_view;

    /* Update descriptors when input views change */
    if (hdr_tex.view != s->bound_hdr_view || bloom_view != s->bound_bloom_view) {
        if (s->desc_set && s->sampler) {
            qs_gpu_write_image_descriptor(ctx->gpu, s->desc_set, 0, s->sampler, hdr_tex.view);
            if (bloom_view)
                qs_gpu_write_image_descriptor(ctx->gpu, s->desc_set, 1, s->sampler, bloom_view);
        }
        s->bound_hdr_view   = hdr_tex.view;
        s->bound_bloom_view = bloom_view;
    }

    /* Determine render target */
    Qs_GpuImageView *out_view;
    uint32_t out_w, out_h;

    bool is_terminal = (ctx->swapchain_view != NULL);
    if (is_terminal) {
        out_view = ctx->swapchain_view;
        out_w    = ctx->swapchain_width;
        out_h    = ctx->swapchain_height;
    } else {
        if (!s->ok || !s->ldr_view) return;
        /* Transition ldr_image to colour attachment */
        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=s->ldr_image, .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});
        out_view = s->ldr_view;
        out_w    = ctx->width;
        out_h    = ctx->height;
    }

    /* Bloom strength from post-process settings (or zero if no bloom) */
    const Qs_PostProcessSettings *pp = qs_renderer_post_process(ctx->renderer);
    float bloom_str = (pp && bloom_tex.view) ? pp->bloom_strength : 0.0f;

    /* Render */
    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=out_view,.depth=NULL,
        .clear_color={0,0,0,0},
        .width=out_w,.height=out_h});
    qs_cmd_set_viewport(ctx->cmd, out_w, out_h);
    qs_cmd_bind_pipeline(ctx->cmd, s->pipeline);
    if (s->desc_set)
        qs_cmd_bind_descriptor_set(ctx->cmd, s->layout, 0, s->desc_set);
    typedef struct { float inv_w, inv_h, bloom_str, pad; } TonemapPC;
    TonemapPC tpc = {1.0f/out_w, 1.0f/out_h, bloom_str, 0};
    qs_cmd_push_constants(ctx->cmd, s->layout, QS_GPU_SHADER_FRAGMENT, 0, 16, &tpc);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);

    if (!is_terminal) {
        /* Transition ldr_image back to shader-read for downstream nodes */
        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=s->ldr_image, .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

        ctx->outputs[0].texture = (Qs_RgTexture){ s->ldr_image, s->ldr_view };
    }
}

/* ================================================================
   PORT DECLARATIONS
   ================================================================ */

static const Qs_RgPort k_tonemap_inputs[] = {
    { .name = "hdr_color", .kind = QS_RG_TEXTURE, .optional = false },
    { .name = "bloom_tex", .kind = QS_RG_TEXTURE, .optional = true  },
};

static const Qs_RgPort k_tonemap_outputs[] = {
    { .name = "ldr_color", .kind = QS_RG_TEXTURE },
};

/* ================================================================
   PUBLIC NODE TYPE
   ================================================================ */

const Qs_RgNodeType qs_rg_tonemap_node_type = {
    .name         = "tonemap",
    .inputs       = k_tonemap_inputs,
    .input_count  = 2,
    .outputs      = k_tonemap_outputs,
    .output_count = 1,
    .create       = tonemap_create,
    .destroy      = tonemap_destroy,
    .execute      = tonemap_execute,
    .on_resize    = tonemap_on_resize,
};
