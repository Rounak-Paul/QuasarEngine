#include "qs_forward.h"
#include "qs_renderer.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_texture.h"
#include "qs_scene.h"
#include "qs_light.h"
#include "qs_log.h"
#include "causality.h"

#include <string.h>
#include <math.h>

/* ================================================================
   GLSL — vertex shader
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
   GLSL — fragment shader (simple directional + ambient)
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
    VkDevice              device;
    VkPipeline            pipeline;
    VkPipelineLayout      pipeline_layout;
    VkDescriptorSetLayout desc_layout;     /* borrowed from material system */
    Qs_Material          *default_material;
    Qs_Renderer          *renderer;
    Qs_RenderNode        *node;
} ForwardState;

static ForwardState g_fwd;

/* ================================================================
   PUSH-CONSTANT DATA
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

static void mat4_rotate_y(float out[16], float rad)
{
    mat4_identity(out);
    float c = cosf(rad), s = sinf(rad);
    out[0] = c;  out[2] = s;
    out[8] = -s; out[10] = c;
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
    /* T * R * S  (rotation from quaternion) */
    float tx[16], rx[16], sx[16], tmp[16];
    mat4_translate(tx, t->position[0], t->position[1], t->position[2]);
    mat4_scale(sx, t->scale[0], t->scale[1], t->scale[2]);

    /* Quaternion to rotation matrix */
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

    vkCmdBindPipeline(ctx->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fwd->pipeline);

    /* VP matrix */
    float vp[16];
    mat4_mul(vp, ctx->proj, ctx->view);

    /* Iterate all entities with MeshComp */
    Qs_ComponentType *mesh_type = qs_mesh_comp_type();
    Qs_ComponentType *xfm_type  = qs_transform_type();
    if (!mesh_type || !xfm_type) return;

    for (Qs_Entity e = qs_scene_first(scene, mesh_type);
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, mesh_type, e))
    {
        if (!qs_entity_enabled(scene, e)) continue;

        Qs_MeshComp  *mc = (Qs_MeshComp *)qs_entity_get(scene, e, mesh_type);
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, xfm_type);
        if (!mc || !mc->mesh || !mc->visible || !tf) continue;

        /* Resolve material early (needed for push constants + descriptor bind) */
        Qs_Material *mat = mc->material ? mc->material : fwd->default_material;

        /* Build model matrix from transform */
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

        vkCmdPushConstants(ctx->cmd, fwd->pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &pc);
        VkDescriptorSet ds = mat ? qs_material_descriptor_set(mat) : VK_NULL_HANDLE;
        if (!ds) continue;
        vkCmdBindDescriptorSets(ctx->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                fwd->pipeline_layout, 0, 1, &ds, 0, NULL);

        qs_mesh_draw(mc->mesh, ctx->cmd);
    }
}

/* ================================================================
   PIPELINE CREATION
   ================================================================ */

