/*
 * rg_shadow_node.c  —  Cascaded Shadow Map (CSM) fixed render graph node.
 *
 * Ports
 *   Outputs:
 *     [0] shadow_map_0  TEXTURE  — cascade 0 depth map
 *     [1] shadow_map_1  TEXTURE  — cascade 1 depth map
 *     [2] shadow_map_2  TEXTURE  — cascade 2 depth map
 *     [3] shadow_ubo    BUFFER   — ShadowUBO with cascade VP matrices + splits
 */

#include "qs_render_graph.h"
#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_math.h"
#include "qs_light.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_memory.h"
#include "qs_log.h"

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ================================================================
   CONSTANTS
   ================================================================ */

#define SHADOW_CASCADE_COUNT 3
#define SHADOW_MAP_SIZE      2048

/* ================================================================
   GPU TYPES  (must match std140 in shaders)
   ================================================================ */

typedef struct {
    float cascade_vp[SHADOW_CASCADE_COUNT][16];
    float cascade_splits[SHADOW_CASCADE_COUNT];
    float _pad;
} ShadowUBO;

typedef struct { float model[16]; int32_t cascade_idx; int32_t _p[3]; } ShadowPC;

/* ================================================================
   GLSL SHADERS
   ================================================================ */

static const char *SHADOW_VERT =
    "#version 450\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(push_constant) uniform PC { mat4 model; int cascade_idx; int _p[3]; } pc;\n"
    "layout(set = 0, binding = 2) uniform ShadowUBO {\n"
    "    mat4  cascade_vp[3];\n"
    "    float cascade_splits[3];\n"
    "    float _pad;\n"
    "} shadow;\n"
    "void main() {\n"
    "    gl_Position = shadow.cascade_vp[pc.cascade_idx] * pc.model * vec4(a_position, 1.0);\n"
    "}\n";

static const char *SHADOW_FRAG = "#version 450\nvoid main() {}\n";

/* ================================================================
   NODE STATE
   ================================================================ */

typedef struct {
    Qs_GpuContext *gpu;

    /* Shadow map images (one per cascade) */
    Qs_GpuImage     *shadow_images[SHADOW_CASCADE_COUNT];
    Qs_GpuImageView *shadow_views[SHADOW_CASCADE_COUNT]; /* sample views */
    Qs_GpuImageView *shadow_rt_views[SHADOW_CASCADE_COUNT]; /* render-target views */

    /* Shadow UBO (written every frame with CSM matrices) */
    Qs_GpuBuffer *shadow_ubo;

    /* Pipeline */
    Qs_GpuDescriptorSetLayout *frame_set_layout;
    Qs_GpuPipelineLayout      *shadow_pipeline_layout;
    Qs_GpuPipeline            *shadow_pipeline;
    Qs_GpuDescriptorPool      *desc_pool;
    Qs_GpuDescriptorSet       *frame_desc_set;

    /* CSM computation cache */
    float shadow_matrices[SHADOW_CASCADE_COUNT][16];
    float shadow_splits[SHADOW_CASCADE_COUNT + 1];

    bool desc_written;
    bool ok;
} ShadowNodeState;

/* ================================================================
   CSM MATH
   ================================================================ */

