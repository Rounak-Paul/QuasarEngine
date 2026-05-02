#ifndef PBR_INTERNAL_H
#define PBR_INTERNAL_H

/* Plugin-internal header shared between pbr_renderer.c and pbr_forward.c.
   Not part of the public engine API. */

#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_light.h"

/* ----------------------------------------------------------------
   Constants
   ---------------------------------------------------------------- */
#define QS_CSM_CASCADES    3
#define QS_SHADOW_MAP_SIZE 2048

/* Desired MSAA sample count for the forward lit pass.  Automatically clamped
   to the device maximum at attach time.  Set to 1 to disable MSAA. */
#define PBR_MSAA_SAMPLES   4

/* ----------------------------------------------------------------
   PbrRenderer — plugin-internal per-renderer state.
   The engine now owns: camera, clear_color, name, nodes, renderables,
   lights, depth buffer, frame UBO, lights UBO, default material, and
   all viewport attachments declared via qs_renderer_add_attachment.

   The plugin owns: pipelines, descriptor sets, shadow UBO (CSM data),
   CSM matrices, and shadow sample views.
   ---------------------------------------------------------------- */
struct PbrRenderer {
    char          name[64];

    Qs_Renderer  *engine_renderer; /* engine-owned handle set at renderer_create */
    Qs_Engine    *engine;
    Qs_GpuContext *gpu;             /* cached pointer from the system context */

    /* CSM shadow computation state */
    float         shadow_matrices[QS_CSM_CASCADES][16];
    float         shadow_splits[QS_CSM_CASCADES + 1];

    /* Plugin-owned UBO — shadow/CSM data (not written by the engine) */
    Qs_GpuBuffer *shadow_ubo;

    /* Descriptor pool + per-renderer descriptor sets */
    Qs_GpuDescriptorPool *desc_pool;
    Qs_GpuDescriptorSet  *frame_desc_set;      /* set=0: UBOs + shadow samplers */
    Qs_GpuDescriptorSet  *composite_desc_set;  /* tonemap pass                  */
    Qs_GpuDescriptorSet  *bloom_desc_sets[2];  /* bloom ping-pong               */

    /* Engine attachment handles declared at renderer_create time */
    Qs_RenderAttachment *hdr_att;             /* full-res RGBA16F color target */
    Qs_RenderAttachment *shadow_att[QS_CSM_CASCADES]; /* 1024x1024 depth maps  */
    Qs_RenderAttachment *bloom_att[2];        /* half-res bloom ping-pong       */

    /* Plugin-owned MSAA transient resources for the forward lit pass.
       msaa_color is the MSAA render target; the resolved output goes to hdr_att.
       msaa_depth is a matching MSAA depth buffer used only during the pass. */
    Qs_GpuImage    *msaa_color_image;
    Qs_GpuImageView *msaa_color_view;
    Qs_GpuImage    *msaa_depth_image;
    Qs_GpuImageView *msaa_depth_view;
    uint32_t         current_msaa_samples; /* sample count of the allocated MSAA images */
    uint32_t         last_w, last_h;       /* viewport dimensions from last on_resize */

    /* Shadow sampling views (one per cascade, created from shadow_att images).
       The engine manages the depth-attachment view via qs_attachment_view();
       these are separate sampler views created by the plugin. */
    Qs_GpuImageView *shadow_sample_views[QS_CSM_CASCADES];

    /* Render node handles (kept for removal in renderer_destroy) */
    Qs_RenderNode *shadow_node;
    Qs_RenderNode *forward_node;
    Qs_RenderNode *bloom_node;
    Qs_RenderNode *composite_node;

    bool ok; /* false until first renderer_on_resize completes */
};

typedef struct PbrRenderer PbrRenderer;

/* ----------------------------------------------------------------
   Shared pipeline resources
   Shared across all renderer instances for efficiency.  Pipelines,
   pipeline layouts, descriptor set layouts, and samplers are
   stateless once created and safe to reuse.
   ---------------------------------------------------------------- */
