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

    void resize(Math::Extent extent);

    b8 render(Math::Extent extent, const VkClearColorValue &bg_color, u8 frame_index);

    VkImageView get_resolve_image_view() { return _resolve_images[_frame_index]._image_view; }

private:
    Math::Extent _extent;
    DynamicArray<VulkanImage> _resolve_images;
    DynamicArray<VulkanImage> _offscreen_images;
    DynamicArray<VkFramebuffer> _framebuffer;

    u8 _frame_index = 0;

    // TODO: temp
    DynamicArray<Math::Vertex> vertices;
    DynamicArray<u32> indices;

    b8 actual_create();
    void actual_destroy();
    void updateUniformBuffer(u32 index);
};
} // namespace Quasar
