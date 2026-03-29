#ifndef VK_RENDERER_INTERNAL_H
#define VK_RENDERER_INTERNAL_H

/* Plugin-internal header shared between vk_renderer.c and vk_forward.c.
   Not part of the public engine API. */

#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_light.h"

/* ----------------------------------------------------------------
   Constants
   ---------------------------------------------------------------- */
#define QS_CSM_CASCADES   3
#define QS_SHADOW_MAP_SIZE 1024

/* ----------------------------------------------------------------
   VkRenderer — plugin-internal per-renderer state.
   The engine now owns: camera, clear_color, name, nodes, renderables,
   lights, depth buffer, frame UBO, lights UBO, default material, and
   all viewport attachments declared via qs_renderer_add_attachment.

   The plugin owns: pipelines, descriptor sets, shadow UBO (CSM data),
   CSM matrices, and shadow sample views.
   ---------------------------------------------------------------- */
struct VkRenderer {
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

typedef struct VkRenderer VkRenderer;

/* ----------------------------------------------------------------
   Shared pipeline resources
   Shared across all renderer instances for efficiency.  Pipelines,
   pipeline layouts, descriptor set layouts, and samplers are
   stateless once created and safe to reuse.
   ---------------------------------------------------------------- */
typedef struct VkPassResources {
    /* Shadow depth-only pass (CSM) */
    Qs_GpuPipeline            *shadow_pipeline;
    Qs_GpuPipelineLayout      *shadow_layout;

    /* Forward lit pass */
    Qs_GpuPipeline            *forward_pipeline;
    Qs_GpuPipelineLayout      *forward_layout;
    Qs_GpuDescriptorSetLayout *frame_set_layout;

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
} VkPassResources;

/* ----------------------------------------------------------------
   vk_renderer.c helpers called from vk_forward.c
   ---------------------------------------------------------------- */

/* Returns the shared pass resources owned by the global render system. */
VkPassResources *vk_renderer_pass_resources(void);

/* ----------------------------------------------------------------
   vk_forward.c entry points called from vk_renderer.c
   ---------------------------------------------------------------- */

/* Initialises the forward pass and adds render nodes.  Called from
   renderer_create. */
void vk_forward_attach(Qs_Engine *engine, VkRenderer *r, Qs_Renderer *handle);

/* Tears down the forward pass.  Called from renderer_destroy. */
void vk_forward_detach(VkRenderer *r);

/* Called from renderer_on_resize after the engine has resized all
   viewport-scaled attachments.  Re-writes descriptor sets. */
void vk_forward_on_resize(VkRenderer *r, uint32_t w, uint32_t h);

/* Destroys all shared pass resources (pipelines, layouts, samplers).
   Called from vk_render_shutdown after all renderer instances are gone. */
void vk_pass_resources_shutdown(Qs_GpuContext *gpu, VkPassResources *ps);

/* ----------------------------------------------------------------
   Post-process settings
   Exposed to the editor via the plugin's on_editor_ui callback.
   ---------------------------------------------------------------- */
typedef struct VkPostProcessSettings {
    float bloom_strength;    /* blend factor for bloom over HDR (default 0.04) */
    float vignette_strength; /* vignette power exponent        (default 0.35)  */
} VkPostProcessSettings;

/* Returns a pointer to the single mutable post-process settings instance. */
VkPostProcessSettings *vk_post_process_settings(void);

#endif /* VK_RENDERER_INTERNAL_H */
