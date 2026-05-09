#ifndef QS_GPU_H
#define QS_GPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct Qs_Engine  Qs_Engine;

/* ================================================================
   OPAQUE GPU HANDLE TYPES
   All GPU resources are created and destroyed exclusively through
   qs_gpu_* functions.  Backends never include vulkan headers.
   ================================================================ */

typedef struct Qs_GpuContext          Qs_GpuContext;     ///< Engine GPU context (device, queue, pools)
typedef struct Qs_Viewport            Qs_Viewport;       ///< UI viewport widget (render target)
typedef struct Qs_GpuCmd              Qs_GpuCmd;         ///< Command buffer for recording GPU commands
typedef struct Qs_GpuBuffer           Qs_GpuBuffer;      ///< GPU buffer (vertex, index, uniform, storage)
typedef struct Qs_GpuImage            Qs_GpuImage;       ///< GPU image (texture, depth)
typedef struct Qs_GpuImageView        Qs_GpuImageView;   ///< Image view for shader access or attachments
typedef struct Qs_GpuSampler          Qs_GpuSampler;     ///< Texture sampler
typedef struct Qs_GpuShader           Qs_GpuShader;      ///< Compiled shader module
typedef struct Qs_GpuPipeline         Qs_GpuPipeline;    ///< Graphics or compute pipeline
typedef struct Qs_GpuPipelineLayout   Qs_GpuPipelineLayout;
typedef struct Qs_GpuDescriptorSetLayout Qs_GpuDescriptorSetLayout;
typedef struct Qs_GpuDescriptorPool   Qs_GpuDescriptorPool;
typedef struct Qs_GpuDescriptorSet    Qs_GpuDescriptorSet;

/* ================================================================
   ENUMERATIONS
   ================================================================ */

typedef enum Qs_GpuShaderStage {
    QS_GPU_SHADER_VERTEX   = 0x01,
    QS_GPU_SHADER_FRAGMENT = 0x02,
    QS_GPU_SHADER_COMPUTE  = 0x04,
} Qs_GpuShaderStage;

typedef enum Qs_GpuImageFormat {
    QS_GPU_FORMAT_RGBA8_UNORM    = 0,
    QS_GPU_FORMAT_RGBA8_SRGB     = 1,
    QS_GPU_FORMAT_RG8_UNORM      = 2,
    QS_GPU_FORMAT_R8_UNORM       = 3,
    QS_GPU_FORMAT_RGBA16_SFLOAT  = 4,
    QS_GPU_FORMAT_BGRA8_UNORM    = 5,
    QS_GPU_FORMAT_D32_SFLOAT     = 6,
    QS_GPU_FORMAT_D32_SFLOAT_S8  = 7,
    QS_GPU_FORMAT_D24_UNORM_S8   = 8,
    QS_GPU_FORMAT_DEPTH_AUTO     = 9,  ///< Engine selects best available depth format
    QS_GPU_FORMAT_NONE           = 10, ///< No color attachment (depth-only pass)
} Qs_GpuImageFormat;

typedef enum {
    QS_GPU_IMAGE_SAMPLED          = 0x01,
    QS_GPU_IMAGE_TRANSFER_SRC     = 0x02,
    QS_GPU_IMAGE_TRANSFER_DST     = 0x04,
    QS_GPU_IMAGE_COLOR_ATTACHMENT = 0x08,
    QS_GPU_IMAGE_DEPTH_ATTACHMENT = 0x10,
} Qs_GpuImageUsage;

typedef enum {
    QS_GPU_IMAGE_ASPECT_COLOR = 0,
    QS_GPU_IMAGE_ASPECT_DEPTH = 1,
} Qs_GpuImageAspect;

typedef enum {
    QS_GPU_IMAGE_LAYOUT_UNDEFINED         = 0,
    QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC      = 1,
    QS_GPU_IMAGE_LAYOUT_TRANSFER_DST      = 2,
    QS_GPU_IMAGE_LAYOUT_SHADER_READ       = 3,
    QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT  = 4,
    QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT  = 5,
} Qs_GpuImageLayout;

