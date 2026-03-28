#ifndef VK_RENDERER_INTERNAL_H
#define VK_RENDERER_INTERNAL_H

/* Plugin-internal header shared between vk_renderer.c and vk_forward.c.
   Not part of the public engine API. */

#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_light.h"

/* ----------------------------------------------------------------
   Shared constants
   ---------------------------------------------------------------- */
#define QS_CSM_CASCADES            3
#define QS_SHADOW_MAP_SIZE         1024
#define QS_MAX_RENDERABLES         4096
#define QS_MAX_RENDERERS           32
#define QS_MAX_RENDER_NODES        16
#define QS_MAX_LIGHTS_PER_RENDERER 128

/* Qs_RenderNode — concrete definition (opaque to external code) */
struct Qs_RenderNode {
    char              name[64];
    int32_t           priority;
    Qs_RenderNodeFn   execute;
    void             *user_data;
    bool              active;
};

/* VkRenderer — the plugin-internal per-renderer state.
   Shared between vk_renderer.c and vk_forward.c. */
struct VkRenderer {
    Qs_Renderer      *handle;    /* engine-owned wrapper (set post-create) */

    char              name[64];
    bool              in_use;

    Qs_GpuContext    *gpu;
    Qs_GpuImage      *depth;
    Qs_GpuImageView  *depth_view;
    bool              depth_enabled;

    float             clear_color[4];
    uint32_t          fb_width;
    uint32_t          fb_height;

    Qs_Camera         camera;

    Qs_RenderNode     nodes[QS_MAX_RENDER_NODES];
    uint32_t          node_count;

    float             dt;

    /* Per-frame renderable accumulation */
    Qs_Renderable     renderables[QS_MAX_RENDERABLES];
    uint32_t          renderable_count;

    /* Per-frame light accumulation */
    Qs_LightGPU       lights[QS_MAX_LIGHTS_PER_RENDERER];
    uint32_t          light_count;

    /* HDR off-screen color target (RGBA16F) */
    Qs_GpuImage      *hdr_image;
    Qs_GpuImageView  *hdr_view;

    /* CSM shadow maps: QS_CSM_CASCADES depth images */
    Qs_GpuImage      *shadow_images[QS_CSM_CASCADES];
    Qs_GpuImageView  *shadow_views[QS_CSM_CASCADES];         /* depth attachment */
    Qs_GpuImageView  *shadow_sample_views[QS_CSM_CASCADES]; /* shader sample   */

    /* Bloom ping-pong (half-res RGBA16F) */
    Qs_GpuImage      *bloom_images[2];
    Qs_GpuImageView  *bloom_views[2];

    /* Per-frame UBOs (HOST_VISIBLE, persistently mapped) */
    Qs_GpuBuffer     *frame_ubo;   /* FrameUBO  */
    Qs_GpuBuffer     *light_ubo;   /* LightUBO  */
    Qs_GpuBuffer     *shadow_ubo;  /* ShadowUBO */

    /* Descriptor pool + sets */
    Qs_GpuDescriptorPool  *desc_pool;
    Qs_GpuDescriptorSet   *frame_desc_set;      /* set=0: forward pass  */
    Qs_GpuDescriptorSet   *composite_desc_set;  /* tonemap pass         */
    Qs_GpuDescriptorSet   *bloom_desc_sets[2];  /* bloom ping-pong      */

    /* CSM matrices (updated per frame from the shadow pass) */
    float shadow_matrices[QS_CSM_CASCADES][16];
    float shadow_splits[QS_CSM_CASCADES + 1];

    /* Swapchain target set by on_render, used by the composite node */
    Qs_GpuImageView  *swapchain_view;
    uint32_t          swapchain_width;
    uint32_t          swapchain_height;
};

typedef struct VkRenderer VkRenderer;

/* ----------------------------------------------------------------
   Shared pipeline resources (pipelines/samplers/layouts kept in
   VkRenderSystemData and shared across all renderer instances).
   ---------------------------------------------------------------- */
typedef struct VkPassResources {
    /* Shadow depth-only pass (CSM) */
    Qs_GpuPipeline            *shadow_pipeline;
    Qs_GpuPipelineLayout      *shadow_layout;

    /* Forward lit pass */
    Qs_GpuPipeline            *forward_pipeline;
    Qs_GpuPipelineLayout      *forward_layout;
    Qs_GpuDescriptorSetLayout *frame_set_layout;  /* set=0: UBOs + shadow samplers */

    /* Bloom (dual-pass: downsample / upsample) */
    Qs_GpuPipeline            *bloom_down_pipeline;
    Qs_GpuPipeline            *bloom_up_pipeline;
    Qs_GpuPipelineLayout      *bloom_layout;
    Qs_GpuDescriptorSetLayout *bloom_set_layout;

    /* Composite (tonemap + vignette → swapchain) */
    Qs_GpuPipeline            *composite_pipeline;
    Qs_GpuPipelineLayout      *composite_layout;
    Qs_GpuDescriptorSetLayout *composite_set_layout;

    /* Shared samplers */
    Qs_GpuSampler             *linear_sampler;
    Qs_GpuSampler             *point_sampler;
    Qs_GpuSampler             *shadow_sampler;  /* compare (PCF) */

    bool ok;
} VkPassResources;

/* ----------------------------------------------------------------
   Render-node management helpers.
   Called directly from vk_forward.c to avoid routing through the
   engine dispatch layer while the Qs_Renderer handle may not yet
   exist (attach happens inside renderer_create).
   ---------------------------------------------------------------- */

Qs_RenderNode *vk_renderer_add_node_impl(VkRenderer *r,
                                          const Qs_RenderNodeDesc *desc);

void           vk_renderer_remove_node_impl(VkRenderer *r,
                                             Qs_RenderNode *node);

/* Retrieve the shared pass resources owned by the global render system. */
VkPassResources *vk_renderer_pass_resources(void);

/* Returns the swapchain image view stored this frame (used by composite node). */
Qs_GpuImageView *vk_renderer_swapchain_view(VkRenderer *r);
uint32_t         vk_renderer_swapchain_width(VkRenderer *r);
uint32_t         vk_renderer_swapchain_height(VkRenderer *r);

/* Resize all forward-renderer offscreen images to (w, h).
   Must be called from OUTSIDE a frame recording callback (e.g. on_resize)
   because it calls vkDeviceWaitIdle to safely destroy old images first. */
void vk_forward_resize(VkRenderer *r, uint32_t w, uint32_t h);

#endif /* VK_RENDERER_INTERNAL_H */
