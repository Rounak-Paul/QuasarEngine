#ifndef VK_RENDERER_INTERNAL_H
#define VK_RENDERER_INTERNAL_H

/* Plugin-internal header shared between vk_renderer.c and vk_forward.c.
   Not part of the public engine API. */

#include "qs_renderer.h"

/* Opaque from vk_forward.c's point of view — define full struct in vk_renderer.c */
typedef struct VkRenderer VkRenderer;

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

#endif /* VK_RENDERER_INTERNAL_H */