typedef enum {
    QS_GPU_BUFFER_VERTEX   = 0x01,
    QS_GPU_BUFFER_INDEX    = 0x02,
    QS_GPU_BUFFER_UNIFORM  = 0x04,
    QS_GPU_BUFFER_STORAGE  = 0x08,
    QS_GPU_BUFFER_TRANSFER = 0x10,
} Qs_GpuBufferUsage;

typedef enum {
    QS_GPU_MEMORY_DEVICE_LOCAL = 0,  ///< GPU-only fast memory
    QS_GPU_MEMORY_HOST_VISIBLE = 1,  ///< CPU-writable (persistently mappable)
} Qs_GpuMemoryUsage;

typedef enum {
    QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER = 0,
    QS_GPU_DESCRIPTOR_UNIFORM_BUFFER         = 1,
    QS_GPU_DESCRIPTOR_STORAGE_BUFFER         = 2,
    QS_GPU_DESCRIPTOR_STORAGE_IMAGE          = 3,
} Qs_GpuDescriptorType;

typedef enum {
    QS_GPU_TOPOLOGY_TRIANGLES = 0,
    QS_GPU_TOPOLOGY_LINES     = 1,
    QS_GPU_TOPOLOGY_POINTS    = 2,
} Qs_GpuTopology;

typedef enum {
    QS_GPU_CULL_NONE  = 0,
    QS_GPU_CULL_BACK  = 1,
    QS_GPU_CULL_FRONT = 2,
} Qs_GpuCullMode;

typedef enum {
    QS_GPU_VERTEX_FORMAT_FLOAT  = 0,   ///< float  (1 component)
    QS_GPU_VERTEX_FORMAT_FLOAT2 = 1,   ///< vec2   (2 components)
    QS_GPU_VERTEX_FORMAT_FLOAT3 = 2,   ///< vec3   (3 components)
    QS_GPU_VERTEX_FORMAT_FLOAT4 = 3,   ///< vec4   (4 components)
} Qs_GpuVertexFormat;

typedef enum {
    QS_GPU_FILTER_NEAREST = 0,
    QS_GPU_FILTER_LINEAR  = 1,
} Qs_GpuFilter;

typedef enum {
    QS_GPU_WRAP_REPEAT          = 0,
    QS_GPU_WRAP_MIRRORED_REPEAT = 1,
    QS_GPU_WRAP_CLAMP_TO_EDGE   = 2,
} Qs_GpuWrap;

/* ================================================================
   RESOURCE DESCRIPTOR STRUCTS
   ================================================================ */

typedef struct Qs_GpuBufferDesc {
    uint64_t          size;
    Qs_GpuBufferUsage usage;
    Qs_GpuMemoryUsage memory;
} Qs_GpuBufferDesc;

typedef struct Qs_GpuImageDesc {
    uint32_t          width;
    uint32_t          height;
    uint32_t          mip_levels;    ///< 1 = no mips
    Qs_GpuImageFormat format;
    Qs_GpuImageUsage  usage;
    uint32_t          sample_count;  ///< 1 = no MSAA (default), 2/4/8 = multisample
} Qs_GpuImageDesc;

typedef struct Qs_GpuImageViewDesc {
    Qs_GpuImage      *image;
    Qs_GpuImageFormat format;        ///< Must match the image's format
    Qs_GpuImageAspect aspect;
    uint32_t          base_mip;
    uint32_t          mip_levels;
} Qs_GpuImageViewDesc;

typedef struct Qs_GpuSamplerDesc {
    Qs_GpuFilter min_filter;
    Qs_GpuFilter mag_filter;
    Qs_GpuWrap   wrap_u;
    Qs_GpuWrap   wrap_v;
    uint32_t     mip_levels;         ///< Set to image mip count for trilinear filtering
    bool         anisotropy;
} Qs_GpuSamplerDesc;

typedef struct Qs_GpuDescriptorBinding {
    uint32_t             binding;
    Qs_GpuDescriptorType type;
    uint32_t             count;
    Qs_GpuShaderStage    stages;
} Qs_GpuDescriptorBinding;

typedef struct Qs_GpuDescriptorPoolSize {
    Qs_GpuDescriptorType type;
    uint32_t             count;
} Qs_GpuDescriptorPoolSize;

typedef struct Qs_GpuDescriptorPoolDesc {
    const Qs_GpuDescriptorPoolSize *sizes;
    uint32_t                        size_count;
    uint32_t                        max_sets;
} Qs_GpuDescriptorPoolDesc;

