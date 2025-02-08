#pragma once

#include <qspch.h>
#include <Math/Math.h>
#include <Renderer/Vulkan/VulkanImage.h>

namespace Quasar
{
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget() = default;

    b8 create();
    void destroy();

    void resize(Math::extent extent);

    b8 render(Math::extent extent, const VkClearColorValue &bg_color);

    VkImageView get_resolve_image_view() { return resolve_image._image_view; }

private:
    VulkanImage resolve_image;
    VulkanImage offscreen_image;
    VkFramebuffer framebuffer;

};
} // namespace Quasar