static void compute_csm(const Qs_Camera *cam,
                         const Qs_LightGPU *lights, uint32_t light_count,
                         float shadow_matrices[SHADOW_CASCADE_COUNT][16],
                         float shadow_splits[SHADOW_CASCADE_COUNT + 1])
{
    float light_dir[3] = { 0.4f, -1.0f, 0.3f };
    for (uint32_t i = 0; i < light_count; i++) {
        if (lights[i].type == (uint32_t)QS_LIGHT_DIRECTIONAL) {
            light_dir[0] = lights[i].direction[0];
            light_dir[1] = lights[i].direction[1];
            light_dir[2] = lights[i].direction[2];
            break;
        }
    }
    float ld = sqrtf(light_dir[0]*light_dir[0]+light_dir[1]*light_dir[1]+light_dir[2]*light_dir[2]);
    if (ld > 1e-6f) { light_dir[0]/=ld; light_dir[1]/=ld; light_dir[2]/=ld; }

    float near_p = cam->near_plane > 0.0f ? cam->near_plane : 0.1f;
    float far_p  = cam->far_plane  > 0.0f ? cam->far_plane  : 500.0f;

    shadow_splits[0] = near_p;
    for (int i = 1; i <= SHADOW_CASCADE_COUNT; i++) {
        float fi = (float)i / (float)SHADOW_CASCADE_COUNT;
        float lg = near_p * powf(far_p / near_p, fi);
        float ln = near_p + (far_p - near_p) * fi;
        shadow_splits[i] = 0.75f * lg + 0.25f * ln;
    }

    float l_fwd[3] = { light_dir[0], light_dir[1], light_dir[2] };
    float l_up[3]  = { 0.0f, 1.0f, 0.0f };
    if (fabsf(l_fwd[1]) > 0.99f) { l_up[0] = 1.0f; l_up[1] = 0.0f; l_up[2] = 0.0f; }
    float l_right[3] = {
        l_fwd[1]*l_up[2]-l_fwd[2]*l_up[1],
        l_fwd[2]*l_up[0]-l_fwd[0]*l_up[2],
        l_fwd[0]*l_up[1]-l_fwd[1]*l_up[0]
    };
    float ll = sqrtf(l_right[0]*l_right[0]+l_right[1]*l_right[1]+l_right[2]*l_right[2]);
    if (ll > 1e-6f) { l_right[0]/=ll; l_right[1]/=ll; l_right[2]/=ll; }
    float l_up2[3] = {
        l_right[1]*l_fwd[2]-l_right[2]*l_fwd[1],
        l_right[2]*l_fwd[0]-l_right[0]*l_fwd[2],
        l_right[0]*l_fwd[1]-l_right[1]*l_fwd[0]
    };

    for (int c = 0; c < SHADOW_CASCADE_COUNT; c++) {
        float near_c = shadow_splits[c], far_c = shadow_splits[c+1];
        float radius = (far_c - near_c) * 0.6f + 2.0f;
        float cx = cam->target[0], cy = cam->target[1], cz = cam->target[2];
        float texel = 2.0f * radius / (float)SHADOW_MAP_SIZE;
        float lc_r = cx*l_right[0] + cy*l_right[1] + cz*l_right[2];
        float lc_u = cx*l_up2[0]   + cy*l_up2[1]   + cz*l_up2[2];
        float lc_d = cx*l_fwd[0]   + cy*l_fwd[1]   + cz*l_fwd[2];
        lc_r = roundf(lc_r/texel)*texel;
        lc_u = roundf(lc_u/texel)*texel;
        cx = lc_r*l_right[0]+lc_u*l_up2[0]+lc_d*l_fwd[0];
        cy = lc_r*l_right[1]+lc_u*l_up2[1]+lc_d*l_fwd[1];
        cz = lc_r*l_right[2]+lc_u*l_up2[2]+lc_d*l_fwd[2];
        float pull = radius;
        float lx = cx - light_dir[0]*pull;
        float ly = cy - light_dir[1]*pull;
        float lz = cz - light_dir[2]*pull;
        float lv[16]; memset(lv,0,64);
        lv[0]= l_right[0]; lv[4]= l_right[1]; lv[8]= l_right[2];  lv[12]=-(l_right[0]*lx+l_right[1]*ly+l_right[2]*lz);
        lv[1]= l_up2[0];   lv[5]= l_up2[1];   lv[9]= l_up2[2];    lv[13]=-(l_up2[0]*lx+l_up2[1]*ly+l_up2[2]*lz);
        lv[2]=-l_fwd[0];   lv[6]=-l_fwd[1];   lv[10]=-l_fwd[2];   lv[14]=(l_fwd[0]*lx+l_fwd[1]*ly+l_fwd[2]*lz);
        lv[3]=0; lv[7]=0; lv[11]=0; lv[15]=1.0f;
        float lp[16];
        qs_m4_ortho_lrtbnf(lp, -radius, radius, -radius, radius, -radius*5.0f, radius*5.0f);
        qs_m4_mul(lp, lv, shadow_matrices[c]);
    }
}

/* ================================================================
   NODE VTABLE IMPLEMENTATION
   ================================================================ */

