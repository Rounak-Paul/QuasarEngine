#ifndef QS_RENDERER_H
#define QS_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "qs_gpu.h"
#include "qs_light.h"
#include "qs_mesh.h"
#include "qs_material.h"

typedef struct Qs_Engine            Qs_Engine;
typedef struct Qs_Renderer          Qs_Renderer;
typedef struct Qs_RenderNode        Qs_RenderNode;
typedef struct Qs_RenderAttachment  Qs_RenderAttachment;
typedef struct Qs_LightComp         Qs_LightComp;   ///< Defined in qs_scene.h.

/* ================================================================
   CAMERA
   ================================================================ */

typedef enum Qs_Projection {
    QS_PROJECTION_PERSPECTIVE  = 0,
    QS_PROJECTION_ORTHOGRAPHIC = 1,
} Qs_Projection;

typedef struct Qs_Camera {
    float         position[3];
    float         target[3];
    float         up[3];
    Qs_Projection projection;
    float         fov_deg;
    float         ortho_size;
    float         near_plane;
    float         far_plane;
} Qs_Camera;

/* ================================================================
   STANDARD FRAME UNIFORM DATA
   Both engine (writer) and plugin shaders (reader) must agree on
   these layouts.  Use std140 alignment rules in GLSL.
   ================================================================ */

/// Per-frame uniform block written by the engine each frame.
typedef struct Qs_FrameUBO {
    float view[16];
    float proj[16];
    float inv_view_proj[16];
    float cam_pos[3];
    float time;
    float screen_width;
    float screen_height;
    float _pad[2];
} Qs_FrameUBO; /* 208 bytes, std140 */

#define QS_LIGHTS_MAX 128

/// Per-frame lights uniform block written by the engine each frame.
typedef struct Qs_LightsUBO {
    Qs_LightGPU lights[QS_LIGHTS_MAX];
    uint32_t    count;
    uint32_t    _pad[3];
} Qs_LightsUBO;

/* ================================================================
   RENDERABLE

   Qs_RenderableDesc  — submission struct; caller provides Qs_Mesh / Qs_Material.
   Qs_Renderable      — GPU-packed struct the engine stores and passes to plugins.
                         The engine extracts vertex/index buffers and material
                         descriptor data at submit time so pass nodes never need
                         to call into the mesh or material systems directly.
   ================================================================ */

typedef struct Qs_AABB {
    float min[3];
    float max[3];
} Qs_AABB;

/// Submission descriptor — fill this and pass to qs_renderer_submit_renderable.
typedef struct Qs_RenderableDesc {
    Qs_Mesh     *mesh;              ///< Required.
    Qs_Material *material;          ///< NULL → engine uses the renderer default material.
    float        transform[16];     ///< Column-major model matrix.
    Qs_AABB      bounds;            ///< World-space AABB for engine-side culling.
    bool         cast_shadows;
    bool         receive_shadows;
} Qs_RenderableDesc;

/// GPU-packed renderable — populated by the engine; passed to render-pass nodes
/// via Qs_RenderContext.renderables.  Pass nodes work at the GPU command level
/// and do not need to access the mesh or material systems.
typedef struct Qs_Renderable {
    /* Mesh — extracted from Qs_Mesh at submit time */
    Qs_GpuBuffer *vertex_buffer;
    Qs_GpuBuffer *index_buffer;     ///< NULL for non-indexed meshes.
    uint32_t      vertex_count;
    uint32_t      index_count;
    bool          index_16bit;      ///< true = UINT16, false = UINT32.

    /* Material — extracted from Qs_Material (or renderer default) at submit time */
    Qs_GpuDescriptorSet *material_set;    ///< Ready-to-bind descriptor set.
    Qs_PBRParams         material_params; ///< Value copy; safe across frames.
    Qs_AlphaMode         alpha_mode;
    bool                 double_sided;

    /* Transform and visibility */
    float    transform[16];  ///< Column-major model matrix.
    Qs_AABB  bounds;         ///< World-space AABB.
    bool     cast_shadows;
    bool     receive_shadows;
} Qs_Renderable;

/* ================================================================
   RENDER ATTACHMENT â€” engine-managed off-screen image
   ================================================================ */

typedef enum Qs_RenderAttachmentUsage {
    QS_ATTACHMENT_COLOR = 0,
    QS_ATTACHMENT_DEPTH = 1,
} Qs_RenderAttachmentUsage;

/// Descriptor for declaring a render attachment.
/// The engine creates, resizes, and destroys the underlying image automatically.
typedef struct Qs_RenderAttachmentDesc {
    const char                *name;
    Qs_GpuImageFormat          format;
    Qs_RenderAttachmentUsage   usage;
    /// Viewport-relative size â€” 1.0 = full viewport width/height.
    /// Set to 0 when using fixed_width / fixed_height.
    float                      width_scale;
    float                      height_scale;
    /// Fixed pixel size.  When > 0, overrides scale; image is never resized.
    uint32_t                   fixed_width;
    uint32_t                   fixed_height;
} Qs_RenderAttachmentDesc;

/* ================================================================
   RENDER PASS NODE â€” pipeline phase
   ================================================================ */

/// Context supplied to every render-pass node callback each frame.
typedef struct Qs_RenderContext {
    Qs_Renderer        *renderer;
    Qs_GpuCmd          *cmd;
    uint32_t            width;
    uint32_t            height;
    float               view[16];
    float               proj[16];
    float               dt;

    /// Engine-populated renderable list for this frame.
    const Qs_Renderable *renderables;
    uint32_t             renderable_count;

    /// Engine-populated GPU-packed light list for this frame.
    const Qs_LightGPU   *lights;
    uint32_t             light_count;

    /// Final output target for this frame.
    Qs_GpuImageView     *swapchain_view;
    uint32_t             swapchain_width;
    uint32_t             swapchain_height;
} Qs_RenderContext;

