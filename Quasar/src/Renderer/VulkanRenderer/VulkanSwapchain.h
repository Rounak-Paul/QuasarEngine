#pragma once
#include <qspch.h>
#include "VulkanDevice.h"

namespace Quasar::Vulkan {

class VulkanSwapchain {
    public:
    VulkanSwapchain() {};
    ~VulkanSwapchain() = default;

    b8 create(VulkanDevice* device, VkSurfaceKHR* surface, u32 width, u32 height, VkAllocationCallbacks* allocator);
    void destroy();
    b8 recreate(u32 width, u32 height);

    VkSwapchainKHR handle;
	VkSurfaceFormatKHR image_format;
    u8 max_frames_in_flight;
    u32 image_count;

	std::vector<VkImage> images;
	std::vector<VkImageView> views;
	VkExtent2D extent;

    private:
    VulkanDevice* _device;
    VkSurfaceKHR* _surface;
    VkAllocationCallbacks* _allocator;
    b8 _create(u32 width, u32 height); 
    void _destroy();
};

}