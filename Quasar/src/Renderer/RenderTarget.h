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

    b8 render(Math::extent extent, const VkClearColorValue &bg_color, u8 frame_index);

    VkImageView get_resolve_image_view() { return _resolve_images[_frame_index]._image_view; }

private:
    DynamicArray<VulkanImage> _resolve_images;
    DynamicArray<VulkanImage> _offscreen_images;
    DynamicArray<VkFramebuffer> _framebuffer;

    u8 _frame_index = 0;
};
} // namespace Quasar
