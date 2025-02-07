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

        b8 create(const VulkanContext* context);
        void destroy();

    private:
        VkImage _image = nullptr;
        VkDeviceMemory _image_memory = nullptr;
        VkImageView _image_view = nullptr;
        Math::extent _extent;
    };
} // namespace Quasar

