#include "ed_pick.h"
#include "qs_math.h"
#include "qs_renderer.h"
#include "qs_scene.h"
#include "qs_log.h"
#include "qs_mesh.h"

#include <stddef.h>
#include <string.h>

/* ================================================================
   GLSL shaders — minimal transform + flat entity-ID output
   ================================================================ */

static const char *PICK_VERT =
    "#version 450\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(push_constant) uniform PC {\n"
    "    mat4 mvp;\n"
    "    uint entity_id;\n"
    "} pc;\n"
    "void main() {\n"
    "    gl_Position = pc.mvp * vec4(a_position, 1.0);\n"
    "}\n";

static const char *PICK_FRAG =
    "#version 450\n"
    "layout(push_constant) uniform PC {\n"
    "    mat4 mvp;\n"
    "    uint entity_id;\n"
    "} pc;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    uint id = pc.entity_id;\n"
    "    out_color = vec4(\n"
    "        float( id        & 0xFFu) / 255.0,\n"
    "        float((id >>  8u) & 0xFFu) / 255.0,\n"
    "        float((id >> 16u) & 0xFFu) / 255.0,\n"
    "        1.0);\n"
    "}\n";

/* ================================================================
   Module state
   ================================================================ */

static Qs_Engine             *s_engine;
static Qs_GpuPipeline        *s_pipeline;
static Qs_GpuPipelineLayout  *s_layout;
static Qs_RenderNode         *s_node;

/* Pick-pass colour image (RGBA8, viewport-sized) */
static Qs_GpuImage     *s_pick_image;
static Qs_GpuImageView *s_pick_view;
static uint32_t         s_pick_w;
static uint32_t         s_pick_h;

/* Own depth image so we don't interfere with the scene depth */
static Qs_GpuImage     *s_depth_image;
static Qs_GpuImageView *s_depth_view;

/* HOST_VISIBLE readback buffer (4 bytes = 1 pixel of RGBA8) */
static Qs_GpuBuffer *s_readback_buf;

/* ================================================================
   Image management
   ================================================================ */

static void destroy_images(Qs_GpuContext *gpu)
{
    if (s_pick_view)  { qs_gpu_destroy_image_view(gpu, s_pick_view);  s_pick_view  = NULL; }
    if (s_pick_image) { qs_gpu_destroy_image(gpu, s_pick_image);      s_pick_image = NULL; }
    if (s_depth_view) { qs_gpu_destroy_image_view(gpu, s_depth_view); s_depth_view = NULL; }
    if (s_depth_image){ qs_gpu_destroy_image(gpu, s_depth_image);     s_depth_image = NULL;}
}

static bool create_images(Qs_GpuContext *gpu, uint32_t w, uint32_t h)
{
    destroy_images(gpu);

    s_pick_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width  = w, .height = h, .mip_levels = 1,
        .format = QS_GPU_FORMAT_RGBA8_UNORM,
        .usage  = QS_GPU_IMAGE_COLOR_ATTACHMENT | QS_GPU_IMAGE_TRANSFER_SRC,
    });
    if (!s_pick_image) goto fail;

    s_pick_view = qs_gpu_create_image_view_for(gpu, s_pick_image,
                                                QS_GPU_IMAGE_ASPECT_COLOR);
    if (!s_pick_view) goto fail;

    s_depth_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width  = w, .height = h, .mip_levels = 1,
        .format = QS_GPU_FORMAT_D32_SFLOAT,
        .usage  = QS_GPU_IMAGE_DEPTH_ATTACHMENT,
    });
    if (!s_depth_image) goto fail;

    s_depth_view = qs_gpu_create_image_view_for(gpu, s_depth_image,
                                                 QS_GPU_IMAGE_ASPECT_DEPTH);
    if (!s_depth_view) goto fail;

    /* Transition pick image to COLOR_ATTACHMENT for first use */
    Qs_GpuCmd *cmd = qs_gpu_begin_transfer(gpu);
    qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
        .image = s_pick_image,
        .old_layout = QS_GPU_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect = QS_GPU_IMAGE_ASPECT_COLOR, .base_mip = 0, .mip_count = 1,
    });
    qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
        .image = s_depth_image,
        .old_layout = QS_GPU_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
        .aspect = QS_GPU_IMAGE_ASPECT_DEPTH, .base_mip = 0, .mip_count = 1,
    });
    qs_gpu_end_transfer(gpu, cmd);

    s_pick_w = w;
    s_pick_h = h;
    return true;