static bool create_pipeline(VkDevice device, VkFormat color_format, VkFormat depth_format)
{
    /* Compile shaders */
    VkShaderModule vert_mod = ca_shader_compile(device, FORWARD_VERT,
                                                VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderModule frag_mod = ca_shader_compile(device, FORWARD_FRAG,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);
    if (!vert_mod || !frag_mod) {
        if (vert_mod) vkDestroyShaderModule(device, vert_mod, NULL);
        if (frag_mod) vkDestroyShaderModule(device, frag_mod, NULL);
        return false;
    }

    /* Descriptor set layout — use the material system's shared layout
       so we can directly bind material descriptor sets. */
    g_fwd.desc_layout = qs_material_set_layout();
    if (!g_fwd.desc_layout) {
        QS_LOG_ERROR("Forward pipeline: material set layout not available");
        vkDestroyShaderModule(device, vert_mod, NULL);
        vkDestroyShaderModule(device, frag_mod, NULL);
        return false;
    }

    /* Pipeline layout — push constants + one descriptor set */
    VkPushConstantRange pc_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(PushConstants),
    };
    VkPipelineLayoutCreateInfo layout_ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &g_fwd.desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pc_range,
    };
    if (vkCreatePipelineLayout(device, &layout_ci, NULL, &g_fwd.pipeline_layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device, g_fwd.desc_layout, NULL);
        vkDestroyShaderModule(device, vert_mod, NULL);
        vkDestroyShaderModule(device, frag_mod, NULL);
        return false;
    }

    /* Shader stages */
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_mod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_mod, .pName = "main" },
    };

    /* Vertex input matching Qs_Vertex layout */
    VkVertexInputBindingDescription binding = {
        .binding   = 0,
        .stride    = sizeof(Qs_Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(Qs_Vertex, position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(Qs_Vertex, normal) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
          .offset = offsetof(Qs_Vertex, tangent) },
        { .location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
          .offset = offsetof(Qs_Vertex, uv) },
    };
    VkPipelineVertexInputStateCreateInfo vert_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &binding,
        .vertexAttributeDescriptionCount = 4,
        .pVertexAttributeDescriptions    = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dyn_states,
    };

    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode    = VK_CULL_MODE_BACK_BIT,
        .frontFace   = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth   = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_att = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_att,
    };

    VkPipelineRenderingCreateInfo rendering_ci = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat   = depth_format,
    };

    VkGraphicsPipelineCreateInfo pipeline_ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &rendering_ci,
        .stageCount          = 2,
        .pStages             = stages,
        .pVertexInputState   = &vert_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState   = &multisample,
        .pDepthStencilState  = &depth_stencil,
        .pColorBlendState    = &color_blend,
        .pDynamicState       = &dynamic_state,
        .layout              = g_fwd.pipeline_layout,
        .renderPass          = VK_NULL_HANDLE,
    };

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE,
                                                 1, &pipeline_ci, NULL,
                                                 &g_fwd.pipeline);

    vkDestroyShaderModule(device, vert_mod, NULL);
    vkDestroyShaderModule(device, frag_mod, NULL);

    if (result != VK_SUCCESS) {
        QS_LOG_ERROR("Forward pipeline creation failed: %d", result);
        vkDestroyPipelineLayout(device, g_fwd.pipeline_layout, NULL);
        vkDestroyDescriptorSetLayout(device, g_fwd.desc_layout, NULL);
        g_fwd.pipeline_layout = VK_NULL_HANDLE;
        g_fwd.desc_layout     = VK_NULL_HANDLE;
        return false;
    }

    QS_LOG_INFO("Forward rendering pipeline created");
    return true;
}

/* ================================================================
   PUBLIC API
   ================================================================ */

bool qs_forward_init(Qs_Engine *engine, Qs_Renderer *renderer)
{
    (void)engine;
    if (!renderer) return false;

    memset(&g_fwd, 0, sizeof(g_fwd));

    VkDevice device = qs_renderer_device(renderer);
    g_fwd.device   = device;
    g_fwd.renderer = renderer;

    /* Color format: B8G8R8A8_UNORM (matches causality viewport) */
    VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    if (!create_pipeline(device, color_format, depth_format)) return false;

    /* Create a default material (white, uses fallback textures from material system) */
    g_fwd.default_material = qs_material_create(engine, &(Qs_MaterialDesc){
        .name              = "_fwd_default",
        .base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f },
        .metallic_factor   = 0.0f,
        .roughness_factor  = 1.0f,
    });

    /* Add as render node to the renderer */
    g_fwd.node = qs_renderer_add_node(renderer, &(Qs_RenderNodeDesc){
        .name     = "forward",
        .priority = 100,
        .execute  = forward_execute,
    });
    if (!g_fwd.node) {
        QS_LOG_ERROR("Failed to add forward render node");
        qs_forward_shutdown();
        return false;
    }

    QS_LOG_INFO("Forward renderer initialized");
    return true;
}

void qs_forward_shutdown(void)
{
    VkDevice dev = g_fwd.device;
    if (!dev) return;

    vkDeviceWaitIdle(dev);

    if (g_fwd.node && g_fwd.renderer)
        qs_renderer_remove_node(g_fwd.renderer, g_fwd.node);

    if (g_fwd.pipeline)
        vkDestroyPipeline(dev, g_fwd.pipeline, NULL);
    if (g_fwd.pipeline_layout)
        vkDestroyPipelineLayout(dev, g_fwd.pipeline_layout, NULL);

    /* desc_layout is borrowed from the material system — not ours to destroy */

    memset(&g_fwd, 0, sizeof(g_fwd));
    QS_LOG_INFO("Forward renderer shut down");
}