typedef struct Qs_GpuVertexAttribute {
    uint32_t           location;
    Qs_GpuVertexFormat format;
    uint32_t           offset;
} Qs_GpuVertexAttribute;

typedef struct Qs_GpuVertexBinding {
    uint32_t                    binding;
    uint32_t                    stride;
    const Qs_GpuVertexAttribute *attributes;
    uint32_t                    attribute_count;
} Qs_GpuVertexBinding;

typedef struct Qs_GpuPushConstantRange {
    Qs_GpuShaderStage stages;
    uint32_t          offset;
    uint32_t          size;
} Qs_GpuPushConstantRange;

typedef struct Qs_GpuPipelineLayoutDesc {
    Qs_GpuDescriptorSetLayout *const *set_layouts;
    uint32_t                          set_layout_count;
    const Qs_GpuPushConstantRange    *push_constants;
    uint32_t                          push_constant_count;
} Qs_GpuPipelineLayoutDesc;

typedef struct Qs_GpuGraphicsPipelineDesc {
    Qs_GpuPipelineLayout      *layout;
    Qs_GpuShader              *vertex_shader;
    Qs_GpuShader              *fragment_shader;
    const Qs_GpuVertexBinding *vertex_bindings;
    uint32_t                   vertex_binding_count;
    Qs_GpuTopology             topology;
    Qs_GpuCullMode             cull_mode;
    bool                       depth_test;
    bool                       depth_write;
    Qs_GpuImageFormat          color_format;
    Qs_GpuImageFormat          depth_format;   ///< QS_GPU_FORMAT_DEPTH_AUTO = no depth attachment
    bool                       wireframe;      ///< Draw triangles as lines (VK_POLYGON_MODE_LINE)
    uint32_t                   sample_count;   ///< 1 = no MSAA (default), 2/4/8 = multisample
} Qs_GpuGraphicsPipelineDesc;

typedef struct Qs_GpuImageBarrier {
    Qs_GpuImage      *image;
    Qs_GpuImageLayout old_layout;
    Qs_GpuImageLayout new_layout;
    Qs_GpuImageAspect aspect;
    uint32_t          base_mip;
    uint32_t          mip_count;
} Qs_GpuImageBarrier;

typedef struct Qs_GpuRenderTarget {
    Qs_GpuImageView *color;           ///< Color attachment (required)
    Qs_GpuImageView *depth;           ///< Depth attachment (NULL = no depth)
    /// When non-NULL and color is a multisample image, the resolved single-sample
    /// output is written to this view automatically at vkCmdEndRendering.
    Qs_GpuImageView *resolve_target;  ///< MSAA resolve destination (NULL = no resolve)
    float            clear_color[4];  ///< RGBA clear value for color attachment
    float            clear_depth;     ///< Depth clear value (typically 1.0)
    uint32_t         width;
    uint32_t         height;
    bool             load_color;      ///< true = LOAD existing contents instead of CLEAR
    bool             load_depth;      ///< true = LOAD existing depth instead of CLEAR
} Qs_GpuRenderTarget;

/* ================================================================
   VIEWPORT FRAME — per-frame render data passed to the render callback
   ================================================================ */

/// All GPU resources the render callback needs for one frame.
typedef struct Qs_GpuFrame {
    Qs_GpuCmd      *cmd;           ///< Command buffer to record into
    Qs_GpuImageView *color_target; ///< Viewport's current swapchain image view
    uint32_t         width;
    uint32_t         height;
} Qs_GpuFrame;

/// Callback fired each frame so the backend can record draw commands.
typedef void (*Qs_ViewportRenderFn)(const Qs_GpuFrame *frame, Qs_Viewport *viewport,
                                    void *user_data);

/// Callback fired when the viewport is resized.
typedef void (*Qs_ViewportResizeFn)(Qs_Viewport *viewport, uint32_t width,
                                    uint32_t height, void *user_data);

/* ================================================================
   GPU CONTEXT — engine-wide GPU state
   ================================================================ */

/// Returns the engine's GPU context.  Valid after qs_engine_create().
Qs_GpuContext *qs_engine_gpu(Qs_Engine *engine);

