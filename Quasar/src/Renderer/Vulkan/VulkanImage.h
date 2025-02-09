#pragma once

#include <qspch.h>
#include "VulkanContext.h"
#include <Math/Math.h>

namespace Quasar
{
    class VulkanImage
    {
    public:
        VulkanImage() {};
        ~VulkanImage() = default;

        b8 create(
            const VulkanContext *context,
            Math::Extent extent,
            VkFormat format,
            VkImageTiling tiling,
            VkImageUsageFlags usage,
            VkSampleCountFlagBits samples,
            VkMemoryPropertyFlags properties,
            VkImageAspectFlags aspect
        );

        void destroy(const VulkanContext* context);

        /**
         * Transitions the provided image from old_layout to new_layout.
         */
        void transition_layout(
            const VulkanContext *context,
            VkCommandBuffer command_buffer,
            VkFormat format,
            VkImageLayout new_layout);

    private:
        VkImage _image = nullptr;
        VkDeviceMemory _image_memory = nullptr;
        VkImageView _image_view = nullptr;
        Math::Extent _extent;
        VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

        friend class RenderTarget;
    };
} // namespace Quasar

