#ifndef QS_RENDERER_H
#define QS_RENDERER_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_SystemDesc    Qs_SystemDesc;
typedef struct Qs_Engine        Qs_Engine;
typedef struct Ca_Viewport      Ca_Viewport;
typedef struct Ca_Instance      Ca_Instance;
typedef struct Qs_Renderer      Qs_Renderer;

/* ================================================================
   CAMERA
   ================================================================ */

/// Projection mode for a renderer's camera.
typedef enum Qs_Projection {
    QS_PROJECTION_PERSPECTIVE  = 0,
    QS_PROJECTION_ORTHOGRAPHIC = 1,
} Qs_Projection;

/// Camera state embedded in each renderer.
typedef struct Qs_Camera {
    float position[3];          ///< World-space eye position.
    float target[3];            ///< World-space look-at target.
    float up[3];                ///< World-space up vector.
    Qs_Projection projection;
    float fov_deg;              ///< Vertical field of view (perspective).
    float ortho_size;           ///< Half-height of view volume (orthographic).
    float near_plane;
    float far_plane;
} Qs_Camera;

/* ================================================================
   RENDER PASS NODE — extensible pipeline phase
   ================================================================ */

/// Render context passed to each render pass node callback.
typedef struct Qs_RenderContext {
    Qs_Renderer    *renderer;
    VkCommandBuffer cmd;
    uint32_t        width;
    uint32_t        height;
    float           view[16];       ///< Column-major 4x4 view matrix.
    float           proj[16];       ///< Column-major 4x4 projection matrix.
    float           dt;             ///< Frame delta time in seconds.
} Qs_RenderContext;

/// Callback executed for a pipeline phase.
typedef void (*Qs_RenderNodeFn)(const Qs_RenderContext *ctx, void *user_data);

/// Descriptor for adding a render pass node to a renderer's pipeline.
typedef struct Qs_RenderNodeDesc {
    const char       *name;         ///< Debug label (e.g. "depth_prepass", "pbr_forward").
    int32_t           priority;     ///< Execution order (lower runs first, default 0).
    Qs_RenderNodeFn   execute;      ///< Called each frame inside the render pass.
    void             *user_data;
} Qs_RenderNodeDesc;

/// Opaque handle to a render pass node in a renderer's pipeline.
typedef struct Qs_RenderNode Qs_RenderNode;

/* ================================================================
   RENDERER INSTANCE
   ================================================================ */

/// Configuration for creating a renderer instance.
typedef struct Qs_RendererDesc {
    const char       *name;         ///< Debug label (e.g. "scene", "minimap", "thumbnail").
    VkClearColorValue clear_color;
    Qs_Camera         camera;       ///< Initial camera state (zero-inited = defaults).
    bool              depth_test;   ///< Create a depth attachment (default: true if zero).
} Qs_RendererDesc;

/// Creates a renderer instance. Must be destroyed with qs_renderer_destroy.
/// The renderer is automatically tracked by the render system.
Qs_Renderer *qs_renderer_create(Qs_Engine *engine, const Qs_RendererDesc *desc);

/// Destroys a renderer and all GPU resources it owns.
void qs_renderer_destroy(Qs_Renderer *renderer);

/// Binds this renderer to a viewport. The viewport will invoke the renderer
/// each frame. Replaces any previous binding.
void qs_renderer_bind(Qs_Renderer *renderer, Ca_Viewport *viewport);

/// Returns a mutable pointer to the renderer's camera for direct manipulation.
Qs_Camera *qs_renderer_camera(Qs_Renderer *renderer);

/// Updates the clear color.
void qs_renderer_set_clear_color(Qs_Renderer *renderer, VkClearColorValue color);

/// Adds a render pass node to the pipeline. Returns the node handle.
Qs_RenderNode *qs_renderer_add_node(Qs_Renderer *renderer,
                                     const Qs_RenderNodeDesc *desc);

/// Removes a render pass node from the pipeline.
void qs_renderer_remove_node(Qs_Renderer *renderer, Qs_RenderNode *node);

/// Returns the renderer's debug name.
const char *qs_renderer_name(const Qs_Renderer *renderer);

/// Returns the VkDevice used by the render system.
VkDevice qs_renderer_device(const Qs_Renderer *renderer);

/// Returns the current framebuffer dimensions (0 if unbound).
void qs_renderer_extents(const Qs_Renderer *renderer,
                         uint32_t *out_width, uint32_t *out_height);

/* ================================================================
   RENDER SYSTEM — engine system that manages all renderers
   ================================================================ */

/// Returns the system descriptor for registration with the engine.
/// Requires a Ca_Instance pointer as context (stored in the system data).
Qs_SystemDesc qs_render_system_desc(Ca_Instance *ca_instance);

#endif
