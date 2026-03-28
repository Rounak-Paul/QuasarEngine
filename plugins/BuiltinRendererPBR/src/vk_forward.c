#include "qs_renderer.h"
#include "qs_material.h"
#include "qs_mesh.h"
#include "qs_scene.h"
#include "qs_light.h"
#include "qs_log.h"

#include <string.h>
#include <math.h>
#include <stddef.h>

/* ================================================================
   GLSL -- vertex shader
   ================================================================ */

static const char *FORWARD_VERT =
    "#version 450\n"
    "\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec3 a_normal;\n"
    "layout(location = 2) in vec4 a_tangent;\n"
    "layout(location = 3) in vec2 a_uv;\n"
    "\n"
    "layout(push_constant) uniform PC {\n"
    "    mat4 mvp;\n"
    "    mat4 model;\n"
    "    vec4 base_color;\n"
    "} pc;\n"
    "\n"
    "layout(location = 0) out vec3 v_world_pos;\n"
    "layout(location = 1) out vec3 v_normal;\n"
    "layout(location = 2) out vec2 v_uv;\n"
    "\n"
    "void main() {\n"
    "    vec4 world = pc.model * vec4(a_position, 1.0);\n"
    "    v_world_pos = world.xyz;\n"
    "    v_normal = mat3(pc.model) * a_normal;\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = pc.mvp * vec4(a_position, 1.0);\n"
    "}\n";

/* ================================================================
   GLSL -- fragment shader (directional + ambient)
   ================================================================ */

static const char *FORWARD_FRAG =
    "#version 450\n"
    "\n"
    "layout(location = 0) in vec3 v_world_pos;\n"
    "layout(location = 1) in vec3 v_normal;\n"
    "layout(location = 2) in vec2 v_uv;\n"
    "\n"
    "layout(push_constant) uniform PC {\n"
    "    mat4 mvp;\n"
    "    mat4 model;\n"
    "    vec4 base_color;\n"
    "} pc;\n"
    "\n"
    "layout(set = 0, binding = 0) uniform sampler2D u_base_color;\n"
    "\n"
    "layout(location = 0) out vec4 out_color;\n"
    "\n"
    "void main() {\n"
    "    vec3 N = normalize(v_normal);\n"
    "    vec3 light_dir = normalize(vec3(0.4, 0.8, 0.6));\n"
    "    float NdotL = max(dot(N, light_dir), 0.0);\n"
    "\n"
    "    vec3 tex_color = texture(u_base_color, v_uv).rgb;\n"
    "    vec3 albedo = tex_color * pc.base_color.rgb;\n"
    "    vec3 ambient = 0.15 * albedo;\n"
    "    vec3 diffuse = NdotL * albedo * vec3(1.0, 0.98, 0.95);\n"
    "\n"
    "    out_color = vec4(ambient + diffuse, 1.0);\n"
    "}\n";

/* ================================================================
   PIPELINE STATE
   ================================================================ */

typedef struct ForwardState {
    Qs_GpuContext             *gpu;
    Qs_GpuPipeline            *pipeline;
    Qs_GpuPipelineLayout      *pipeline_layout;
    Qs_GpuDescriptorSetLayout *desc_layout;
    Qs_Material               *default_material;
    Qs_Renderer               *renderer;
    Qs_RenderNode             *node;
} ForwardState;

static ForwardState g_fwd;

/* ================================================================
   PUSH CONSTANTS
   ================================================================ */

typedef struct PushConstants {
    float mvp[16];
    float model[16];
    float base_color[4];
} PushConstants;

/* ================================================================
   MATH HELPERS
   ================================================================ */

static void mat4_identity(float m[16])
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_translate(float out[16], float x, float y, float z)
{
    mat4_identity(out);
    out[12] = x; out[13] = y; out[14] = z;
}

static void mat4_scale(float out[16], float sx, float sy, float sz)
{
    memset(out, 0, 64);
    out[0] = sx; out[5] = sy; out[10] = sz; out[15] = 1.0f;
}

static void mat4_mul(float out[16], const float a[16], const float b[16])
{
    float tmp[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a[r + k*4] * b[k + c*4];
            tmp[r + c*4] = sum;
        }
    memcpy(out, tmp, 64);
}