static void *shadow_create(Qs_Engine *engine, Qs_GpuContext *gpu)
{
    (void)engine;
    ShadowNodeState *s = qs_calloc(1, sizeof(ShadowNodeState), QS_MEM_RENDER);
    if (!s) return NULL;
    s->gpu = gpu;

    /* Shadow map images (fixed size — never resize) */
    for (int i = 0; i < SHADOW_CASCADE_COUNT; i++) {
        s->shadow_images[i] = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
            .width=SHADOW_MAP_SIZE, .height=SHADOW_MAP_SIZE, .mip_levels=1,
            .format=QS_GPU_FORMAT_D32_SFLOAT,
            .usage=QS_GPU_IMAGE_DEPTH_ATTACHMENT|QS_GPU_IMAGE_SAMPLED,
            .sample_count=1});
        if (!s->shadow_images[i]) {
            QS_LOG_ERROR("ShadowNode: shadow map image %d creation failed", i);
            goto fail;
        }
        /* Render-target view (depth aspect for writing) */
        s->shadow_rt_views[i] = qs_gpu_create_image_view_for(
            gpu, s->shadow_images[i], QS_GPU_IMAGE_ASPECT_DEPTH);
        /* Sample view (depth aspect for reading in shader) */
        s->shadow_views[i] = qs_gpu_create_image_view_for(
            gpu, s->shadow_images[i], QS_GPU_IMAGE_ASPECT_DEPTH);
        if (!s->shadow_rt_views[i] || !s->shadow_views[i]) {
            QS_LOG_ERROR("ShadowNode: shadow map view %d creation failed", i);
            goto fail;
        }
    }

    /* Shadow UBO */
    s->shadow_ubo = qs_gpu_create_buffer(gpu, &(Qs_GpuBufferDesc){
        .size=sizeof(ShadowUBO),
        .usage=QS_GPU_BUFFER_UNIFORM,
        .memory=QS_GPU_MEMORY_HOST_VISIBLE});
    if (!s->shadow_ubo) { QS_LOG_ERROR("ShadowNode: UBO creation failed"); goto fail; }

    /* Descriptor set layout: set=0, 6 bindings
       binding 0 — FrameUBO (vertex+frag)
       binding 1 — LightsUBO (frag)
       binding 2 — ShadowUBO (vertex+frag)
       binding 3-5 — shadow maps (frag) */
    {
        Qs_GpuDescriptorBinding b[6] = {
            {0,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
            {1,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_FRAGMENT},
            {2,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
            {3,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
            {4,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
            {5,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
        };
        s->frame_set_layout = qs_gpu_create_descriptor_set_layout(gpu, b, 6);
        if (!s->frame_set_layout) { QS_LOG_ERROR("ShadowNode: set layout failed"); goto fail; }
    }

    /* Shadow pipeline layout: push constants [model(64)+cascade_idx(16)] + set=0 */
    {
        Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_VERTEX, 0, 80};
        Qs_GpuDescriptorSetLayout *sets[] = {s->frame_set_layout};
        s->shadow_pipeline_layout = qs_gpu_create_pipeline_layout(gpu,
            &(Qs_GpuPipelineLayoutDesc){sets, 1, &pc, 1});
        if (!s->shadow_pipeline_layout) { QS_LOG_ERROR("ShadowNode: pipeline layout failed"); goto fail; }
    }

    /* Compile shadow shaders */
    {
        Qs_GpuShader *vs = qs_gpu_compile_shader(gpu, SHADOW_VERT, QS_GPU_SHADER_VERTEX);
        Qs_GpuShader *fs = qs_gpu_compile_shader(gpu, SHADOW_FRAG, QS_GPU_SHADER_FRAGMENT);
        if (!vs || !fs) {
            if (vs) qs_gpu_destroy_shader(gpu,vs);
            if (fs) qs_gpu_destroy_shader(gpu,fs);
            QS_LOG_ERROR("ShadowNode: shader compilation failed");
            goto fail;
        }
        Qs_GpuVertexAttribute attr = {0, QS_GPU_VERTEX_FORMAT_FLOAT3, offsetof(Qs_Vertex,position)};
        Qs_GpuVertexBinding   vb   = {0, sizeof(Qs_Vertex), &attr, 1};
        s->shadow_pipeline = qs_gpu_create_graphics_pipeline(gpu, &(Qs_GpuGraphicsPipelineDesc){
            s->shadow_pipeline_layout, vs, fs, &vb, 1,
            QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_FRONT, true, true,
            QS_GPU_FORMAT_NONE, QS_GPU_FORMAT_D32_SFLOAT});
        qs_gpu_destroy_shader(gpu, vs);
        qs_gpu_destroy_shader(gpu, fs);
        if (!s->shadow_pipeline) { QS_LOG_ERROR("ShadowNode: pipeline creation failed"); goto fail; }
    }

    /* Descriptor pool + set */
    {
        Qs_GpuDescriptorPoolSize sizes[] = {
            {QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,         3},
            {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 3},
        };
        s->desc_pool = qs_gpu_create_descriptor_pool(gpu,
            &(Qs_GpuDescriptorPoolDesc){sizes, 2, 2});
        if (!s->desc_pool) { QS_LOG_ERROR("ShadowNode: desc pool failed"); goto fail; }
        s->frame_desc_set = qs_gpu_alloc_descriptor_set(gpu, s->desc_pool, s->frame_set_layout);
        if (!s->frame_desc_set) { QS_LOG_ERROR("ShadowNode: desc set alloc failed"); goto fail; }
    }

    s->ok = true;
    QS_LOG_INFO("ShadowNode: created (3×%ux%u shadow maps)", SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    return s;

fail:
    /* Partial cleanup — destroy will handle what was created */
    return s; /* Return partial state so destroy can clean up */
}

static void shadow_destroy(void *state, Qs_GpuContext *gpu)
{
    ShadowNodeState *s = state;
    if (!s) return;
    if (s->frame_desc_set) qs_gpu_free_descriptor_set(gpu, s->desc_pool, s->frame_desc_set);
    if (s->desc_pool)      qs_gpu_destroy_descriptor_pool(gpu, s->desc_pool);
    if (s->shadow_pipeline) qs_gpu_destroy_pipeline(gpu, s->shadow_pipeline);
    if (s->shadow_pipeline_layout) qs_gpu_destroy_pipeline_layout(gpu, s->shadow_pipeline_layout);
    if (s->frame_set_layout) qs_gpu_destroy_descriptor_set_layout(gpu, s->frame_set_layout);
    if (s->shadow_ubo) qs_gpu_destroy_buffer(gpu, s->shadow_ubo);
    for (int i = 0; i < SHADOW_CASCADE_COUNT; i++) {
        if (s->shadow_rt_views[i]) qs_gpu_destroy_image_view(gpu, s->shadow_rt_views[i]);
        if (s->shadow_views[i] && s->shadow_views[i] != s->shadow_rt_views[i])
            qs_gpu_destroy_image_view(gpu, s->shadow_views[i]);
        if (s->shadow_images[i]) qs_gpu_destroy_image(gpu, s->shadow_images[i]);
    }
    qs_free(s);
}

static void shadow_on_resize(void *state, Qs_GpuContext *gpu, uint32_t w, uint32_t h)
{
    /* Shadow maps are fixed-size — nothing to do on viewport resize */
    (void)state; (void)gpu; (void)w; (void)h;
}

static void shadow_execute(void *state, const Qs_RgExecCtx *ctx)
{
    ShadowNodeState *s = state;
    if (!s || !s->ok) return;

    /* Write static descriptor bindings once */
    if (!s->desc_written) {
        Qs_GpuBuffer *frame_ubo  = qs_renderer_get_frame_ubo(ctx->renderer);
        Qs_GpuBuffer *lights_ubo = qs_renderer_get_lights_ubo(ctx->renderer);
        if (!frame_ubo || !lights_ubo) return;
        qs_gpu_write_buffer_descriptor(ctx->gpu, s->frame_desc_set, 0, frame_ubo,    0, 0);
        qs_gpu_write_buffer_descriptor(ctx->gpu, s->frame_desc_set, 1, lights_ubo,   0, 0);
        qs_gpu_write_buffer_descriptor(ctx->gpu, s->frame_desc_set, 2, s->shadow_ubo,0, 0);
        /* Shadow maps are sampled in the forward pass (via PBR node's descriptor);
           we still fill bindings 3-5 for completeness (shadow shader doesn't use them). */
        for (int i = 0; i < SHADOW_CASCADE_COUNT; i++) {
            if (s->shadow_views[i]) {
                /* Use the shadow sampler — a linear clamping sampler created by the GPU device */
                Qs_GpuSampler *samp = qs_gpu_create_sampler(ctx->gpu, &(Qs_GpuSamplerDesc){
                    .min_filter=QS_GPU_FILTER_LINEAR,.mag_filter=QS_GPU_FILTER_LINEAR,
                    .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,
                    .mip_levels=1});
                if (samp) {
                    qs_gpu_write_image_descriptor(ctx->gpu, s->frame_desc_set,
                                                  3+(uint32_t)i, samp, s->shadow_views[i]);
                    qs_gpu_destroy_sampler(ctx->gpu, samp); /* desc set holds a ref */
                }
            }
        }
        s->desc_written = true;
    }

    /* Compute CSM matrices */
    Qs_Camera *cam = qs_renderer_camera(ctx->renderer);
    compute_csm(cam, ctx->lights, ctx->light_count,
                s->shadow_matrices, s->shadow_splits);

    /* Upload to GPU UBO */
    ShadowUBO *subo = qs_gpu_map_buffer(s->gpu, s->shadow_ubo);
    if (subo) {
        for (int c = 0; c < SHADOW_CASCADE_COUNT; c++) {
            memcpy(subo->cascade_vp[c], s->shadow_matrices[c], 64);
            subo->cascade_splits[c] = s->shadow_splits[c+1];
        }
        qs_gpu_unmap_buffer(s->gpu, s->shadow_ubo);
    }

    if (ctx->renderable_count == 0) goto emit_outputs;

    /* Render each cascade */
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++) {
        Qs_GpuImageView *rtv = s->shadow_rt_views[cascade];
        Qs_GpuImage     *img = s->shadow_images[cascade];
        if (!rtv || !img) continue;

        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=img, .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .new_layout=QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .aspect=QS_GPU_IMAGE_ASPECT_DEPTH, .base_mip=0, .mip_count=1});

        qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
            .color=NULL, .depth=rtv,
            .clear_depth=1.0f,
            .width=SHADOW_MAP_SIZE, .height=SHADOW_MAP_SIZE});
        qs_cmd_set_viewport(ctx->cmd, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        qs_cmd_bind_pipeline(ctx->cmd, s->shadow_pipeline);
        qs_cmd_bind_descriptor_set(ctx->cmd, s->shadow_pipeline_layout, 0, s->frame_desc_set);

        for (uint32_t ri = 0; ri < ctx->renderable_count; ri++) {
            const Qs_Renderable *ren = &ctx->renderables[ri];
            if (!ren->cast_shadows || !ren->vertex_buffer) continue;
            ShadowPC spc;
            memcpy(spc.model, ren->transform, 64);
            spc.cascade_idx = cascade;
            spc._p[0] = spc._p[1] = spc._p[2] = 0;
            qs_cmd_push_constants(ctx->cmd, s->shadow_pipeline_layout,
                                  QS_GPU_SHADER_VERTEX, 0, sizeof(ShadowPC), &spc);
            qs_cmd_bind_vertex_buffer(ctx->cmd, 0, ren->vertex_buffer, 0);
            if (ren->index_buffer)
                qs_cmd_bind_index_buffer(ctx->cmd, ren->index_buffer, ren->index_16bit);
            if (ren->index_count > 0)
                qs_cmd_draw_indexed(ctx->cmd, ren->index_count, 0, 0);
            else
                qs_cmd_draw(ctx->cmd, ren->vertex_count, 0);
        }
        qs_cmd_end_rendering(ctx->cmd);

        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=img, .old_layout=QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .aspect=QS_GPU_IMAGE_ASPECT_DEPTH, .base_mip=0, .mip_count=1});
    }

