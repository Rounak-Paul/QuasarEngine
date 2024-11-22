#include "VulkanSwapchain.h"
#include "VulkanDevice.h"

namespace Quasar::Renderer
{
b8 _create(VulkanContext* context, VkExtent2D swapchain_extent, VulkanSwapchain* swapchain);
void _destroy(VulkanContext* context, VulkanSwapchain* swapchain);

b8 vulkan_swapchain_create(VulkanContext *context, u32 width, u32 height, VulkanSwapchain *out_swapchain)
{
    if (context && out_swapchain) {
        return _create(context, {width, height}, out_swapchain);
    }
    return false;
}

void vulkan_swapchain_recreate(VulkanContext *context, u32 width, u32 height, VulkanSwapchain *swapchain)
{
    vkDeviceWaitIdle(context->device.logical_device);
    _destroy(context, swapchain);
    _create(context, {width, height}, swapchain);
}

void vulkan_swapchain_destroy(VulkanContext *context, VulkanSwapchain *swapchain)
{
    _destroy(context, swapchain);
}

b8 vulkan_swapchain_acquire_next_image_index(VulkanContext *context, VulkanSwapchain *swapchain, u64 timeout_ns, VkSemaphore image_available_semaphore, VkFence fence, u32 *out_image_index)
{
    return true;
}

void vulkan_swapchain_present(VulkanContext *context, VulkanSwapchain *swapchain, VkQueue present_queue, VkSemaphore render_complete_semaphore, u32 present_image_index)
{
}

b8 _create(VulkanContext* context, VkExtent2D swapchain_extent, VulkanSwapchain* swapchain) {
    // Choose a swap surface format.
    b8 found = false;
    for (u32 i = 0; i < context->device.swapchain_support.format_count; ++i) {
        VkSurfaceFormatKHR format = context->device.swapchain_support.formats[i];
        // Preferred formats
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapchain->format = format;
            found = true;
            break;
        }
    }

    if (!found) {
        swapchain->format = context->device.swapchain_support.formats[0];
    }

    // VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
    // VK_PRESENT_MODE_MAILBOX_KHR = 1,
    // VK_PRESENT_MODE_FIFO_KHR = 2,
    // VK_PRESENT_MODE_FIFO_RELAXED_KHR = 3,
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < context->device.swapchain_support.present_mode_count; ++i) {
        VkPresentModeKHR mode = context->device.swapchain_support.present_modes[i];
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = mode;
            break;
        }
    }

    // Requery swapchain support.
    vulkan_device_query_swapchain_support(
        context->device.physical_device,
        context->surface,
        &context->device.swapchain_support);

    // Swapchain extent
    if (context->device.swapchain_support.capabilities.currentExtent.width != UINT32_MAX) {
        swapchain_extent = context->device.swapchain_support.capabilities.currentExtent;
    }

    // Clamp to the value allowed by the GPU.
    VkExtent2D min = context->device.swapchain_support.capabilities.minImageExtent;
    VkExtent2D max = context->device.swapchain_support.capabilities.maxImageExtent;
    swapchain_extent.width = QS_CLAMP(swapchain_extent.width, min.width, max.width);
    swapchain_extent.height = QS_CLAMP(swapchain_extent.height, min.height, max.height);

    u32 image_count = context->device.swapchain_support.capabilities.minImageCount + 1;
    if (context->device.swapchain_support.capabilities.maxImageCount > 0 && image_count > context->device.swapchain_support.capabilities.maxImageCount) {
        image_count = context->device.swapchain_support.capabilities.maxImageCount;
    }

    // Swapchain create info
    VkSwapchainCreateInfoKHR swapchain_create_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_create_info.surface = context->surface;
    swapchain_create_info.minImageCount = image_count;
    swapchain_create_info.imageFormat = swapchain->format.format;
    swapchain_create_info.imageColorSpace = swapchain->format.colorSpace;
    swapchain_create_info.imageExtent = swapchain_extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Setup the queue family indices
    if (context->device.graphics_queue_index != context->device.present_queue_index) {
        u32 queueFamilyIndices[] = {
            (u32)context->device.graphics_queue_index,
            (u32)context->device.present_queue_index};
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = 0;
        swapchain_create_info.pQueueFamilyIndices = 0;
    }

    swapchain_create_info.preTransform = context->device.swapchain_support.capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = 0;

    VK_CHECK(vkCreateSwapchainKHR(context->device.logical_device, &swapchain_create_info, context->allocator, &swapchain->handle));

    // Start with a zero frame index.
    context->current_frame = 0;

    // Images
    swapchain->image_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(context->device.logical_device, swapchain->handle, &swapchain->image_count, 0));
    swapchain->images.resize(swapchain->image_count);
    VkImage swapchain_images[32] {VK_NULL_HANDLE};
    vkGetSwapchainImagesKHR(context->device.logical_device, swapchain->handle, &swapchain->image_count, swapchain_images);
    for (u32 i=0; i < swapchain->image_count; i++) {
        if (swapchain_images[i] == VK_NULL_HANDLE) {
            LOG_ERROR("swapchain image can not be NULL");
            return false;
        }
        swapchain->images[i].handle = swapchain_images[i];
        swapchain->images[i].extent = {swapchain_extent.width, swapchain_extent.height, 0};
        swapchain->images[i].format = swapchain->format.format;
    }

    // Views
    for (u32 i = 0; i < swapchain->image_count; ++i) {
        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = swapchain->images[i].handle;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain->images[i].format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(context->device.logical_device, &view_info, context->allocator, &swapchain->images[i].view));
    }

    LOG_DEBUG("Swapchain created successfully.");
    return true;
}

void _destroy(VulkanContext* context, VulkanSwapchain* swapchain) {
    vkDeviceWaitIdle(context->device.logical_device);

    // for (u32 i = 0; i < context->swapchain.image_count; ++i) {
    //     VulkanImage::destroy(context, (vulkan_image*)swapchain->depth_textures[i].internal_data);
    //     swapchain->depth_textures[i].internal_data = 0;
    // }

    // Only destroy the views, not the images, since those are owned by the swapchain and are thus
    // destroyed when it is.
    for (u32 i = 0; i < swapchain->image_count; ++i) {
        vkDestroyImageView(context->device.logical_device, swapchain->images[i].view, context->allocator);
    }

    vkDestroySwapchainKHR(context->device.logical_device, swapchain->handle, context->allocator);
}

} // namespace Quasar::Renderer

