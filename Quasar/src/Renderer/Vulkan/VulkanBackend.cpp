#include "VulkanBackend.h"

#include <Math/Math.h>

namespace Quasar::Renderer
{
    std::vector<const char*> get_required_extensions();

    b8 Backend::init(String &app_name, Window *main_window)
    {
        auto extensions = get_required_extensions();
        context = std::make_unique<VulkanContext>(extensions);
        return true;
    }

    void Backend::shutdown()
    {
    }

    void Backend::resize(u32 width, u32 height)
    {
    }

    b8 Backend::render()
    {
        u32 width = 800; 
        u32 height = 600; // TODO: get from state
        context->_extent = vk::Extent2D{width, height};
        context->_device->waitIdle();

        // Create an offscreen image to render the scene into.
        const auto offscreen_image = context->_device->createImageUnique({
            {},
            vk::ImageType::e2D,
            context->_image_format,
            vk::Extent3D{width, height, 1},
            1,
            1,
            context->_msaa_samples,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive,
        });
        const auto image_mem_reqs = context->_device->getImageMemoryRequirements(offscreen_image.get());
        const auto offscreen_image_memory = context->_device->allocateMemoryUnique({image_mem_reqs.size, context->find_memory_type(image_mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)});
        context->_device->bindImageMemory(offscreen_image.get(), offscreen_image_memory.get(), 0);
        const auto offscreen_image_view = context->_device->createImageViewUnique({{}, offscreen_image.get(), vk::ImageViewType::e2D, context->_image_format, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});

        context->ResolveImage = context->_device->createImageUnique({
            {},
            vk::ImageType::e2D,
            context->_image_format,
            vk::Extent3D{width, height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1, // Single-sampled resolve image.
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive,
        });

        const auto resolve_image_mem_reqs = context->_device->getImageMemoryRequirements(context->ResolveImage.get());
        context->ResolveImageMemory = context->_device->allocateMemoryUnique({resolve_image_mem_reqs.size, context->find_memory_type(resolve_image_mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)});
        context->_device->bindImageMemory(context->ResolveImage.get(), context->ResolveImageMemory.get(), 0);
        context->ResolveImageView = context->_device->createImageViewUnique({{}, context->ResolveImage.get(), vk::ImageViewType::e2D, context->_image_format, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});

        const std::array image_views{*offscreen_image_view, *context->ResolveImageView};
        const auto framebuffer = context->_device->createFramebufferUnique({{}, context->_render_pass.get(), image_views, width, height, 1});

        const auto &command_buffer = context->_command_buffers[0];
        const vk::Viewport viewport{0.f, 0.f, float(width), float(height), 0.f, 1.f};
        const vk::Rect2D scissor{{0, 0}, context->_extent};
        command_buffer->begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        command_buffer->setViewport(0, {viewport});
        command_buffer->setScissor(0, {scissor});

        const vk::ImageMemoryBarrier barrier{
            {},
            {},
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            context->ResolveImage.get(),
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::DependencyFlags{},
            0, nullptr, // No memory barriers.
            0, nullptr, // No buffer memory barriers.
            1, &barrier // 1 image memory barrier.
        );

        const vk::ClearValue clear_value{0.f};
        command_buffer->beginRenderPass({context->_render_pass.get(), framebuffer.get(), vk::Rect2D{{0, 0}, context->_extent}, 1, &clear_value}, vk::SubpassContents::eInline);
        command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *context->_graphics_pipeline);
        command_buffer->draw(3, 1, 0, 0);
        command_buffer->endRenderPass();
        command_buffer->end();

        vk::SubmitInfo submit;
        submit.setCommandBuffers(command_buffer.get());
        context->_queue.submit(submit);
        context->_device->waitIdle();

        return true;
    }

// -----------------------------------------------------------------//
//                       Helper Functions                           //
// -----------------------------------------------------------------//

    std::vector<const char*> get_required_extensions() {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> requiredExtensions;
        for(uint32_t i = 0; i < glfwExtensionCount; i++) {
            requiredExtensions.emplace_back(glfwExtensions[i]);
        }
    #ifdef QS_PLATFORM_APPLE
        requiredExtensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    #endif
    #ifdef QS_DEBUG 
            requiredExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    #endif
        return requiredExtensions;
    }

} // namespace Quasa::Vulkan