/// Returns the maximum multisample count supported by the device for color
/// and depth images used as render targets (always a power of 2, minimum 1).
uint32_t qs_gpu_max_sample_count(Qs_GpuContext *gpu);

/* ================================================================
   BUFFER API
   ================================================================ */

/// Creates a GPU buffer.  Destroy with qs_gpu_destroy_buffer.
Qs_GpuBuffer *qs_gpu_create_buffer(Qs_GpuContext *gpu, const Qs_GpuBufferDesc *desc);

/// Creates a device-local buffer pre-filled with data via a staging upload.
/// Equivalent to: create staging → map/copy → transfer → destroy staging.
Qs_GpuBuffer *qs_gpu_create_buffer_from_data(Qs_GpuContext *gpu, Qs_GpuBufferUsage usage,
                                              const void *data, uint64_t size);

/// Destroys a buffer and frees its memory.
void qs_gpu_destroy_buffer(Qs_GpuContext *gpu, Qs_GpuBuffer *buffer);

/// Maps a HOST_VISIBLE buffer for CPU writes.  Returns the mapped pointer.
void *qs_gpu_map_buffer(Qs_GpuContext *gpu, Qs_GpuBuffer *buffer);

/// Unmaps a previously mapped buffer.
void qs_gpu_unmap_buffer(Qs_GpuContext *gpu, Qs_GpuBuffer *buffer);

/* ================================================================
   IMAGE API
   ================================================================ */

/// Creates a GPU image.  Destroy with qs_gpu_destroy_image.
Qs_GpuImage *qs_gpu_create_image(Qs_GpuContext *gpu, const Qs_GpuImageDesc *desc);

/// Destroys an image and frees its memory.
void qs_gpu_destroy_image(Qs_GpuContext *gpu, Qs_GpuImage *image);

/// Uploads pixel data to an image via a staging buffer.
/// Handles staging creation, layout transitions, optional mipmap generation.
bool qs_gpu_upload_image(Qs_GpuContext *gpu, Qs_GpuImage *image,
                         const void *pixels, uint64_t size, bool generate_mips);

/// Creates an image view.  Destroy with qs_gpu_destroy_image_view.
Qs_GpuImageView *qs_gpu_create_image_view(Qs_GpuContext *gpu,
                                           const Qs_GpuImageViewDesc *desc);

/// Convenience: creates a simple view covering all mips using the image's own format.
Qs_GpuImageView *qs_gpu_create_image_view_for(Qs_GpuContext *gpu, Qs_GpuImage *image,
                                               Qs_GpuImageAspect aspect);

/// Destroys an image view.
void qs_gpu_destroy_image_view(Qs_GpuContext *gpu, Qs_GpuImageView *view);

/// Creates a sampler.  Destroy with qs_gpu_destroy_sampler.
Qs_GpuSampler *qs_gpu_create_sampler(Qs_GpuContext *gpu, const Qs_GpuSamplerDesc *desc);

/// Destroys a sampler.
void qs_gpu_destroy_sampler(Qs_GpuContext *gpu, Qs_GpuSampler *sampler);

/* ================================================================
   SHADER API
   ================================================================ */

/// Compiles GLSL source into a shader module.  Destroy with qs_gpu_destroy_shader.
Qs_GpuShader *qs_gpu_compile_shader(Qs_GpuContext *gpu, const char *glsl_source,
                                     Qs_GpuShaderStage stage);

/// Destroys a compiled shader module.
void qs_gpu_destroy_shader(Qs_GpuContext *gpu, Qs_GpuShader *shader);

/* ================================================================
   DESCRIPTOR API
   ================================================================ */

/// Creates a descriptor set layout.  Destroy with qs_gpu_destroy_descriptor_set_layout.
Qs_GpuDescriptorSetLayout *qs_gpu_create_descriptor_set_layout(
    Qs_GpuContext *gpu, const Qs_GpuDescriptorBinding *bindings, uint32_t count);

/// Destroys a descriptor set layout.
void qs_gpu_destroy_descriptor_set_layout(Qs_GpuContext *gpu,
                                           Qs_GpuDescriptorSetLayout *layout);

/// Creates a descriptor pool.  Destroy with qs_gpu_destroy_descriptor_pool.
Qs_GpuDescriptorPool *qs_gpu_create_descriptor_pool(Qs_GpuContext *gpu,
                                                      const Qs_GpuDescriptorPoolDesc *desc);

