/*
 * bloom_node.c  —  Kawase bloom render graph node.
 *
 * Ports
 *   Inputs:
 *     [0] color_in  TEXTURE            — HDR scene colour (from forward_pbr)
 *     [1] strength  PARAM_FLOAT (opt)  — override bloom strength [0,1]
 *   Outputs:
 *     [0] bloom_tex  TEXTURE           — bloom contribution (half-res, RGBA16F)
 *
 * Algorithm: 2-pass Kawase filter.
 *   Pass 1 (downsample): threshold + half-res downsample of color_in
 *   Pass 2 (upsample):   Kawase tent upsample back to half-res
 *
 * The bloom_tex output is consumed by the tonemap node (binding 1) and
 * blended with a strength factor during tone mapping.
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

/* Downsample: extract bright pixels + 4-tap box filter at half resolution */
static const char *BLOOM_DOWN_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_src;\n"
    "layout(push_constant) uniform PC { vec2 inv_half; float threshold; float _p; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "float lum(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy * pc.inv_half;\n"   /* inv_half = 1/(half-res) = 2/full-res */
    "    vec2 h  = pc.inv_half * 0.5;\n"               /* 0.5/half-res = 1/full-res = 1 source texel */
    "    vec3 s  = texture(u_src, uv+vec2(-h.x,-h.y)).rgb\n"
    "            + texture(u_src, uv+vec2( h.x,-h.y)).rgb\n"
    "            + texture(u_src, uv+vec2(-h.x, h.y)).rgb\n"
    "            + texture(u_src, uv+vec2( h.x, h.y)).rgb;\n"
    "    s *= 0.25;\n"
    "    float l=lum(s); float w=max(l-pc.threshold,0.0)/(l+0.001);\n"
    "    out_color=vec4(s*w,1.0);\n"
    "}\n";

/* Upsample: Kawase tent — 8-tap gather at half-res */
static const char *BLOOM_UP_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_src;\n"
    "layout(push_constant) uniform PC { vec2 inv_src; float _p0; float _p1; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy * pc.inv_src;\n"
    "    vec2 h  = pc.inv_src;\n"
    "    vec3 s = vec3(0.0);\n"
    "    s += texture(u_src, uv+vec2(-h.x*2.0, 0.0)).rgb * 1.0;\n"
    "    s += texture(u_src, uv+vec2(-h.x,     h.y)).rgb * 2.0;\n"
    "    s += texture(u_src, uv+vec2( 0.0,  h.y*2.0)).rgb * 1.0;\n"
    "    s += texture(u_src, uv+vec2( h.x,     h.y)).rgb * 2.0;\n"
    "    s += texture(u_src, uv+vec2( h.x*2.0, 0.0)).rgb * 1.0;\n"
    "    s += texture(u_src, uv+vec2( h.x,    -h.y)).rgb * 2.0;\n"
    "    s += texture(u_src, uv+vec2( 0.0, -h.y*2.0)).rgb * 1.0;\n"
    "    s += texture(u_src, uv+vec2(-h.x,    -h.y)).rgb * 2.0;\n"
    "    out_color=vec4(s*(1.0/12.0),1.0);\n"
    "}\n";

/* ================================================================
   NODE STATE
   ================================================================ */

typedef struct {
    Qs_GpuContext *gpu;

    /* Half-resolution bloom images */
    Qs_GpuImage     *bloom_images[2];
    Qs_GpuImageView *bloom_views[2];
    uint32_t         bloom_w, bloom_h; /* current half-res dimensions */

    /* Downsample pass */
    Qs_GpuPipeline            *down_pipeline;
    Qs_GpuPipelineLayout      *down_layout;
    Qs_GpuDescriptorSetLayout *set_layout; /* shared: 1 combined image sampler */
    Qs_GpuDescriptorPool      *pool;
    Qs_GpuDescriptorSet       *desc_sets[2]; /* [0]=down, [1]=up */
    Qs_GpuSampler             *sampler;

    /* Upsample pass */
    Qs_GpuPipeline *up_pipeline;

    /* Track which view is bound to each desc set */
    Qs_GpuImageView *bound_down_view; /* what's in desc_sets[0] */

    bool ok;
} BloomNodeState;

/* Push constant: shared layout for both passes */
typedef struct { float inv_w, inv_h, param0, param1; } BloomPC; /* 16 bytes */

/* ================================================================
   NODE VTABLE
   ================================================================ */

static void *bloom_create(Qs_Engine *engine, Qs_GpuContext *gpu)
{
    (void)engine;
    BloomNodeState *s = qs_calloc(1, sizeof(BloomNodeState), QS_MEM_RENDER);
    if (!s) return NULL;
    s->gpu = gpu;

    /* Shared set layout: 1 combined image sampler */
    {
        Qs_GpuDescriptorBinding b = {0, QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
                                      1, QS_GPU_SHADER_FRAGMENT};
        s->set_layout = qs_gpu_create_descriptor_set_layout(gpu, &b, 1);
        if (!s->set_layout) { QS_LOG_ERROR("BloomNode: set layout failed"); return s; }
    }

    /* Sampler */
    s->sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR, .mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE, .wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,
        .mip_levels=1});
    if (!s->sampler) { QS_LOG_ERROR("BloomNode: sampler failed"); return s; }

    /* Pipeline layout */
    Qs_GpuPipelineLayout *layout;
    {
        Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_FRAGMENT, 0, sizeof(BloomPC)};
        Qs_GpuDescriptorSetLayout *sets[] = {s->set_layout};
        layout = qs_gpu_create_pipeline_layout(gpu,
            &(Qs_GpuPipelineLayoutDesc){sets, 1, &pc, 1});
        if (!layout) { QS_LOG_ERROR("BloomNode: pipeline layout failed"); return s; }
    }
    /* Reuse same layout for both passes */
    s->down_layout = layout;

    /* Shaders + pipelines */
    {
        Qs_GpuShader *fv = qs_gpu_compile_shader(gpu, FULLSCREEN_VERT, QS_GPU_SHADER_VERTEX);
        if (!fv) { QS_LOG_ERROR("BloomNode: fullscreen vert shader failed"); return s; }

        Qs_GpuShader *fd = qs_gpu_compile_shader(gpu, BLOOM_DOWN_FRAG, QS_GPU_SHADER_FRAGMENT);
        Qs_GpuShader *fu = qs_gpu_compile_shader(gpu, BLOOM_UP_FRAG,   QS_GPU_SHADER_FRAGMENT);
        if (fd) {
            s->down_pipeline = qs_gpu_create_graphics_pipeline(gpu,
                &(Qs_GpuGraphicsPipelineDesc){
                    layout, fv, fd, NULL, 0,
                    QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_NONE, false, false,
                    QS_GPU_FORMAT_RGBA16_SFLOAT, QS_GPU_FORMAT_DEPTH_AUTO});
            qs_gpu_destroy_shader(gpu, fd);
        }
        if (fu) {
            s->up_pipeline = qs_gpu_create_graphics_pipeline(gpu,
                &(Qs_GpuGraphicsPipelineDesc){
                    layout, fv, fu, NULL, 0,
                    QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_NONE, false, false,
                    QS_GPU_FORMAT_RGBA16_SFLOAT, QS_GPU_FORMAT_DEPTH_AUTO});
            qs_gpu_destroy_shader(gpu, fu);
        }
        qs_gpu_destroy_shader(gpu, fv);
        if (!s->down_pipeline || !s->up_pipeline) {
            QS_LOG_ERROR("BloomNode: pipeline creation failed");
            return s;
        }
    }

    /* Descriptor pool + 2 sets */
    {
        Qs_GpuDescriptorPoolSize ps = {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 4};
        s->pool = qs_gpu_create_descriptor_pool(gpu,
            &(Qs_GpuDescriptorPoolDesc){&ps, 1, 4});
        if (!s->pool) { QS_LOG_ERROR("BloomNode: pool failed"); return s; }
        s->desc_sets[0] = qs_gpu_alloc_descriptor_set(gpu, s->pool, s->set_layout);
        s->desc_sets[1] = qs_gpu_alloc_descriptor_set(gpu, s->pool, s->set_layout);
        if (!s->desc_sets[0] || !s->desc_sets[1]) {
            QS_LOG_ERROR("BloomNode: desc set alloc failed");
            return s;
        }
    }

    QS_LOG_INFO("BloomNode: created");
    return s;
}

