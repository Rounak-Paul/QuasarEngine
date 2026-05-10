/*
 * sky_node.c  —  Procedural gradient sky render graph node.
 *
 * Ports
 *   Inputs:  none
 *   Outputs:
 *     [0] color_out  TEXTURE  — sky colour (RGBA16F, viewport-sized)
 *
 * The sky is a simple vertical gradient from a configurable zenith colour
 * to a horizon colour, driven by the camera's view direction extracted from
 * the FrameUBO (binding 0, set 0).  The gradient is computed in world-space
 * by reconstructing the view ray through each fragment.
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

static const char *SKY_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform FrameUBO {\n"
    "    mat4  view; mat4  proj; mat4  inv_view_proj;\n"
    "    vec3  cam_pos; float time;\n"
    "    float screen_width; float screen_height; uint debug_flags; float _pad;\n"
    "} frame;\n"
    "layout(push_constant) uniform PC {\n"
    "    vec4 zenith_color;\n"
    "    vec4 horizon_color;\n"
    "    float inv_w; float inv_h; float horizon_falloff; float _p1;\n"
    "} pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy * vec2(pc.inv_w, pc.inv_h);\n"
    "    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);\n"
    "    vec4 world = frame.inv_view_proj * clip;\n"
    "    vec3 dir   = normalize(world.xyz / world.w - frame.cam_pos);\n"
    "    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);\n"
    "    out_color = mix(pc.horizon_color, pc.zenith_color, pow(t, pc.horizon_falloff));\n"
    "}\n";

/* ================================================================
   SKY PARAMETERS  (mutable, edited from the properties panel)
   ================================================================ */

typedef struct {
    float zenith[3];   /* RGB zenith colour   */
    float horizon[3];  /* RGB horizon colour  */
    float falloff;     /* gradient exponent — lower = more zenith coverage */
} SkyParams;

/* Defaults tuned for a rich, clearly-blue sky.
 * zenith: deep blue, horizon: light but unmistakably blue. */
static SkyParams s_sky_params = {
    .zenith  = {0.04f, 0.18f, 0.60f},
    .horizon = {0.35f, 0.60f, 0.95f},
    .falloff = 1.50f,
};

/* ================================================================
   PARAM TABLE  (consumed by the editor props panel)
   ================================================================ */

const Qs_RgNodeParam sky_node_params[] = {
    { "Zenith R",  0.0f, 1.0f, 0.04f },
    { "Zenith G",  0.0f, 1.0f, 0.18f },
    { "Zenith B",  0.0f, 1.0f, 0.60f },
    { "Horizon R", 0.0f, 1.0f, 0.35f },
    { "Horizon G", 0.0f, 1.0f, 0.60f },
    { "Horizon B", 0.0f, 1.0f, 0.95f },
    { "Falloff",   0.5f, 5.0f, 1.50f },
};
#define SKY_PARAM_COUNT 7

/* ================================================================
   NODE STATE
   ================================================================ */

typedef struct {
    Qs_GpuContext *gpu;

    Qs_GpuImage     *sky_image;
    Qs_GpuImageView *sky_view;

    Qs_GpuPipeline            *pipeline;
    Qs_GpuPipelineLayout      *layout;
    Qs_GpuDescriptorSetLayout *set_layout;
    Qs_GpuDescriptorPool      *pool;
    Qs_GpuDescriptorSet       *desc_set;

    bool desc_written; /* true after first frame when FrameUBO is bound */
    bool ok;
} SkyNodeState;

/* Push constant layout must match SKY_FRAG */
typedef struct {
    float zenith_color[4];    /* 16 bytes */
    float horizon_color[4];   /* 16 bytes */
    float inv_w, inv_h, horizon_falloff, _p1; /* 16 bytes */
} SkyPC; /* 48 bytes */

/* ================================================================
   NODE VTABLE
   ================================================================ */