/// Destroys a descriptor pool (also implicitly frees all sets from it).
void qs_gpu_destroy_descriptor_pool(Qs_GpuContext *gpu, Qs_GpuDescriptorPool *pool);

/// Allocates a descriptor set from a pool.  Free with qs_gpu_free_descriptor_set.
Qs_GpuDescriptorSet *qs_gpu_alloc_descriptor_set(Qs_GpuContext *gpu,
                                                   Qs_GpuDescriptorPool *pool,
                                                   Qs_GpuDescriptorSetLayout *layout);

/// Frees a descriptor set back to its pool.
void qs_gpu_free_descriptor_set(Qs_GpuContext *gpu, Qs_GpuDescriptorPool *pool,
                                 Qs_GpuDescriptorSet *set);

/// Writes a combined image+sampler binding into a descriptor set.
void qs_gpu_write_image_descriptor(Qs_GpuContext *gpu, Qs_GpuDescriptorSet *set,
                                    uint32_t binding,
                                    Qs_GpuSampler *sampler, Qs_GpuImageView *view);

/// Writes a uniform buffer binding into a descriptor set.
void qs_gpu_write_buffer_descriptor(Qs_GpuContext *gpu, Qs_GpuDescriptorSet *set,
                                     uint32_t binding, Qs_GpuBuffer *buffer,
                                     uint64_t offset, uint64_t range);

/* ================================================================
   PIPELINE API
   ================================================================ */

/// Creates a pipeline layout.  Destroy with qs_gpu_destroy_pipeline_layout.
Qs_GpuPipelineLayout *qs_gpu_create_pipeline_layout(Qs_GpuContext *gpu,
                                                      const Qs_GpuPipelineLayoutDesc *desc);

/// Destroys a pipeline layout.
void qs_gpu_destroy_pipeline_layout(Qs_GpuContext *gpu, Qs_GpuPipelineLayout *layout);

/// Creates a graphics pipeline.  Destroy with qs_gpu_destroy_pipeline.
Qs_GpuPipeline *qs_gpu_create_graphics_pipeline(Qs_GpuContext *gpu,
                                                  const Qs_GpuGraphicsPipelineDesc *desc);

/// Destroys a pipeline.
void qs_gpu_destroy_pipeline(Qs_GpuContext *gpu, Qs_GpuPipeline *pipeline);

/* ================================================================
   COMMAND RECORDING API (qs_cmd_*)
   Two usage patterns:
     1. Frame commands: via Qs_GpuFrame.cmd in the viewport render callback
     2. Transfer commands: via qs_gpu_begin_transfer / qs_gpu_end_transfer
   ================================================================ */

/// Allocates and begins a one-shot transfer command buffer.
Qs_GpuCmd *qs_gpu_begin_transfer(Qs_GpuContext *gpu);

/// Ends, submits, waits, and frees a transfer command buffer.
void qs_gpu_end_transfer(Qs_GpuContext *gpu, Qs_GpuCmd *cmd);

/// Begins a dynamic rendering pass into the specified render target.
void qs_cmd_begin_rendering(Qs_GpuCmd *cmd, const Qs_GpuRenderTarget *target);

/// Ends the current rendering pass.
void qs_cmd_end_rendering(Qs_GpuCmd *cmd);

/// Sets viewport + scissor to cover the given dimensions.
void qs_cmd_set_viewport(Qs_GpuCmd *cmd, uint32_t width, uint32_t height);

/// Binds a graphics pipeline.
void qs_cmd_bind_pipeline(Qs_GpuCmd *cmd, Qs_GpuPipeline *pipeline);

/// Binds a descriptor set.
void qs_cmd_bind_descriptor_set(Qs_GpuCmd *cmd, Qs_GpuPipelineLayout *layout,
                                 uint32_t set_index, Qs_GpuDescriptorSet *set);

/// Pushes constants into the pipeline.
void qs_cmd_push_constants(Qs_GpuCmd *cmd, Qs_GpuPipelineLayout *layout,
                            Qs_GpuShaderStage stages, uint32_t offset,
                            uint32_t size, const void *data);