static void bloom_destroy(void *state, Qs_GpuContext *gpu)
{
    BloomNodeState *s = state;
    if (!s) return;
    if (s->desc_sets[0]) qs_gpu_free_descriptor_set(gpu, s->pool, s->desc_sets[0]);
    if (s->desc_sets[1]) qs_gpu_free_descriptor_set(gpu, s->pool, s->desc_sets[1]);
    if (s->pool)         qs_gpu_destroy_descriptor_pool(gpu, s->pool);
    if (s->sampler)      qs_gpu_destroy_sampler(gpu, s->sampler);
    if (s->down_pipeline)qs_gpu_destroy_pipeline(gpu, s->down_pipeline);
    if (s->up_pipeline)  qs_gpu_destroy_pipeline(gpu, s->up_pipeline);
    if (s->down_layout)  qs_gpu_destroy_pipeline_layout(gpu, s->down_layout);
    if (s->set_layout)   qs_gpu_destroy_descriptor_set_layout(gpu, s->set_layout);
    for (int i = 0; i < 2; i++) {
        if (s->bloom_views[i])  qs_gpu_destroy_image_view(gpu, s->bloom_views[i]);
        if (s->bloom_images[i]) qs_gpu_destroy_image(gpu, s->bloom_images[i]);
    }
    qs_free(s);
}

