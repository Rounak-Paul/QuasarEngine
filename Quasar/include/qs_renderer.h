#ifndef QS_RENDERER_H
#define QS_RENDERER_H

#include <vulkan/vulkan.h>

typedef struct Ca_Viewport Ca_Viewport;
typedef struct Qs_Renderer Qs_Renderer;

typedef struct Qs_RendererDesc {
    VkDevice          device;
    VkClearColorValue clear_color;
} Qs_RendererDesc;

/// Creates a renderer backed by Vulkan. Multiple renderers can coexist
/// (e.g. scene view, minimap, material thumbnails).
Qs_Renderer *qs_renderer_create(const Qs_RendererDesc *desc);

/// Destroys the renderer and all GPU resources it owns.
void qs_renderer_destroy(Qs_Renderer *renderer);

/// Binds this renderer to an existing viewport. The viewport will invoke
/// the renderer's render pass each frame. Can be called on different
/// viewports to drive multiple render targets from one renderer.
void qs_renderer_bind(Qs_Renderer *renderer, Ca_Viewport *viewport);

/// Updates the clear color for subsequent frames.
void qs_renderer_set_clear_color(Qs_Renderer *renderer, VkClearColorValue color);

#endif
