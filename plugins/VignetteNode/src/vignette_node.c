/*
 * vignette_node.c  —  Radial vignette render graph node.
 *
 * Ports
 *   Inputs:
 *     [0] color_in  TEXTURE            — LDR scene colour (from tonemap or previous chain)
 *     [1] strength  PARAM_FLOAT (opt)  — override vignette strength [0,1]
 *   Outputs:
 *     [0] color_out  TEXTURE           — vignette-applied BGRA8 image
 *
 * When this node is the terminal node (ctx->swapchain_view != NULL) it renders
 * directly into the swapchain image rather than the intermediate output texture.
 */

#include "quasar.h"

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

static const char *VIGNETTE_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_src;\n"
    "layout(push_constant) uniform PC { float inv_w; float inv_h; float strength; float _p; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 uv  = gl_FragCoord.xy * vec2(pc.inv_w, pc.inv_h);\n"
    "    vec4 col = texture(u_src, uv);\n"
    "    /* Radial vignette: dark at corners, bright at centre */\n"
    "    vec2 vu  = uv * (1.0 - uv.yx);\n"
    "    float v  = vu.x * vu.y * 15.0;\n"
    "    float vig = pow(clamp(v, 0.0, 1.0), pc.strength);\n"
    "    out_color = vec4(col.rgb * vig, col.a);\n"
    "}\n";

/* ================================================================
   NODE STATE
   ================================================================ */

typedef struct {
    Qs_GpuContext *gpu;

    /* Intermediate output (BGRA8, used when non-terminal) */
    Qs_GpuImage     *out_image;
    Qs_GpuImageView *out_view;

    Qs_GpuPipeline            *pipeline;
    Qs_GpuPipelineLayout      *layout;
    Qs_GpuDescriptorSetLayout *set_layout;
    Qs_GpuDescriptorPool      *pool;
    Qs_GpuDescriptorSet       *desc_set;
    Qs_GpuSampler             *sampler;

    Qs_GpuImageView *bound_view; /* currently bound source view */
    bool ok;
} VignetteNodeState;

typedef struct { float inv_w, inv_h, strength, pad; } VignettePC; /* 16 bytes */

/* ================================================================
   NODE VTABLE
   ================================================================ */

static void *vignette_create(Qs_Engine *engine, Qs_GpuContext *gpu)
{
    (void)engine;
    VignetteNodeState *s = qs_calloc(1, sizeof(VignetteNodeState), QS_MEM_RENDER);
    if (!s) return NULL;
    s->gpu = gpu;

    /* Descriptor set layout: 1 combined image sampler */
    {
        Qs_GpuDescriptorBinding b = {0, QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
                                      1, QS_GPU_SHADER_FRAGMENT};
        s->set_layout = qs_gpu_create_descriptor_set_layout(gpu, &b, 1);
        if (!s->set_layout) { QS_LOG_ERROR("VignetteNode: set layout failed"); return s; }
    }

    /* Pipeline layout */
    {
        Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_FRAGMENT, 0, sizeof(VignettePC)};
        Qs_GpuDescriptorSetLayout *sets[] = {s->set_layout};
        s->layout = qs_gpu_create_pipeline_layout(gpu,
            &(Qs_GpuPipelineLayoutDesc){sets, 1, &pc, 1});
        if (!s->layout) { QS_LOG_ERROR("VignetteNode: pipeline layout failed"); return s; }
    }

    /* Pipeline — BGRA8 (matches swapchain and intermediate output) */
    {
        Qs_GpuShader *fv = qs_gpu_compile_shader(gpu, FULLSCREEN_VERT, QS_GPU_SHADER_VERTEX);
        Qs_GpuShader *ff = qs_gpu_compile_shader(gpu, VIGNETTE_FRAG,   QS_GPU_SHADER_FRAGMENT);
        if (!fv || !ff) {
            if (fv) qs_gpu_destroy_shader(gpu, fv);
            if (ff) qs_gpu_destroy_shader(gpu, ff);
            QS_LOG_ERROR("VignetteNode: shader compilation failed");
            return s;
        }
        s->pipeline = qs_gpu_create_graphics_pipeline(gpu,
            &(Qs_GpuGraphicsPipelineDesc){
                s->layout, fv, ff, NULL, 0,
                QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_NONE, false, false,
                QS_GPU_FORMAT_BGRA8_UNORM, QS_GPU_FORMAT_DEPTH_AUTO});
        qs_gpu_destroy_shader(gpu, fv);
        qs_gpu_destroy_shader(gpu, ff);
        if (!s->pipeline) { QS_LOG_ERROR("VignetteNode: pipeline creation failed"); return s; }
    }

    /* Sampler */
    s->sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR, .mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE, .wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,
        .mip_levels=1});
    if (!s->sampler) { QS_LOG_ERROR("VignetteNode: sampler failed"); return s; }

    /* Descriptor pool + set */
    {
        Qs_GpuDescriptorPoolSize ps = {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 2};
        s->pool = qs_gpu_create_descriptor_pool(gpu,
            &(Qs_GpuDescriptorPoolDesc){&ps, 1, 2});
        if (!s->pool) { QS_LOG_ERROR("VignetteNode: pool failed"); return s; }
        s->desc_set = qs_gpu_alloc_descriptor_set(gpu, s->pool, s->set_layout);
        if (!s->desc_set) { QS_LOG_ERROR("VignetteNode: desc set alloc failed"); return s; }
    }

    QS_LOG_INFO("VignetteNode: created");
    return s;
}

