#include "RenderTarget.h"

namespace Quasar
{
    b8 RenderTarget::create()
    {
        auto context = QS_RENDERER.get_vkcontext();
        // Create Offscreen Image
        _offscreen_images.resize(MAX_FRAMES_IN_FLIGHT);
        for (auto& offscreen_image : _offscreen_images) {
            offscreen_image.create(
                context,
                { context->_extent.width, context->_extent.height },
                context->_image_format,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                context->_msaa_samples,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT
            );
        }

        // Create Resolve Image (for MSAA)
        _resolve_images.resize(MAX_FRAMES_IN_FLIGHT);
        for (auto& resolve_image : _resolve_images) {
            resolve_image.create(
                context,
                { context->_extent.width, context->_extent.height },
                context->_image_format,
                VK_IMAGE_TILING_OPTIMAL,

                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_SAMPLE_COUNT_1_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT
            );
        }

        VulkanCommandBuffer commandBuffer;
        commandBuffer.allocate_and_begin_single_use(context, context->_command_pool);

        for (auto& offscreen_image : _offscreen_images) {
            offscreen_image.transition_layout(
                context, 
                commandBuffer._handle,
                context->_image_format,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            );
        }

        for (auto& resolve_image : _resolve_images) {
            resolve_image.transition_layout(
                context, 
                commandBuffer._handle,
                context->_image_format,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }

        commandBuffer.end_single_use(context, context->_command_pool, context->_device.graphics_queue);

        _framebuffer.resize(MAX_FRAMES_IN_FLIGHT);
        for (u8 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkImageView attachments[] = { _offscreen_images[i]._image_view, _resolve_images[i]._image_view };

            VkFramebufferCreateInfo framebufferInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebufferInfo.renderPass = context->_render_pass;
            framebufferInfo.attachmentCount = 2;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = context->_extent.width;
            framebufferInfo.height = context->_extent.height;
            framebufferInfo.layers = 1;
            vkCreateFramebuffer(context->_device.logical_device, &framebufferInfo, nullptr, &_framebuffer[i]);
        }

        return true;
    }
    void RenderTarget::destroy()
    {
        auto context = QS_RENDERER.get_vkcontext();
        vkDeviceWaitIdle(context->_device.logical_device);

        for (auto& framebuffer : _framebuffer) {
            vkDestroyFramebuffer(context->_device.logical_device, framebuffer, nullptr);
        }

        for (auto& resolve_image : _resolve_images) {
            resolve_image.destroy(context);
        }

        for (auto& offscreen_image : _offscreen_images) {
            offscreen_image.destroy(context);
        }
    }
    void RenderTarget::resize(Math::extent extent)

    {
        auto context = QS_RENDERER.get_vkcontext();
        vkDeviceWaitIdle(context->_device.logical_device);
        destroy();
        create();
    }
    b8 RenderTarget::render(Math::extent extent, const VkClearColorValue &bg_color, u8 frame_index)
    {
        auto context = QS_RENDERER.get_vkcontext();
        if (_extent.width != extent.width || _extent.height != extent.height) {
            _extent = extent;
            resize(extent);
            return false;
        }

        VulkanCommandBuffer *commandBuffer = &context->_command_buffers[frame_index];

        // Set Viewport and Scissor
        VkViewport viewport = { 0, 0, float(extent.width), float(extent.height), 0.0f, 1.0f };
        vkCmdSetViewport(commandBuffer->_handle, 0, 1, &viewport);
        VkRect2D scissor = { {0, 0}, { extent.width, extent.height } };
        vkCmdSetScissor(commandBuffer->_handle, 0, 1, &scissor);

        // Begin Render Pass
        VkClearValue clearValue;
        clearValue.color = { bg_color.float32[0], bg_color.float32[1], bg_color.float32[2], bg_color.float32[3] };
        VkRenderPassBeginInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassInfo.renderPass = context->_render_pass;
        renderPassInfo.framebuffer = _framebuffer[frame_index];
        renderPassInfo.renderArea.extent = { extent.width, extent.height };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(commandBuffer->_handle, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Bind Pipeline and Draw
        vkCmdBindPipeline(commandBuffer->_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, context->_pipeline._graphics_pipeline);
        vkCmdDraw(commandBuffer->_handle, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer->_handle);

        _resolve_images[frame_index].transition_layout(
            context, 
            commandBuffer->_handle, 
            context->_image_format,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        return true;
    }
}