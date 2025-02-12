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

        vertices.resize(4);
        vertices[0] = {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}};
        vertices[1] = {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}};
        vertices[2] = {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}};
        vertices[3] = {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}};

        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);
        indices.push_back(2);
        indices.push_back(3);
        indices.push_back(0);

        VulkanBuffer::upload_data<Math::Vertex>(&context->vertex_buffer, vertices);
        VulkanBuffer::upload_data<u32>(&context->index_buffer, indices);

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
    void RenderTarget::resize(Math::Extent extent)

    {
        auto context = QS_RENDERER.get_vkcontext();
        vkDeviceWaitIdle(context->_device.logical_device);
        destroy();
        create();
    }
    b8 RenderTarget::render(Math::Extent extent, const VkClearColorValue &bg_color, u8 frame_index)
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
        {
            // Bind Pipeline and Draw
            vkCmdBindPipeline(commandBuffer->_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, context->_pipeline._pipeline);
            VkBuffer vertexBuffers[] = {context->vertex_buffer._buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer->_handle, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer->_handle, context->index_buffer._buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(commandBuffer->_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, context->_pipeline._pipeline_layout, 0, 1, &context->_pipeline._descriptor_sets[frame_index], 0, nullptr);
            vkCmdDrawIndexed(commandBuffer->_handle, static_cast<uint32_t>(indices.get_size()), 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(commandBuffer->_handle);

        _resolve_images[frame_index].transition_layout(
            context, 
            commandBuffer->_handle, 
            context->_image_format,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        updateUniformBuffer(frame_index);

        return true;
    }
    void RenderTarget::updateUniformBuffer(u32 index)
    {
        auto context = QS_RENDERER.get_vkcontext();
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        Math::UniformBufferObject ubo{};
        ubo.model = Math::Mat4::rotation(time * Math::PI / 2, Math::Vec3(0.0f, 0.0f, 1.0f));
        ubo.view = Math::Mat4::look_at(Math::Vec3(2.0f, 2.0f, 2.0f), Math::Vec3(0.0f, 0.0f, 0.0f), Math::Vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = Math::Mat4::perspective(Math::PI / 4, _extent.width / (f32) _extent.height, 0.1f, 10.0f);
        ubo.proj.mat[1][1] *= -1;

        memcpy(context->_pipeline._uniform_buffers_mapped[index], &ubo, sizeof(ubo));
    }
}