static void vignette_destroy(void *state, Qs_GpuContext *gpu)
{
    VignetteNodeState *s = state;
    if (!s) return;
    if (s->desc_set)   qs_gpu_free_descriptor_set(gpu, s->pool, s->desc_set);
    if (s->pool)       qs_gpu_destroy_descriptor_pool(gpu, s->pool);
    if (s->sampler)    qs_gpu_destroy_sampler(gpu, s->sampler);
    if (s->pipeline)   qs_gpu_destroy_pipeline(gpu, s->pipeline);
    if (s->layout)     qs_gpu_destroy_pipeline_layout(gpu, s->layout);
    if (s->set_layout) qs_gpu_destroy_descriptor_set_layout(gpu, s->set_layout);
    if (s->out_view)   qs_gpu_destroy_image_view(gpu, s->out_view);
    if (s->out_image)  qs_gpu_destroy_image(gpu, s->out_image);
    qs_free(s);
}

static void vignette_on_resize(void *state, Qs_GpuContext *gpu, uint32_t w, uint32_t h)
{
    VignetteNodeState *s = state;
    if (!s) return;

    if (s->out_view)  { qs_gpu_destroy_image_view(gpu, s->out_view);  s->out_view  = NULL; }
    if (s->out_image) { qs_gpu_destroy_image(gpu, s->out_image);      s->out_image = NULL; }

    /* BGRA8 output — same format as swapchain, pipeline-compatible */
    s->out_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width=w, .height=h, .mip_levels=1,
        .format=QS_GPU_FORMAT_BGRA8_UNORM,
        .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT | QS_GPU_IMAGE_SAMPLED,
        .sample_count=1});
    if (!s->out_image) { QS_LOG_ERROR("VignetteNode: output image failed"); return; }
    s->out_view = qs_gpu_create_image_view_for(gpu, s->out_image, QS_GPU_IMAGE_ASPECT_COLOR);

    s->bound_view = NULL;
    s->ok = (s->out_view != NULL);
}

static void vignette_execute(void *state, const Qs_RgExecCtx *ctx)
{
    VignetteNodeState *s = state;
    if (!s) return;

    Qs_RgTexture color_in = ctx->inputs[0].texture;
    if (!color_in.view) return;

    /* Update descriptor when source changes */
    if (color_in.view != s->bound_view) {
        if (s->desc_set && s->sampler)
            qs_gpu_write_image_descriptor(ctx->gpu, s->desc_set, 0,
                                          s->sampler, color_in.view);
        s->bound_view = color_in.view;
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
        if (!s->ok || !s->out_view) return;
        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=s->out_image, .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});
        out_view = s->out_view;
        out_w    = ctx->width;
        out_h    = ctx->height;
    }

    /* Vignette strength: port override or post-process setting */
    float strength = ctx->inputs[1].f32; /* 0 if unconnected */
    if (strength == 0.0f) {
        const Qs_PostProcessSettings *pp = qs_renderer_post_process(ctx->renderer);
        strength = pp ? pp->vignette_strength : 0.4f;
    }

    VignettePC pc = {1.0f / (float)out_w, 1.0f / (float)out_h, strength, 0};

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=out_view, .depth=NULL,
        .clear_color={0,0,0,0},
        .width=out_w, .height=out_h});
    qs_cmd_set_viewport(ctx->cmd, out_w, out_h);
    qs_cmd_bind_pipeline(ctx->cmd, s->pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, s->layout, 0, s->desc_set);
    qs_cmd_push_constants(ctx->cmd, s->layout, QS_GPU_SHADER_FRAGMENT,
                          0, sizeof(VignettePC), &pc);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);

    if (!is_terminal) {
        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=s->out_image, .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});
        ctx->outputs[0].texture = (Qs_RgTexture){ s->out_image, s->out_view };
    }
}

/* ================================================================
   PORT DECLARATIONS + PUBLIC TYPE
   ================================================================ */

static const Qs_RgPort k_vignette_inputs[] = {
    { .name = "color_in", .kind = QS_RG_TEXTURE,    .optional = false },
    { .name = "strength", .kind = QS_RG_PARAM_FLOAT, .optional = true  },
};

static const Qs_RgPort k_vignette_outputs[] = {
    { .name = "color_out", .kind = QS_RG_TEXTURE },
};

const Qs_RgNodeType vignette_node_type = {
    .name         = "vignette",
    .inputs       = k_vignette_inputs,
    .input_count  = 2,
    .outputs      = k_vignette_outputs,
    .output_count = 1,
    .create       = vignette_create,
    .destroy      = vignette_destroy,
    .execute      = vignette_execute,
    .on_resize    = vignette_on_resize,
};