emit_outputs:
    /* Emit output port values */
    for (int i = 0; i < SHADOW_CASCADE_COUNT; i++)
        ctx->outputs[i].texture = (Qs_RgTexture){ s->shadow_images[i], s->shadow_views[i] };
    ctx->outputs[SHADOW_CASCADE_COUNT].buffer = s->shadow_ubo;
}

/* ================================================================
   PORT DECLARATIONS
   ================================================================ */

static const Qs_RgPort k_shadow_outputs[] = {
    { .name = "shadow_map_0", .kind = QS_RG_TEXTURE },
    { .name = "shadow_map_1", .kind = QS_RG_TEXTURE },
    { .name = "shadow_map_2", .kind = QS_RG_TEXTURE },
    { .name = "shadow_ubo",   .kind = QS_RG_BUFFER  },
};

/* ================================================================
   PUBLIC NODE TYPE
   ================================================================ */

const Qs_RgNodeType qs_rg_shadow_node_type = {
    .name         = "shadow_csm",
    .inputs       = NULL,
    .input_count  = 0,
    .outputs      = k_shadow_outputs,
    .output_count = 4,
    .create       = shadow_create,
    .destroy      = shadow_destroy,
    .execute      = shadow_execute,
    .on_resize    = shadow_on_resize,
};