static void bloom_on_resize(void *state, Qs_GpuContext *gpu, uint32_t w, uint32_t h)
{
    BloomNodeState *s = state;
    if (!s) return;

    for (int i = 0; i < 2; i++) {
        if (s->bloom_views[i])  { qs_gpu_destroy_image_view(gpu, s->bloom_views[i]);  s->bloom_views[i]  = NULL; }
        if (s->bloom_images[i]) { qs_gpu_destroy_image(gpu, s->bloom_images[i]);      s->bloom_images[i] = NULL; }
    }

    s->bloom_w = (w > 1) ? (w / 2) : 1;
    s->bloom_h = (h > 1) ? (h / 2) : 1;

    for (int i = 0; i < 2; i++) {
        s->bloom_images[i] = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
            .width=s->bloom_w, .height=s->bloom_h, .mip_levels=1,
            .format=QS_GPU_FORMAT_RGBA16_SFLOAT,
            .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT | QS_GPU_IMAGE_SAMPLED,
            .sample_count=1});
        if (s->bloom_images[i])
            s->bloom_views[i] = qs_gpu_create_image_view_for(
                gpu, s->bloom_images[i], QS_GPU_IMAGE_ASPECT_COLOR);
    }

    s->bound_down_view = NULL;

    /* Write the up-pass descriptor set permanently: it always samples bloom[0] */
    if (s->desc_sets[1] && s->bloom_views[0] && s->sampler)
        qs_gpu_write_image_descriptor(gpu, s->desc_sets[1], 0, s->sampler, s->bloom_views[0]);

    s->ok = (s->bloom_views[0] && s->bloom_views[1]);
}

static void bloom_execute(void *state, const Qs_RgExecCtx *ctx)
{
    BloomNodeState *s = state;
    if (!s || !s->ok) return;

    Qs_RgTexture color_in = ctx->inputs[0].texture;
    if (!color_in.view) return;

    /* Re-bind downsample source if it changed */
    if (color_in.view != s->bound_down_view && s->desc_sets[0] && s->sampler) {
        qs_gpu_write_image_descriptor(ctx->gpu, s->desc_sets[0], 0,
                                      s->sampler, color_in.view);
        s->bound_down_view = color_in.view;
    }

    /* Luminance threshold: only pixels brighter than this contribute to bloom.
       Controlled via Qs_PostProcessSettings.bloom_threshold (default 0.4).
       bloom_strength in the tonemap node controls the blend weight separately. */
    const Qs_PostProcessSettings *pp = qs_renderer_post_process(ctx->renderer);
    float threshold = pp ? pp->bloom_threshold : 0.4f;

    /* --- Pass 1: downsample colour → bloom[0] --- */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->bloom_images[0], .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=s->bloom_views[0], .depth=NULL,
        .clear_color={0,0,0,0},
        .width=s->bloom_w, .height=s->bloom_h});
    qs_cmd_set_viewport(ctx->cmd, s->bloom_w, s->bloom_h);
    qs_cmd_bind_pipeline(ctx->cmd, s->down_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, s->down_layout, 0, s->desc_sets[0]);
    /* inv_half = 1/bloom_res = 2/full_res: maps half-res fragcoord to full [0,1] UV */
    BloomPC pc_down = {1.0f/(float)s->bloom_w, 1.0f/(float)s->bloom_h, threshold, 0};
    qs_cmd_push_constants(ctx->cmd, s->down_layout, QS_GPU_SHADER_FRAGMENT,
                          0, sizeof(BloomPC), &pc_down);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);

    /* Transition bloom[0] to shader read for the upsample pass */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->bloom_images[0], .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

    /* --- Pass 2: upsample bloom[0] → bloom[1] --- */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->bloom_images[1], .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=s->bloom_views[1], .depth=NULL,
        .clear_color={0,0,0,0},
        .width=s->bloom_w, .height=s->bloom_h});
    qs_cmd_set_viewport(ctx->cmd, s->bloom_w, s->bloom_h);
    qs_cmd_bind_pipeline(ctx->cmd, s->up_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, s->down_layout, 0, s->desc_sets[1]);
    BloomPC pc_up = {1.0f/(float)s->bloom_w, 1.0f/(float)s->bloom_h, 0, 0};
    qs_cmd_push_constants(ctx->cmd, s->down_layout, QS_GPU_SHADER_FRAGMENT,
                          0, sizeof(BloomPC), &pc_up);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);

    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->bloom_images[1], .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

    ctx->outputs[0].texture = (Qs_RgTexture){ s->bloom_images[1], s->bloom_views[1] };
}

/* ================================================================
   PORT DECLARATIONS + PUBLIC TYPE
   ================================================================ */

static const Qs_RgPort k_bloom_inputs[] = {
    { .name = "color_in", .kind = QS_RG_TEXTURE,     .optional = false },
    { .name = "strength", .kind = QS_RG_PARAM_FLOAT,  .optional = true  },
};

static const Qs_RgPort k_bloom_outputs[] = {
    { .name = "bloom_tex", .kind = QS_RG_TEXTURE },
};

const Qs_RgNodeType bloom_node_type = {
    .name         = "bloom",
    .inputs       = k_bloom_inputs,
    .input_count  = 2,
    .outputs      = k_bloom_outputs,
    .output_count = 1,
    .create       = bloom_create,
    .destroy      = bloom_destroy,
    .execute      = bloom_execute,
    .on_resize    = bloom_on_resize,
};
