#ifndef PBR_INTERNAL_H
#define PBR_INTERNAL_H

/* Built-in PBR renderer — internal header shared between
   pbr_renderer.c and pbr_forward.c.  Not part of the public engine API. */

#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_light.h"

/* ----------------------------------------------------------------
   Constants
   ---------------------------------------------------------------- */
#define QS_CSM_CASCADES    3
#define QS_SHADOW_MAP_SIZE 2048

/* Default MSAA sample count.  Clamped to device maximum at attach time. */
#define PBR_MSAA_SAMPLES   4

/* ----------------------------------------------------------------
   PbrRenderer — per-renderer state owned by the PBR backend.
   ---------------------------------------------------------------- */
struct PbrRenderer {
    char          name[64];

    Qs_Renderer  *engine_renderer; /* engine-owned handle set at renderer_create */
    Qs_Engine    *engine;
    Qs_GpuContext *gpu;

    /* CSM shadow computation state */
    float         shadow_matrices[QS_CSM_CASCADES][16];
    float         shadow_splits[QS_CSM_CASCADES + 1];

    /* Plugin-owned UBO — shadow/CSM data */
    Qs_GpuBuffer *shadow_ubo;

    /* Descriptor pool + per-renderer descriptor sets */
    Qs_GpuDescriptorPool *desc_pool;
    Qs_GpuDescriptorSet  *frame_desc_set;
    Qs_GpuDescriptorSet  *composite_desc_set;
    Qs_GpuDescriptorSet  *bloom_desc_sets[2];

    /* Engine attachment handles declared at renderer_create time */
    Qs_RenderAttachment *hdr_att;
    Qs_RenderAttachment *shadow_att[QS_CSM_CASCADES];
    Qs_RenderAttachment *bloom_att[2];

    /* MSAA transient resources for the forward lit pass */
    Qs_GpuImage    *msaa_color_image;
    Qs_GpuImageView *msaa_color_view;
    Qs_GpuImage    *msaa_depth_image;
    Qs_GpuImageView *msaa_depth_view;
    uint32_t         current_msaa_samples;
    uint32_t         last_w, last_h;

    /* Shadow sampling views (one per cascade) */
    Qs_GpuImageView *shadow_sample_views[QS_CSM_CASCADES];

    /* Render node handles */
    Qs_RenderNode *shadow_node;
    Qs_RenderNode *forward_node;
    Qs_RenderNode *bloom_node;
    Qs_RenderNode *composite_node;

    bool ok; /* false until first renderer_on_resize completes */
};

typedef struct PbrRenderer PbrRenderer;

/* ----------------------------------------------------------------
   PbrPassResources — shared pipelines, layouts, and samplers.
   Stateless once created; safe to reuse across renderer instances.
   ---------------------------------------------------------------- */
typedef struct PbrPassResources {
    /* Shadow depth-only pass (CSM) */
    Qs_GpuPipeline            *shadow_pipeline;
    Qs_GpuPipelineLayout      *shadow_layout;

    /* Forward lit pass — one pair per MSAA tier (0=1×, 1=2×, 2=4×, 3=8×) */
#define PBR_MSAA_TIER_COUNT 4
    Qs_GpuPipeline            *forward_pipelines[PBR_MSAA_TIER_COUNT];
    Qs_GpuPipeline            *forward_wireframe_pipelines[PBR_MSAA_TIER_COUNT];
    Qs_GpuPipelineLayout      *forward_layout;
    Qs_GpuDescriptorSetLayout *frame_set_layout;
    uint32_t                   dev_max_samples;

    /* Bloom (downsample / upsample) */
    Qs_GpuPipeline            *bloom_down_pipeline;
    Qs_GpuPipeline            *bloom_up_pipeline;
    Qs_GpuPipelineLayout      *bloom_layout;
    Qs_GpuDescriptorSetLayout *bloom_set_layout;

    /* Composite (ACES tonemap + vignette → swapchain) */
    Qs_GpuPipeline            *composite_pipeline;
    Qs_GpuPipelineLayout      *composite_layout;
    Qs_GpuDescriptorSetLayout *composite_set_layout;

    /* Shared samplers */
    Qs_GpuSampler             *linear_sampler;
    Qs_GpuSampler             *point_sampler;
    Qs_GpuSampler             *shadow_sampler;

    bool ok;
} PbrPassResources;

/* ----------------------------------------------------------------
   Debug flag bits (stored in Qs_FrameUBO.debug_flags)
   ---------------------------------------------------------------- */
#define PBR_DEBUG_SHOW_NORMALS 0x1u

/* ----------------------------------------------------------------
   pbr_renderer.c helpers called from pbr_forward.c
   ---------------------------------------------------------------- */

/* Returns the shared pass resources. */
PbrPassResources *pbr_renderer_pass_resources(void);

/* ----------------------------------------------------------------
   pbr_forward.c entry points called from pbr_renderer.c
   ---------------------------------------------------------------- */

void pbr_forward_attach    (Qs_Engine *engine, PbrRenderer *r, Qs_Renderer *handle);
void pbr_forward_detach    (PbrRenderer *r);
void pbr_forward_on_resize (PbrRenderer *r, uint32_t w, uint32_t h);

void pbr_pass_resources_shutdown(Qs_GpuContext *gpu, PbrPassResources *ps);

/* ----------------------------------------------------------------
   Internal engine function — called by the PBR backend at create time
   to report the device maximum MSAA sample count.
   ---------------------------------------------------------------- */
void qs_renderer_set_max_msaa_samples(Qs_Renderer *r, uint32_t max);

#endif /* PBR_INTERNAL_H */
