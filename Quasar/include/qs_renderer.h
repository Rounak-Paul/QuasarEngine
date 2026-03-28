#ifndef QS_RENDERER_H
#define QS_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "qs_gpu.h"

typedef struct Qs_Engine        Qs_Engine;
typedef struct Qs_Renderer      Qs_Renderer;    ///< Opaque — defined by the renderer backend.
typedef struct Qs_RenderNode    Qs_RenderNode;  ///< Opaque — defined by the renderer backend.
typedef struct Qs_Light         Qs_Light;
typedef struct Qs_LightGPU      Qs_LightGPU;    ///< Full definition in qs_light.h.

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
    Qs_GpuCmd      *cmd;
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

/* Qs_RenderNode is an opaque type defined by the renderer backend. */

/* ================================================================
   RENDERER INSTANCE
   ================================================================ */

/// Configuration for creating a renderer instance.
typedef struct Qs_RendererDesc {
    const char       *name;         ///< Debug label (e.g. "scene", "minimap", "thumbnail").
    const char       *backend;      ///< Backend name to use (NULL = default backend).
    float             clear_color[4];
    Qs_Camera         camera;       ///< Initial camera state (zero-inited = defaults).
    bool              depth_test;   ///< Create a depth attachment (default: true if zero).
} Qs_RendererDesc;

/* ================================================================
   RENDERER BACKEND — implement to provide a rendering backend
   ================================================================ */

/// Vtable that a renderer plugin fills in and registers with the engine.
/// Every function pointer must be non-NULL.
typedef struct Qs_RendererBackend {
    const char *name;

    /* --- System lifecycle ----------------------------------------- */

    /// One-time GPU resource setup (device, physical device, command pools).
    /// Called when the Render system initialises.
    bool (*init)(Qs_Engine *engine, Qs_GpuContext *gpu, void **out_ctx);

    /// Tear down all GPU resources and release ctx.
    void (*shutdown)(void *ctx);

    /// Per-frame update (propagate delta time, etc.).
    void (*update)(void *ctx, float dt);

    /* --- Renderer instances --------------------------------------- */

    /// Allocate and initialise a backend-internal renderer.
    /// Returns a backend-owned opaque pointer (impl).  The engine wraps
    /// this in a Qs_Renderer handle and calls renderer_post_create.
    void          *(*renderer_create)(void *ctx, Qs_Engine *engine,
                                       const Qs_RendererDesc *desc);

    /// Destroy a renderer and free all its GPU resources.
    void           (*renderer_destroy)(void *ctx, void *impl);

    /// Register viewport callbacks so the renderer redraws every frame.
    void           (*renderer_bind)(void *ctx, void *impl,
                                    Qs_Viewport *viewport);

    /// Called by the engine immediately after it wraps impl in a
    /// Qs_Renderer handle.  Backends that need the back-reference
    /// (e.g. to populate Qs_RenderContext.renderer) implement this;
    /// others may leave it NULL.
    void           (*renderer_post_create)(void *impl, Qs_Renderer *handle);

    /* --- Renderer accessors (impl = backend-internal pointer) ----- */

    Qs_Camera     *(*renderer_camera)(void *impl);
    void           (*renderer_set_clear_color)(void *impl,
                                               const float color[4]);
    Qs_RenderNode *(*renderer_add_node)(void *impl,
                                        const Qs_RenderNodeDesc *desc);
    void           (*renderer_remove_node)(void *impl,
                                           Qs_RenderNode *node);
    const char    *(*renderer_name)(const void *impl);
    void           (*renderer_extents)(const void *impl,
                                       uint32_t *out_w, uint32_t *out_h);

    /* --- Per-frame light submission ------------------------------ */

    void              (*submit_light)(void *impl, Qs_Light *light);
    void              (*clear_lights)(void *impl);
    const Qs_LightGPU *(*get_lights)(const void *impl, uint32_t *out_count);
} Qs_RendererBackend;

/// Registers a renderer backend.  Multiple backends may be registered
/// simultaneously; each is identified by its unique name field.
/// Must be called before the first qs_renderer_create with this backend
/// (or before the Render system initialises if no backends are yet registered).
void qs_renderer_backend_register(const Qs_RendererBackend *backend);

/// Unregisters the backend with the given name.  Shuts it down if the
/// Render system is currently running.
void qs_renderer_backend_unregister(const char *name);

/// Sets the default backend used when Qs_RendererDesc.backend is NULL.
/// If never called, the first registered backend is the default.
void qs_renderer_backend_set_default(const char *name);

/* ================================================================
   PUBLIC RENDERER API
   All functions dispatch through the registered backend.
   ================================================================ */

/// Creates a renderer instance. Destroy with qs_renderer_destroy.
Qs_Renderer *qs_renderer_create(Qs_Engine *engine, const Qs_RendererDesc *desc);

/// Destroys a renderer and all GPU resources it owns.
void qs_renderer_destroy(Qs_Renderer *renderer);

/// Binds this renderer to a Qs_Viewport — it will render every frame.
void qs_renderer_bind(Qs_Renderer *renderer, Qs_Viewport *viewport);

/// Returns a mutable pointer to the renderer's camera.
Qs_Camera *qs_renderer_camera(Qs_Renderer *renderer);

/// Updates the clear colour.
void qs_renderer_set_clear_color(Qs_Renderer *renderer, const float color[4]);

/// Adds a render pass node to the pipeline. Returns the node handle.
Qs_RenderNode *qs_renderer_add_node(Qs_Renderer *renderer,
                                     const Qs_RenderNodeDesc *desc);

/// Removes a render pass node from the pipeline.
void qs_renderer_remove_node(Qs_Renderer *renderer, Qs_RenderNode *node);

/// Returns the renderer's debug name.
const char *qs_renderer_name(const Qs_Renderer *renderer);

/// Returns the current framebuffer dimensions (0 if unbound).
void qs_renderer_extents(const Qs_Renderer *renderer,
                          uint32_t *out_width, uint32_t *out_height);

/* --- Per-frame light submission (on the renderer) --------------- */

/// Submits a light to a renderer for the current frame.  Must be called
/// each frame — lights are not persistent.
void qs_renderer_submit_light(Qs_Renderer *renderer, Qs_Light *light);

/// Clears all submitted lights from a renderer (called automatically each frame).
void qs_renderer_clear_lights(Qs_Renderer *renderer);

/// Returns the array of GPU-packed lights submitted this frame.
const Qs_LightGPU *qs_renderer_lights(const Qs_Renderer *renderer,
                                        uint32_t *out_count);

#endif
