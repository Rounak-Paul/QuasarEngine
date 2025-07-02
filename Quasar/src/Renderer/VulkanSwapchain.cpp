#include "VulkanSwapchain.h"

namespace Quasar {

static void _create(VulkanDevice& device, VkSurfaceKHR surface, u32 width, u32 height, VulkanSwapchain* swapchain);
static void _destroy(VulkanDevice& device, VulkanSwapchain* swapchain);

void vulkan_swapchain_create(VulkanDevice& device, VkSurfaceKHR surface, u32 width, u32 height, VulkanSwapchain& swapchain)
{
    _create(device, surface, width, height, &swapchain);
}

void vulkan_swapchain_destroy(VulkanDevice& device, VulkanSwapchain& swapchain)
{
    _destroy(device, &swapchain);
}

void vulkan_swapchain_recreate(VulkanDevice &device, VkSurfaceKHR surface, u32 width, u32 height, VulkanSwapchain &swapchain)
{
    _destroy(device, &swapchain);
    _create(device, surface, width, height, &swapchain);
}

static void _create(VulkanDevice& device, VkSurfaceKHR surface, u32 width, u32 height, VulkanSwapchain* swapchain) {
    VkExtent2D swapchain_extent = {width, height};

    // Choose a swap surface format.
    b8 found = false;
    for (u32 i = 0; i < device.swapchain_support.format_count; ++i) {
        VkSurfaceFormatKHR format = device.swapchain_support.formats[i];
        // Preferred formats
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapchain->image_format = format;
            found = true;
            break;
        }
    }

    if (!found) {
        swapchain->image_format = device.swapchain_support.formats[0];
    }

    // VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
    // VK_PRESENT_MODE_MAILBOX_KHR = 1,
    // VK_PRESENT_MODE_FIFO_KHR = 2,
    // VK_PRESENT_MODE_FIFO_RELAXED_KHR = 3,
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < device.swapchain_support.present_mode_count; ++i) {
        VkPresentModeKHR mode = device.swapchain_support.present_modes[i];
        if (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
            present_mode = mode;
            break;
        }
    }

    // Requery swapchain support.
    query_swapchain_support(
        device.physical_device,
        surface,
        &device.swapchain_support);

    // Swapchain extent
    if (device.swapchain_support.capabilities.currentExtent.width != UINT32_MAX) {
        swapchain_extent = device.swapchain_support.capabilities.currentExtent;
    }

    // Clamp to the value allowed by the GPU.
    VkExtent2D min = device.swapchain_support.capabilities.minImageExtent;
    VkExtent2D max = device.swapchain_support.capabilities.maxImageExtent;
    swapchain_extent.width = QS_CLAMP(swapchain_extent.width, min.width, max.width);
    swapchain_extent.height = QS_CLAMP(swapchain_extent.height, min.height, max.height);

    u32 image_count = device.swapchain_support.capabilities.minImageCount + 1;
    if (device.swapchain_support.capabilities.maxImageCount > 0 && image_count > device.swapchain_support.capabilities.maxImageCount) {
        image_count = device.swapchain_support.capabilities.maxImageCount;
    }

    // Swapchain create info
    VkSwapchainCreateInfoKHR swapchain_create_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_create_info.surface = surface;
    swapchain_create_info.minImageCount = image_count;
    swapchain_create_info.imageFormat = swapchain->image_format.format;
    swapchain_create_info.imageColorSpace = swapchain->image_format.colorSpace;
    swapchain_create_info.imageExtent = swapchain_extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Setup the queue family indices
    if (device.graphics_queue_index != device.present_queue_index) {
        u32 queueFamilyIndices[] = {
            (u32)device.graphics_queue_index,
            (u32)device.present_queue_index};
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = 0;
        swapchain_create_info.pQueueFamilyIndices = 0;
    }

    swapchain_create_info.preTransform = device.swapchain_support.capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(device.logical_device, &swapchain_create_info, nullptr, &swapchain->handle));

    // Images
    u32 swapchain_image_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device.logical_device, swapchain->handle, &swapchain_image_count, 0));
    if (swapchain->images.empty()) {
        swapchain->images.resize(swapchain_image_count);
    }
    VK_CHECK(vkGetSwapchainImagesKHR(device.logical_device, swapchain->handle, &swapchain_image_count, swapchain->images.data()));

    // Views
    if (swapchain->views.empty()) {
        swapchain->views.resize(swapchain_image_count);
    }
    for (u32 i = 0; i < swapchain_image_count; ++i) {
        auto& image = swapchain->images[i];
        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain->image_format.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(device.logical_device, &view_info, nullptr, &swapchain->views[i]));
    }

    LOG_DEBUG("Swapchain created successfully.");
}

static void _destroy(VulkanDevice& device, VulkanSwapchain* swapchain) {
    vkDeviceWaitIdle(device.logical_device);

    // Only destroy the views, not the images, since those are owned by the swapchain and are thus
    // destroyed when it is.
    for (auto& view : swapchain->views) {
        vkDestroyImageView(device.logical_device, view, nullptr);
    }

    vkDestroySwapchainKHR(device.logical_device, swapchain->handle, nullptr);
}
}