static void build_model_matrix(const Qs_Transform *t, float out[16])
{
    float tx[16], rx[16], sx[16], tmp[16];
    mat4_translate(tx, t->position[0], t->position[1], t->position[2]);
    mat4_scale(sx, t->scale[0], t->scale[1], t->scale[2]);

    float qx = t->rotation[0], qy = t->rotation[1];
    float qz = t->rotation[2], qw = t->rotation[3];
    float xx = qx*qx, yy = qy*qy, zz = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;

    mat4_identity(rx);
    rx[0]  = 1.0f - 2.0f*(yy + zz);
    rx[1]  = 2.0f*(xy + wz);
    rx[2]  = 2.0f*(xz - wy);
    rx[4]  = 2.0f*(xy - wz);
    rx[5]  = 1.0f - 2.0f*(xx + zz);
    rx[6]  = 2.0f*(yz + wx);
    rx[8]  = 2.0f*(xz + wy);
    rx[9]  = 2.0f*(yz - wx);
    rx[10] = 1.0f - 2.0f*(xx + yy);

    mat4_mul(tmp, tx, rx);
    mat4_mul(out, tmp, sx);
}

/* ================================================================
   RENDER NODE CALLBACK
   ================================================================ */

static void forward_execute(const Qs_RenderContext *ctx, void *user_data)
{
    (void)user_data;
    ForwardState *fwd = &g_fwd;

    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;

    qs_cmd_bind_pipeline(ctx->cmd, fwd->pipeline);

    float vp[16];
    mat4_mul(vp, ctx->proj, ctx->view);

    Qs_ComponentType *mesh_type = qs_mesh_comp_type();
    Qs_ComponentType *xfm_type  = qs_transform_type();
    if (!mesh_type || !xfm_type) return;

    for (Qs_Entity e = qs_scene_first(scene, mesh_type);
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, mesh_type, e))
    {
        if (!qs_entity_enabled(scene, e)) continue;

        Qs_MeshComp  *mc = (Qs_MeshComp  *)qs_entity_get(scene, e, mesh_type);
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, xfm_type);
        if (!mc || !mc->mesh || !mc->visible || !tf) continue;

        Qs_Material *mat = mc->material ? mc->material : fwd->default_material;

        float model[16];
        build_model_matrix(tf, model);

        PushConstants pc;
        mat4_mul(pc.mvp, vp, model);
        memcpy(pc.model, model, 64);

        const Qs_PBRParams *pbr = qs_material_params(mat);
        if (pbr) {
            memcpy(pc.base_color, pbr->base_color_factor, 16);
        } else {
            pc.base_color[0] = pc.base_color[1] = pc.base_color[2] = pc.base_color[3] = 1.0f;
        }

        qs_cmd_push_constants(ctx->cmd, fwd->pipeline_layout,
                              QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
                              0, sizeof(PushConstants), &pc);

        Qs_GpuDescriptorSet *ds = mat ? qs_material_descriptor_set(mat) : NULL;
        if (!ds) continue;
        qs_cmd_bind_descriptor_set(ctx->cmd, fwd->pipeline_layout, 0, ds);

        qs_mesh_bind(mc->mesh, ctx->cmd);
        qs_mesh_draw(mc->mesh, ctx->cmd);
    }
}

/* ================================================================
   PIPELINE CREATION
   ================================================================ */

