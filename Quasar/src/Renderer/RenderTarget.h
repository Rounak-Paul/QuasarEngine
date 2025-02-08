#pragma once

#include <qspch.h>
#include <Math/Math.h>
#include <Renderer/Vulkan/VulkanImage.h>

#define MAX_FRAMES_IN_FLIGHT 3

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

    VkImageView get_resolve_image_view() { return resolve_images[_frame_index]._image_view; }

private:
    DynamicArray<VulkanImage> resolve_images;
    DynamicArray<VulkanImage> offscreen_images;
    DynamicArray<VulkanCommandBuffer> _command_buffers;
    DynamicArray<VkFramebuffer> framebuffer;



    u32 _frame_index = 0;
};
} // namespace Quasar