typedef struct PbrPassResources {
    /* Shadow depth-only pass (CSM) */
    Qs_GpuPipeline            *shadow_pipeline;
    Qs_GpuPipelineLayout      *shadow_layout;

    /* Forward lit pass
     * One pipeline pair per MSAA tier: index 0=1×, 1=2×, 2=4×, 3=8×.
     * Only entries up to [sample_count_to_idx(dev_max_samples)] are created. */
#define PBR_MSAA_TIER_COUNT 4
    Qs_GpuPipeline            *forward_pipelines[PBR_MSAA_TIER_COUNT];
    Qs_GpuPipeline            *forward_wireframe_pipelines[PBR_MSAA_TIER_COUNT];
    Qs_GpuPipelineLayout      *forward_layout;
    Qs_GpuDescriptorSetLayout *frame_set_layout;
    uint32_t                   dev_max_samples; /* highest tier supported by the device */

    /* Bloom (downsample / upsample) */
    Qs_GpuPipeline            *bloom_down_pipeline;
    Qs_GpuPipeline            *bloom_up_pipeline;
    Qs_GpuPipelineLayout      *bloom_layout;
    Qs_GpuDescriptorSetLayout *bloom_set_layout;

    /* Composite (ACES tonemap + vignette  swapchain) */
    Qs_GpuPipeline            *composite_pipeline;
    Qs_GpuPipelineLayout      *composite_layout;
    Qs_GpuDescriptorSetLayout *composite_set_layout;

    /* Shared samplers */
    Qs_GpuSampler             *linear_sampler;
    Qs_GpuSampler             *point_sampler;
    Qs_GpuSampler             *shadow_sampler; /* compare/PCF */

    bool ok;
} PbrPassResources;

/* ----------------------------------------------------------------
   pbr_renderer.c helpers called from pbr_forward.c
   ---------------------------------------------------------------- */

/* Returns the shared pass resources owned by the global render system. */
PbrPassResources *pbr_renderer_pass_resources(void);

/* Returns the active Qs_Renderer handle (set during renderer_create). */
Qs_Renderer *pbr_active_renderer(void);

/* Debug flag bits owned by the PBR plugin (stored in Qs_FrameUBO.debug_flags) */
#define PBR_DEBUG_SHOW_NORMALS 0x1u

/* ----------------------------------------------------------------
   pbr_forward.c entry points called from pbr_renderer.c
   ---------------------------------------------------------------- */

/* Initialises the forward pass and adds render nodes.  Called from
   renderer_create. */
void pbr_forward_attach(Qs_Engine *engine, PbrRenderer *r, Qs_Renderer *handle);

/* Tears down the forward pass.  Called from renderer_destroy. */
void pbr_forward_detach(PbrRenderer *r);

/* Called from renderer_on_resize after the engine has resized all
   viewport-scaled attachments.  Re-writes descriptor sets. */
void pbr_forward_on_resize(PbrRenderer *r, uint32_t w, uint32_t h);

/* Destroys all shared pass resources (pipelines, layouts, samplers).
   Called from pbr_render_shutdown after all renderer instances are gone. */
void pbr_pass_resources_shutdown(Qs_GpuContext *gpu, PbrPassResources *ps);

/* ----------------------------------------------------------------
   Post-process settings
   Exposed to the editor via the plugin's on_editor_ui callback.
   ---------------------------------------------------------------- */
typedef struct PbrPostProcessSettings {
    float    bloom_strength;    /* blend factor for bloom over HDR (default 0.04) */
    float    vignette_strength; /* vignette power exponent        (default 0.35)  */
    uint32_t msaa_sample_count; /* MSAA tier: 1=off, 2/4/8=on (default PBR_MSAA_SAMPLES) */
} PbrPostProcessSettings;

/* Returns a pointer to the single mutable post-process settings instance. */
PbrPostProcessSettings *pbr_post_process_settings(void);

#endif /* PBR_INTERNAL_H */
