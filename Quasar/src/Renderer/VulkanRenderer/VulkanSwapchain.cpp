#include "VulkanSwapchain.h"
#include "VulkanTypes.h"

namespace Quasar::Vulkan 
{

b8 VulkanSwapchain::_create(u32 width, u32 height) {
    extent = {width, height};

    // Choose a swap surface format.
    b8 found = false;
    for (u32 i = 0; i < _device->swapchain_support.format_count; ++i) {
        VkSurfaceFormatKHR format = _device->swapchain_support.formats[i];
        // Preferred formats
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            image_format = format;
            found = true;
            break;
        }
    }

    if (!found) {
        image_format = _device->swapchain_support.formats[0];
    }

    // VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
    // VK_PRESENT_MODE_MAILBOX_KHR = 1,
    // VK_PRESENT_MODE_FIFO_KHR = 2,
    // VK_PRESENT_MODE_FIFO_RELAXED_KHR = 3,
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < _device->swapchain_support.present_mode_count; ++i) {
        VkPresentModeKHR mode = _device->swapchain_support.present_modes[i];
        if (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
            present_mode = mode;
            break;
        }
    }

    // Requery swapchain support.
    VulkanDevice::query_swapchain_support(
        _device->physical_device,
        *_surface,
        &_device->swapchain_support);

    // Swapchain extent
    if (_device->swapchain_support.capabilities.currentExtent.width != UINT32_MAX) {
        extent = _device->swapchain_support.capabilities.currentExtent;
    }

    // Clamp to the value allowed by the GPU.
    VkExtent2D min = _device->swapchain_support.capabilities.minImageExtent;
    VkExtent2D max = _device->swapchain_support.capabilities.maxImageExtent;
    extent.width = QS_CLAMP(extent.width, min.width, max.width);
    extent.height = QS_CLAMP(extent.height, min.height, max.height);

    u32 image_count = _device->swapchain_support.capabilities.minImageCount + 1;
    if (_device->swapchain_support.capabilities.maxImageCount > 0 && image_count > _device->swapchain_support.capabilities.maxImageCount) {
        image_count = _device->swapchain_support.capabilities.maxImageCount;
    }

    max_frames_in_flight = image_count - 1;

    // Swapchain create info
    VkSwapchainCreateInfoKHR swapchain_create_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_create_info.surface = *_surface;
    swapchain_create_info.minImageCount = image_count;
    swapchain_create_info.imageFormat = image_format.format;
    swapchain_create_info.imageColorSpace = image_format.colorSpace;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // Setup the queue family indices
    if (_device->graphics_queue_index != _device->present_queue_index) {
        u32 queueFamilyIndices[] = {
            (u32)_device->graphics_queue_index,
            (u32)_device->present_queue_index};
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = 0;
        swapchain_create_info.pQueueFamilyIndices = 0;
    }

    swapchain_create_info.preTransform = _device->swapchain_support.capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = 0;

    VK_CHECK(vkCreateSwapchainKHR(_device->logical_device, &swapchain_create_info, _allocator, &handle));

    // Images
    image_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(_device->logical_device, handle, &image_count, 0));
    images.resize(image_count);
    views.resize(image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(_device->logical_device, handle, &image_count, images.data()));

    // Views
    for (u32 i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = image_format.format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(_device->logical_device, &view_info, _allocator, &views[i]));
    }

    LOG_DEBUG("Swapchain created successfully.");
    return true;
}

void VulkanSwapchain::_destroy()
{
    vkDeviceWaitIdle(_device->logical_device);
    for (auto view : views) {
        vkDestroyImageView(_device->logical_device, view, nullptr);
    }
    vkDestroySwapchainKHR(_device->logical_device, handle, nullptr);
}

b8 VulkanSwapchain::create(VulkanDevice* device, VkSurfaceKHR* surface, u32 width, u32 height, VkAllocationCallbacks *allocator)
{
    _device = device;
    _surface = surface;
    _allocator = allocator;

    if (!_create(width, height)) {
        LOG_ERROR("Failed to create Swapchain!")
        return false;
    }
    return true;
}
void VulkanSwapchain::destroy()
{
    _destroy();
}
b8 VulkanSwapchain::recreate(u32 width, u32 height)
{
    _destroy();
    if (!_create(width, height)) {
        LOG_ERROR("Failed to recreate Swapchain!")
        return false;
    }
    return true;
}
}