/// Binds a vertex buffer.
void qs_cmd_bind_vertex_buffer(Qs_GpuCmd *cmd, uint32_t binding,
                                Qs_GpuBuffer *buffer, uint64_t offset);

/// Binds an index buffer.
void qs_cmd_bind_index_buffer(Qs_GpuCmd *cmd, Qs_GpuBuffer *buffer, bool use_uint16);

/// Issues a non-indexed draw call.
void qs_cmd_draw(Qs_GpuCmd *cmd, uint32_t vertex_count, uint32_t first_vertex);

/// Issues an indexed draw call.
void qs_cmd_draw_indexed(Qs_GpuCmd *cmd, uint32_t index_count,
                          uint32_t first_index, int32_t vertex_offset);

/// Inserts a pipeline/image memory barrier to transition an image's layout.
void qs_cmd_image_barrier(Qs_GpuCmd *cmd, const Qs_GpuImageBarrier *barrier);

/// Copies pixel data from a buffer into an image (mip 0, full extent).
void qs_cmd_copy_buffer_to_image(Qs_GpuCmd *cmd, Qs_GpuBuffer *src,
                                  Qs_GpuImage *dst, uint32_t width, uint32_t height);

/// Copies a region of an image into a buffer.  Image must be in TRANSFER_SRC layout.
void qs_cmd_copy_image_to_buffer(Qs_GpuCmd *cmd, Qs_GpuImage *src,
                                  Qs_GpuBuffer *dst, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height);

/// Blits between two mip levels of the same image (for mipmap generation).
void qs_cmd_blit_image_mip(Qs_GpuCmd *cmd, Qs_GpuImage *image,
                            uint32_t src_mip, uint32_t src_w, uint32_t src_h,
                            uint32_t dst_mip, uint32_t dst_w, uint32_t dst_h);

/* ================================================================
   VIEWPORT API
   ================================================================ */

/// Registers render and resize callbacks on a viewport.
void qs_viewport_set_callbacks(Qs_Viewport *viewport,
                                Qs_ViewportRenderFn on_render, void *render_data,
                                Qs_ViewportResizeFn on_resize, void *resize_data);

/// Returns the viewport's current width in pixels.
uint32_t qs_viewport_width(const Qs_Viewport *viewport);

/// Returns the viewport's current height in pixels.
uint32_t qs_viewport_height(const Qs_Viewport *viewport);

/* ================================================================
   GPU MEMORY STATS
   ================================================================ */

/// Per-purpose GPU VRAM categories tracked by the engine.
typedef enum Qs_GpuMemTag {
    QS_GPU_MEM_VERTEX    = 0,  ///< Device-local vertex buffers
    QS_GPU_MEM_INDEX     = 1,  ///< Device-local index buffers
    QS_GPU_MEM_UNIFORM   = 2,  ///< Host-visible uniform buffers (UBOs)
    QS_GPU_MEM_STORAGE   = 3,  ///< Storage buffers (SSBO)
    QS_GPU_MEM_TEXTURE   = 4,  ///< Sampled/transfer images (textures)
    QS_GPU_MEM_RT_COLOR  = 5,  ///< Color render-target images
    QS_GPU_MEM_RT_DEPTH  = 6,  ///< Depth/stencil images
    QS_GPU_MEM_OTHER     = 7,  ///< Miscellaneous (readback, etc.)
    QS_GPU_MEM_TAG_COUNT = 8,
} Qs_GpuMemTag;

typedef struct Qs_GpuMemStats {
    size_t device_bytes;        ///< Total DEVICE_LOCAL VRAM in use
    size_t host_bytes;          ///< Total HOST_VISIBLE VRAM in use (UBOs, mapped buffers)
    size_t device_total_bytes;  ///< Total DEVICE_LOCAL heap size reported by the driver
    size_t tag_bytes[QS_GPU_MEM_TAG_COUNT]; ///< Per-purpose breakdown
} Qs_GpuMemStats;

/// Fills *out with current GPU memory usage.
void qs_gpu_mem_stats(Qs_GpuContext *gpu, Qs_GpuMemStats *out);

/// Writes the physical device name into buf (null-terminated, max len bytes).
void qs_gpu_device_name(Qs_GpuContext *gpu, char *buf, size_t len);

#endif /* QS_GPU_H */