static void *sky_create(Qs_Engine *engine, Qs_GpuContext *gpu)
{
    (void)engine;
    SkyNodeState *s = qs_calloc(1, sizeof(SkyNodeState), QS_MEM_RENDER);
    if (!s) return NULL;
    s->gpu = gpu;

    /* Descriptor set layout: 1 uniform buffer (FrameUBO) */
    {
        Qs_GpuDescriptorBinding b = {0, QS_GPU_DESCRIPTOR_UNIFORM_BUFFER, 1,
                                      QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT};
        s->set_layout = qs_gpu_create_descriptor_set_layout(gpu, &b, 1);
        if (!s->set_layout) { QS_LOG_ERROR("SkyNode: set layout failed"); return s; }
    }

    /* Pipeline layout */
    {
        Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_FRAGMENT | QS_GPU_SHADER_VERTEX, 0, sizeof(SkyPC)};
        Qs_GpuDescriptorSetLayout *sets[] = {s->set_layout};
        s->layout = qs_gpu_create_pipeline_layout(gpu,
            &(Qs_GpuPipelineLayoutDesc){sets, 1, &pc, 1});
        if (!s->layout) { QS_LOG_ERROR("SkyNode: pipeline layout failed"); return s; }
    }

    /* Pipeline — RGBA16F, no depth */
    {
        Qs_GpuShader *fv = qs_gpu_compile_shader(gpu, FULLSCREEN_VERT, QS_GPU_SHADER_VERTEX);
        Qs_GpuShader *ff = qs_gpu_compile_shader(gpu, SKY_FRAG, QS_GPU_SHADER_FRAGMENT);
        if (!fv || !ff) {
            if (fv) qs_gpu_destroy_shader(gpu, fv);
            if (ff) qs_gpu_destroy_shader(gpu, ff);
            QS_LOG_ERROR("SkyNode: shader compilation failed");
            return s;
        }
        s->pipeline = qs_gpu_create_graphics_pipeline(gpu,
            &(Qs_GpuGraphicsPipelineDesc){
                s->layout, fv, ff, NULL, 0,
                QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_NONE, false, false,
                QS_GPU_FORMAT_RGBA16_SFLOAT, QS_GPU_FORMAT_DEPTH_AUTO});
        qs_gpu_destroy_shader(gpu, fv);
        qs_gpu_destroy_shader(gpu, ff);
        if (!s->pipeline) { QS_LOG_ERROR("SkyNode: pipeline creation failed"); return s; }
    }

    /* Descriptor pool + set */
    {
        Qs_GpuDescriptorPoolSize ps = {QS_GPU_DESCRIPTOR_UNIFORM_BUFFER, 2};
        s->pool = qs_gpu_create_descriptor_pool(gpu,
            &(Qs_GpuDescriptorPoolDesc){&ps, 1, 2});
        if (!s->pool) { QS_LOG_ERROR("SkyNode: pool failed"); return s; }
        s->desc_set = qs_gpu_alloc_descriptor_set(gpu, s->pool, s->set_layout);
        if (!s->desc_set) { QS_LOG_ERROR("SkyNode: desc set alloc failed"); return s; }
    }

    QS_LOG_INFO("SkyNode: created");
    return s;
}

static void sky_destroy(void *state, Qs_GpuContext *gpu)
{
    SkyNodeState *s = state;
    if (!s) return;
    if (s->desc_set)   qs_gpu_free_descriptor_set(gpu, s->pool, s->desc_set);
    if (s->pool)       qs_gpu_destroy_descriptor_pool(gpu, s->pool);
    if (s->pipeline)   qs_gpu_destroy_pipeline(gpu, s->pipeline);
    if (s->layout)     qs_gpu_destroy_pipeline_layout(gpu, s->layout);
    if (s->set_layout) qs_gpu_destroy_descriptor_set_layout(gpu, s->set_layout);
    if (s->sky_view)   qs_gpu_destroy_image_view(gpu, s->sky_view);
    if (s->sky_image)  qs_gpu_destroy_image(gpu, s->sky_image);
    qs_free(s);
}