static bool create_pipeline(Qs_GpuContext *gpu, Qs_GpuImageFormat color_format,
                             Qs_GpuImageFormat depth_format)
{
    Qs_GpuShader *vert = qs_gpu_compile_shader(gpu, FORWARD_VERT, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *frag = qs_gpu_compile_shader(gpu, FORWARD_FRAG, QS_GPU_SHADER_FRAGMENT);
    if (!vert || !frag) {
        if (vert) qs_gpu_destroy_shader(gpu, vert);
        if (frag) qs_gpu_destroy_shader(gpu, frag);
        QS_LOG_ERROR("Forward pipeline: shader compilation failed");
        return false;
    }

    /* Borrow descriptor set layout from the material system */
    g_fwd.desc_layout = qs_material_set_layout();
    if (!g_fwd.desc_layout) {
        QS_LOG_ERROR("Forward pipeline: material set layout not available");
        qs_gpu_destroy_shader(gpu, vert);
        qs_gpu_destroy_shader(gpu, frag);
        return false;
    }

    Qs_GpuPushConstantRange pc_range = {
        .stages = QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
        .offset = 0,
        .size   = sizeof(PushConstants),
    };
    Qs_GpuDescriptorSetLayout *set_layouts[] = { g_fwd.desc_layout };
    Qs_GpuPipelineLayoutDesc layout_desc = {
        .set_layouts         = set_layouts,
        .set_layout_count    = 1,
        .push_constants      = &pc_range,
        .push_constant_count = 1,
    };
    g_fwd.pipeline_layout = qs_gpu_create_pipeline_layout(gpu, &layout_desc);
    if (!g_fwd.pipeline_layout) {
        qs_gpu_destroy_shader(gpu, vert);
        qs_gpu_destroy_shader(gpu, frag);
        return false;
    }

    Qs_GpuVertexAttribute attrs[] = {
        { .location = 0, .format = QS_GPU_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(Qs_Vertex, position) },
        { .location = 1, .format = QS_GPU_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(Qs_Vertex, normal) },
        { .location = 2, .format = QS_GPU_VERTEX_FORMAT_FLOAT4,
          .offset = offsetof(Qs_Vertex, tangent) },
        { .location = 3, .format = QS_GPU_VERTEX_FORMAT_FLOAT2,
          .offset = offsetof(Qs_Vertex, uv) },
    };
    Qs_GpuVertexBinding vertex_binding = {
        .binding         = 0,
        .stride          = sizeof(Qs_Vertex),
        .attributes      = attrs,
        .attribute_count = 4,
    };
    Qs_GpuGraphicsPipelineDesc pipeline_desc = {
        .layout               = g_fwd.pipeline_layout,
        .vertex_shader        = vert,
        .fragment_shader      = frag,
        .vertex_bindings      = &vertex_binding,
        .vertex_binding_count = 1,
        .topology             = QS_GPU_TOPOLOGY_TRIANGLES,
        .cull_mode            = QS_GPU_CULL_BACK,
        .depth_test           = true,
        .depth_write          = true,
        .color_format         = color_format,
        .depth_format         = depth_format,
    };
    g_fwd.pipeline = qs_gpu_create_graphics_pipeline(gpu, &pipeline_desc);

    qs_gpu_destroy_shader(gpu, vert);
    qs_gpu_destroy_shader(gpu, frag);

    if (!g_fwd.pipeline) {
        QS_LOG_ERROR("Forward pipeline creation failed");
        qs_gpu_destroy_pipeline_layout(gpu, g_fwd.pipeline_layout);
        g_fwd.pipeline_layout = NULL;
        return false;
    }

    QS_LOG_INFO("VkForward: pipeline created");
    return true;
}

/* ================================================================
   PUBLIC IMPL (called via backend vtable)
   ================================================================ */

static void fwd_shutdown_internal(void)
{
    if (!g_fwd.gpu) return;

    if (g_fwd.node && g_fwd.renderer)
        qs_renderer_remove_node(g_fwd.renderer, g_fwd.node);

    if (g_fwd.default_material)
        qs_material_destroy(g_fwd.default_material);

    if (g_fwd.pipeline)
        qs_gpu_destroy_pipeline(g_fwd.gpu, g_fwd.pipeline);
    if (g_fwd.pipeline_layout)
        qs_gpu_destroy_pipeline_layout(g_fwd.gpu, g_fwd.pipeline_layout);

    /* desc_layout is borrowed from the material system -- do not destroy */

    memset(&g_fwd, 0, sizeof(g_fwd));
    QS_LOG_INFO("VkForward: shut down");
}

void vk_forward_attach(Qs_Engine *engine, Qs_Renderer *renderer)
{
    if (!renderer) return;

    memset(&g_fwd, 0, sizeof(g_fwd));
    g_fwd.gpu      = qs_engine_gpu(engine);
    g_fwd.renderer = renderer;

    if (!create_pipeline(g_fwd.gpu,
                         QS_GPU_FORMAT_BGRA8_UNORM,
                         QS_GPU_FORMAT_D32_SFLOAT)) {
        memset(&g_fwd, 0, sizeof(g_fwd));
        return;
    }

    /* Default white material (fallback textures provided by material backend) */
    g_fwd.default_material = qs_material_create(engine, &(Qs_MaterialDesc){
        .name              = "_fwd_default",
        .base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f },
        .metallic_factor   = 0.0f,
        .roughness_factor  = 1.0f,
    });

    g_fwd.node = qs_renderer_add_node(renderer, &(Qs_RenderNodeDesc){
        .name     = "forward",
        .priority = 100,
        .execute  = forward_execute,
    });
    if (!g_fwd.node) {
        QS_LOG_ERROR("VkForward: failed to add render node");
        fwd_shutdown_internal();
        return;
    }

    QS_LOG_INFO("VkForward: attached");
}

void vk_forward_detach(Qs_Renderer *renderer)
{
    if (renderer && g_fwd.renderer != renderer) return;
    fwd_shutdown_internal();
}