fail:
    destroy_images(gpu);
    return false;
}

/* ================================================================
   Render callback — draws all renderables with flat entity-ID colour
   ================================================================ */

typedef struct {
    float    mvp[16];
    uint32_t entity_id;
    uint32_t _pad[3];
} PickPC;

static void pick_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    (void)user_data;
    if (!s_pipeline) return;

    /* Create or resize images as needed */
    if (!s_pick_view || !s_depth_view ||
        ctx->width != s_pick_w || ctx->height != s_pick_h) {
        if (!create_images(qs_engine_gpu(s_engine), ctx->width, ctx->height))
            return;
    }

    /* Clear to entity INVALID: R=0xFF, G=0xFF, B=0xFF → 0x00FFFFFF
       Alpha = 0 marks "no entity" */
    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color       = s_pick_view,
        .depth       = s_depth_view,
        .clear_color = { 1.0f, 1.0f, 1.0f, 0.0f },
        .clear_depth = 1.0f,
        .width       = ctx->width,
        .height      = ctx->height,
    });

    qs_cmd_set_viewport(ctx->cmd, ctx->width, ctx->height);
    qs_cmd_bind_pipeline(ctx->cmd, s_pipeline);

    /* Compute VP matrix */
    float vp[16];
    qs_m4_mul(ctx->proj, ctx->view, vp);

    for (uint32_t i = 0; i < ctx->renderable_count; i++) {
        const Qs_Renderable *ren = &ctx->renderables[i];
        if (!ren->vertex_buffer) continue;

        float mvp[16];
        qs_m4_mul(vp, ren->transform, mvp);

        PickPC pc;
        memcpy(pc.mvp, mvp, 64);
        pc.entity_id = ren->entity;
        pc._pad[0] = pc._pad[1] = pc._pad[2] = 0;

        qs_cmd_push_constants(ctx->cmd, s_layout,
                              QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
                              0, sizeof(PickPC), &pc);

        qs_cmd_bind_vertex_buffer(ctx->cmd, 0, ren->vertex_buffer, 0);
        if (ren->index_buffer) {
            qs_cmd_bind_index_buffer(ctx->cmd, ren->index_buffer, ren->index_16bit);
            qs_cmd_draw_indexed(ctx->cmd, ren->index_count, 0, 0);
        } else {
            qs_cmd_draw(ctx->cmd, ren->vertex_count, 0);
        }
    }

    qs_cmd_end_rendering(ctx->cmd);
}

/* ================================================================
   Public API
   ================================================================ */