static void sky_on_resize(void *state, Qs_GpuContext *gpu, uint32_t w, uint32_t h)
{
    SkyNodeState *s = state;
    if (!s) return;

    if (s->sky_view)  { qs_gpu_destroy_image_view(gpu, s->sky_view);  s->sky_view  = NULL; }
    if (s->sky_image) { qs_gpu_destroy_image(gpu, s->sky_image);      s->sky_image = NULL; }

    s->sky_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width=w, .height=h, .mip_levels=1,
        .format=QS_GPU_FORMAT_RGBA16_SFLOAT,
        .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT | QS_GPU_IMAGE_SAMPLED,
        .sample_count=1});
    if (!s->sky_image) { QS_LOG_ERROR("SkyNode: image creation failed"); return; }
    s->sky_view = qs_gpu_create_image_view_for(gpu, s->sky_image, QS_GPU_IMAGE_ASPECT_COLOR);

    s->desc_written = false; /* re-bind FrameUBO after resize */
    s->ok = (s->sky_view != NULL);
}

static void sky_execute(void *state, const Qs_RgExecCtx *ctx)
{
    SkyNodeState *s = state;
    if (!s || !s->ok || !s->sky_image || !s->sky_view) return;

    /* Bind FrameUBO on first frame (or after resize) */
    if (!s->desc_written) {
        Qs_GpuBuffer *frame_ubo = qs_renderer_get_frame_ubo(ctx->renderer);
        if (!frame_ubo) goto emit;
        qs_gpu_write_buffer_descriptor(ctx->gpu, s->desc_set, 0, frame_ubo, 0, 0);
        s->desc_written = true;
    }

    /* Transition sky image to colour attachment */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->sky_image, .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

    SkyPC pc = {
        .zenith_color    = {s_sky_params.zenith[0],  s_sky_params.zenith[1],
                            s_sky_params.zenith[2],  1.0f},
        .horizon_color   = {s_sky_params.horizon[0], s_sky_params.horizon[1],
                            s_sky_params.horizon[2], 1.0f},
        .inv_w           = 1.0f / (float)ctx->width,
        .inv_h           = 1.0f / (float)ctx->height,
        .horizon_falloff = s_sky_params.falloff,
    };

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=s->sky_view, .depth=NULL,
        .clear_color={0,0,0,0},
        .width=ctx->width, .height=ctx->height});
    qs_cmd_set_viewport(ctx->cmd, ctx->width, ctx->height);
    qs_cmd_bind_pipeline(ctx->cmd, s->pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, s->layout, 0, s->desc_set);
    qs_cmd_push_constants(ctx->cmd, s->layout,
        QS_GPU_SHADER_FRAGMENT | QS_GPU_SHADER_VERTEX, 0, sizeof(SkyPC), &pc);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);

    /* Transition sky image back to shader-read */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->sky_image, .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

emit:
    ctx->outputs[0].texture = (Qs_RgTexture){ s->sky_image, s->sky_view };
}

/* ================================================================
   EDITOR PARAMETER CALLBACKS
   ================================================================ */

float sky_get_param(Qs_Engine *engine, uint32_t idx)
{
    (void)engine;
    switch (idx) {
        case 0: return s_sky_params.zenith[0];
        case 1: return s_sky_params.zenith[1];
        case 2: return s_sky_params.zenith[2];
        case 3: return s_sky_params.horizon[0];
        case 4: return s_sky_params.horizon[1];
        case 5: return s_sky_params.horizon[2];
        case 6: return s_sky_params.falloff;
        default: return 0.0f;
    }
}

void sky_set_param(Qs_Engine *engine, uint32_t idx, float val)
{
    (void)engine;
    switch (idx) {
        case 0: s_sky_params.zenith[0]  = val; break;
        case 1: s_sky_params.zenith[1]  = val; break;
        case 2: s_sky_params.zenith[2]  = val; break;
        case 3: s_sky_params.horizon[0] = val; break;
        case 4: s_sky_params.horizon[1] = val; break;
        case 5: s_sky_params.horizon[2] = val; break;
        case 6: s_sky_params.falloff    = val; break;
        default: break;
    }
}

/* ================================================================
   PORT DECLARATIONS + PUBLIC TYPE
   ================================================================ */

static const Qs_RgPort k_sky_outputs[] = {
    { .name = "color_out", .kind = QS_RG_TEXTURE },
};

const Qs_RgNodeType sky_node_type = {
    .name         = "sky",
    .inputs       = NULL,
    .input_count  = 0,
    .outputs      = k_sky_outputs,
    .output_count = 1,
    .create       = sky_create,
    .destroy      = sky_destroy,
    .execute      = sky_execute,
    .on_resize    = sky_on_resize,
};