typedef void (*Qs_RenderNodeFn)(const Qs_RenderContext *ctx, void *user_data);

typedef struct Qs_RenderNodeDesc {
    const char      *name;
    int32_t          priority; ///< Execution order â€” lower runs first.
    Qs_RenderNodeFn  execute;
    void            *user_data;
} Qs_RenderNodeDesc;

/* ================================================================
   RENDERER DESCRIPTOR
   ================================================================ */

typedef struct Qs_RendererDesc {
    const char  *name;
    const char  *backend;    ///< NULL = default backend.
    float        clear_color[4];
    Qs_Camera    camera;
    bool         depth_test;
    /// Optional PBR material used when a submitted renderable has no material.
    /// If NULL the engine creates a built-in grey dielectric fallback.
    const Qs_MaterialDesc *default_material;
} Qs_RendererDesc;

/* ================================================================
   RENDERER BACKEND VTABLE
   Implement to register a rendering backend plugin.
   ================================================================ */

typedef struct Qs_RendererBackend {
    const char *name;

    /* System lifecycle */
    bool (*init)    (Qs_Engine *engine, Qs_GpuContext *gpu, void **out_ctx);
    void (*shutdown)(void *ctx);
    void (*update)  (void *ctx, float dt);

    /* Renderer instance lifecycle.
       handle is the engine-allocated Qs_Renderer* so the backend can call
       qs_renderer_add_attachment() and qs_renderer_add_node() during creation.
       Returns a backend-owned opaque impl pointer, or NULL on failure. */
    void *(*renderer_create) (void *ctx, Qs_Engine *engine,
                               const Qs_RendererDesc *desc, Qs_Renderer *handle);
    void  (*renderer_destroy)(void *ctx, void *impl);

    /// Called after the engine has resized all viewport-scaled attachments.
    /// Backend should re-write descriptor sets that reference those image views.
    void  (*renderer_on_resize)(void *ctx, void *impl, uint32_t w, uint32_t h);
} Qs_RendererBackend;

void qs_renderer_backend_register  (const Qs_RendererBackend *backend);
void qs_renderer_backend_unregister(const char *name);
void qs_renderer_backend_set_default(const char *name);

/* ================================================================
   PUBLIC RENDERER API
   ================================================================ */

Qs_Renderer *qs_renderer_create (Qs_Engine *engine, const Qs_RendererDesc *desc);
void         qs_renderer_destroy(Qs_Renderer *renderer);
void         qs_renderer_bind   (Qs_Renderer *renderer, Qs_Viewport *viewport);

Qs_Camera  *qs_renderer_camera          (Qs_Renderer *renderer);
void        qs_renderer_set_clear_color (Qs_Renderer *renderer, const float color[4]);
const float *qs_renderer_clear_color    (const Qs_Renderer *renderer);
const char *qs_renderer_name            (const Qs_Renderer *renderer);
void        qs_renderer_extents         (const Qs_Renderer *renderer,
                                          uint32_t *out_w, uint32_t *out_h);

Qs_RenderNode *qs_renderer_add_node   (Qs_Renderer *renderer,
                                        const Qs_RenderNodeDesc *desc);
void           qs_renderer_remove_node(Qs_Renderer *renderer, Qs_RenderNode *node);

/* ================================================================
   RENDER ATTACHMENT API
   Call from Qs_RendererBackend.renderer_create to declare attachments
   that the engine owns and resizes automatically.
   ================================================================ */

Qs_RenderAttachment *qs_renderer_add_attachment(Qs_Renderer *renderer,
                                                 const Qs_RenderAttachmentDesc *desc);
Qs_GpuImageView     *qs_attachment_view (const Qs_RenderAttachment *att);
Qs_GpuImage         *qs_attachment_image(const Qs_RenderAttachment *att);

/// Engine-managed depth buffer view (NULL when depth_test=false).
Qs_GpuImageView *qs_renderer_depth_view(const Qs_Renderer *renderer);

/* ================================================================
   ENGINE UBO ACCESSORS
   Return the engine-owned per-frame UBO handles so backends can
   write descriptor sets that reference them during renderer_create.
   ================================================================ */

Qs_GpuBuffer *qs_renderer_get_frame_ubo (const Qs_Renderer *renderer);
Qs_GpuBuffer *qs_renderer_get_lights_ubo(const Qs_Renderer *renderer);

/* ================================================================
   RENDERABLE / LIGHT SUBMISSION
   ================================================================ */

/// Submit a renderable for this frame.  The engine extracts GPU handles from
/// desc->mesh and desc->material and stores a GPU-packed Qs_Renderable
/// internally.  If desc->material is NULL the renderer default material is used.
void                  qs_renderer_submit_renderable (Qs_Renderer *renderer,
                                                      const Qs_RenderableDesc *desc);
void                  qs_renderer_clear_renderables (Qs_Renderer *renderer);
const Qs_Renderable  *qs_renderer_renderables       (const Qs_Renderer *renderer,
                                                      uint32_t *out_count);

void               qs_renderer_submit_light    (Qs_Renderer *renderer, Qs_Light *light);
void               qs_renderer_submit_light_comp(Qs_Renderer *renderer,
                                                 const Qs_LightComp *comp);
void               qs_renderer_clear_lights(Qs_Renderer *renderer);
const Qs_LightGPU *qs_renderer_lights      (const Qs_Renderer *renderer,
                                             uint32_t *out_count);

#endif /* QS_RENDERER_H */
