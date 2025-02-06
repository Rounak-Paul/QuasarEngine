#include "Scene.h"

namespace Quasar {
Scene::Scene()
{
}

b8 Scene::render(u32 width, u32 height, const VkClearColorValue &bg_color)
{
    auto context = QS_RENDERER.get_vkcontext();
    if (context->_extent.width == width && context->_extent.height == height) {
        return false;
    }
    context->_extent = vk::Extent2D{width, height};
    vkDeviceWaitIdle(context->_device.logical_device);

    // Create Offscreen Image
    VkImage offscreenImage;
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = context->_image_format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = context->_msaa_samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(context->_device.logical_device, &imageInfo, nullptr, &offscreenImage);

    // Allocate Memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(context->_device.logical_device, offscreenImage, &memReqs);
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context->find_memory_type(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory offscreenImageMemory;
    vkAllocateMemory(context->_device.logical_device, &allocInfo, nullptr, &offscreenImageMemory);
    vkBindImageMemory(context->_device.logical_device, offscreenImage, offscreenImageMemory, 0);

    // Create Image View
    VkImageView offscreenImageView;
    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = offscreenImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = context->_image_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(context->_device.logical_device, &viewInfo, nullptr, &offscreenImageView);

    // Create Resolve Image (for MSAA)
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    vkCreateImage(context->_device.logical_device, &imageInfo, nullptr, &resolveImage);
    vkGetImageMemoryRequirements(context->_device.logical_device, resolveImage, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    vkAllocateMemory(context->_device.logical_device, &allocInfo, nullptr, &resolveImageMemory);
    vkBindImageMemory(context->_device.logical_device, resolveImage, resolveImageMemory, 0);

    // Create Image View
    VkImageViewCreateInfo resolve_view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    resolve_view_info.image = resolveImage;
    resolve_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    resolve_view_info.format = context->_image_format;
    resolve_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    resolve_view_info.subresourceRange.levelCount = 1;
    resolve_view_info.subresourceRange.layerCount = 1;
    vkCreateImageView(context->_device.logical_device, &resolve_view_info, nullptr, &resolveImageView);

    // Create Framebuffer
    VkImageView attachments[] = { offscreenImageView, resolveImageView };
    VkFramebuffer framebuffer;
    VkFramebufferCreateInfo framebufferInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass = context->_render_pass;
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    vkCreateFramebuffer(context->_device.logical_device, &framebufferInfo, nullptr, &framebuffer);

    // Begin Command Buffer
    VkCommandBuffer commandBuffer = context->_command_buffers[frame_index];
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Set Viewport and Scissor
    VkViewport viewport = { 0, 0, float(width), float(height), 0.0f, 1.0f };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor = { {0, 0}, { width, height } };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Image Memory Barrier for Resolve Image
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image = resolveImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Begin Render Pass
    VkClearValue clearValue;
    clearValue.color = { bg_color.float32[0], bg_color.float32[1], bg_color.float32[2], bg_color.float32[3] };
    VkRenderPassBeginInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = context->_render_pass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.extent = { width, height };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind Pipeline and Draw
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->_pipeline._graphics_pipeline);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    // Submit Command Buffer
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(context->_device.graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);

    vkDeviceWaitIdle(context->_device.logical_device);
    frame_index = (frame_index + 1) % context->_command_buffers.size();

    return true;
}
}