void ed_pick_init(Qs_Engine *engine)
{
    s_engine = engine;
    Qs_GpuContext *gpu = qs_engine_gpu(engine);

    Qs_GpuShader *vs = qs_gpu_compile_shader(gpu, PICK_VERT, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs = qs_gpu_compile_shader(gpu, PICK_FRAG, QS_GPU_SHADER_FRAGMENT);
    if (!vs || !fs) {
        QS_LOG_ERROR("Pick pass: shader compilation failed");
        if (vs) qs_gpu_destroy_shader(gpu, vs);
        if (fs) qs_gpu_destroy_shader(gpu, fs);
        return;
    }

    Qs_GpuPushConstantRange pc = {
        .stages = QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
        .offset = 0,
        .size   = sizeof(PickPC),
    };
    s_layout = qs_gpu_create_pipeline_layout(gpu, &(Qs_GpuPipelineLayoutDesc){
        .push_constants      = &pc,
        .push_constant_count = 1,
    });

    Qs_GpuVertexAttribute attr = {
        .location = 0,
        .format   = QS_GPU_VERTEX_FORMAT_FLOAT3,
        .offset   = offsetof(Qs_Vertex, position),
    };
    Qs_GpuVertexBinding vb = {
        .binding         = 0,
        .stride          = sizeof(Qs_Vertex),
        .attributes      = &attr,
        .attribute_count = 1,
    };

    s_pipeline = qs_gpu_create_graphics_pipeline(gpu, &(Qs_GpuGraphicsPipelineDesc){
        .layout               = s_layout,
        .vertex_shader        = vs,
        .fragment_shader      = fs,
        .vertex_bindings      = &vb,
        .vertex_binding_count = 1,
        .topology             = QS_GPU_TOPOLOGY_TRIANGLES,
        .cull_mode            = QS_GPU_CULL_BACK,
        .depth_test           = true,
        .depth_write          = true,
        .color_format         = QS_GPU_FORMAT_RGBA8_UNORM,
        .depth_format         = QS_GPU_FORMAT_D32_SFLOAT,
    });

    qs_gpu_destroy_shader(gpu, vs);
    qs_gpu_destroy_shader(gpu, fs);

    if (!s_pipeline) {
        QS_LOG_ERROR("Pick pass: pipeline creation failed");
        qs_gpu_destroy_pipeline_layout(gpu, s_layout);
        s_layout = NULL;
        return;
    }

    /* Readback buffer — 4 bytes for a single RGBA8 pixel */
    s_readback_buf = qs_gpu_create_buffer(gpu, &(Qs_GpuBufferDesc){
        .size   = 4,
        .usage  = QS_GPU_BUFFER_TRANSFER,
        .memory = QS_GPU_MEMORY_HOST_VISIBLE,
    });
}

void ed_pick_shutdown(Qs_Engine *engine)
{
    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    destroy_images(gpu);
    if (s_readback_buf) { qs_gpu_destroy_buffer(gpu, s_readback_buf);          s_readback_buf = NULL; }
    if (s_pipeline)     { qs_gpu_destroy_pipeline(gpu, s_pipeline);            s_pipeline     = NULL; }
    if (s_layout)       { qs_gpu_destroy_pipeline_layout(gpu, s_layout);       s_layout       = NULL; }
    s_node   = NULL;
    s_engine = NULL;
}

void ed_pick_attach(Qs_Renderer *renderer)
{
    if (!renderer || !s_pipeline) return;
    s_node = qs_renderer_add_node(renderer, &(Qs_RenderNodeDesc){
        .name     = "editor_pick",
        .priority = 50,
        .execute  = pick_pass_execute,
    });
}

Qs_Entity ed_pick_entity_at(Qs_Engine *engine, float norm_x, float norm_y)
{
    if (!s_pick_image || !s_readback_buf) return QS_ENTITY_INVALID;
    if (norm_x < 0.0f || norm_x >= 1.0f || norm_y < 0.0f || norm_y >= 1.0f)
        return QS_ENTITY_INVALID;

    uint32_t vp_x = (uint32_t)(norm_x * s_pick_w);
    uint32_t vp_y = (uint32_t)(norm_y * s_pick_h);
    if (vp_x >= s_pick_w) vp_x = s_pick_w - 1;
    if (vp_y >= s_pick_h) vp_y = s_pick_h - 1;

    Qs_GpuContext *gpu = qs_engine_gpu(engine);

    /* Synchronous transfer: transition to TRANSFER_SRC, copy the single
       pixel, transition back to COLOR_ATTACHMENT for next frame's render. */
    Qs_GpuCmd *cmd = qs_gpu_begin_transfer(gpu);
    qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
        .image = s_pick_image,
        .old_layout = QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout = QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC,
        .aspect = QS_GPU_IMAGE_ASPECT_COLOR, .base_mip = 0, .mip_count = 1,
    });
    qs_cmd_copy_image_to_buffer(cmd, s_pick_image, s_readback_buf,
                                 vp_x, vp_y, 1, 1);
    qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
        .image = s_pick_image,
        .old_layout = QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC,
        .new_layout = QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect = QS_GPU_IMAGE_ASPECT_COLOR, .base_mip = 0, .mip_count = 1,
    });
    qs_gpu_end_transfer(gpu, cmd);

    /* Read back the pixel */
    uint8_t *pixel = (uint8_t *)qs_gpu_map_buffer(gpu, s_readback_buf);
    if (!pixel) return QS_ENTITY_INVALID;

    uint8_t r = pixel[0];
    uint8_t g = pixel[1];
    uint8_t b = pixel[2];
    uint8_t a = pixel[3];
    qs_gpu_unmap_buffer(gpu, s_readback_buf);

    /* Alpha == 0 means the clear colour = no entity */
    if (a == 0) return QS_ENTITY_INVALID;

    uint32_t entity_id = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    return (Qs_Entity)entity_id;
}
