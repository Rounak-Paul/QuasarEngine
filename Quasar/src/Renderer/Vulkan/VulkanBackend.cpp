#include "VulkanBackend.h"
#include "VulkanCheckReslt.h"
#include <Math/Math.h>

namespace Quasar::Renderer
{
    std::vector<const char*> get_required_extensions();

    using namespace ImGui;
    static vk::ClearColorValue ImVec4ToClearColor(const ImVec4 &v) { return {v.x, v.y, v.z, v.w}; }

    // #define IMGUI_UNLIMITED_FRAME_RATE

    static ImGui_ImplVulkanH_Window MainWindowData;
    static u32 MinImageCount = 2;
    static bool SwapChainRebuild = false;

    b8 Backend::init(String &app_name, Window *main_window)
    {
        auto extensions = get_required_extensions();
        context = std::make_unique<VulkanContext>(extensions);
        if (!create_vulkan_surface(main_window->get_GLFWwindow())) {
            LOG_FATAL("Failed to create surface for rendering!!!")
        }

        auto extent = main_window->get_extent();
        ImGui_ImplVulkanH_Window *wd = &MainWindowData;
        SetupVulkanWindow(wd, surface, extent.width, extent.height);

        // Setup ImGui context.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
        io.ConfigFlags |= ImGuiDockNodeFlags_PassthruCentralNode;
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows

        // io.IniFilename = nullptr; // Disable ImGui's .ini file saving

        StyleColorsDark();
        // StyleColorsLight();

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForVulkan(main_window->get_GLFWwindow(), true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = context->_instance.get();
        init_info.PhysicalDevice = context->_physical_device;
        init_info.Device = context->_device.get();
        init_info.QueueFamily = context->_queue_family;
        init_info.Queue = context->_queue;
        init_info.PipelineCache = context->_pipeline_cache.get();
        init_info.DescriptorPool = context->_descriptor_pool.get();
        init_info.RenderPass = wd->RenderPass;
        init_info.Subpass = 0;
        init_info.MinImageCount = MinImageCount;
        init_info.ImageCount = wd->ImageCount;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = CheckVk;
        ImGui_ImplVulkan_Init(&init_info);

        // Load fonts.
        // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use PushFont()/PopFont() to select them.
        // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
        // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
        // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
        // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
        // - Read 'docs/FONTS.md' for more instructions and details.
        // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
        // io.Fonts->AddFontDefault();
        // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
        // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
        // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
        // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
        // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
        // IM_ASSERT(font != nullptr);

        return true;
    }

    void Backend::shutdown()
    {
    }

    void Backend::resize(u32 width, u32 height)
    {
    }

    void Backend::update()
    {
        ImGui_ImplVulkanH_Window *wd = &MainWindowData;

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto dockspace_id = DockSpaceOverViewport();

        if (Windows.ImGuiDemo.Visible) ShowDemoWindow(&Windows.ImGuiDemo.Visible);

        if (Windows.Scene.Visible) {
            PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            Begin(Windows.Scene.Name, &Windows.Scene.Visible);
            const auto content_region = GetContentRegionAvail();
            if (render(content_region.x, content_region.y, ImVec4ToClearColor(GetStyleColorVec4(ImGuiCol_WindowBg)))) {
                if (MainSceneDescriptorSet) {
                    ImGui_ImplVulkan_RemoveTexture(MainSceneDescriptorSet);
                }
                MainSceneDescriptorSet = ImGui_ImplVulkan_AddTexture(context->_texture_sampler.get(), context->ResolveImageView.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            ImTextureID textureID = (ImTextureID) static_cast<VkDescriptorSet>(MainSceneDescriptorSet);
            Image(textureID, ImGui::GetContentRegionAvail());
            End();
            PopStyleVar();
        }

        // Rendering
        ImGui::Render();
        ImDrawData *draw_data = GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized) {
            static const ImVec4 clear_color{0.45f, 0.55f, 0.60f, 1.f};
            wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            wd->ClearValue.color.float32[3] = clear_color.w;
            FrameRender(wd, draw_data);
            FramePresent(wd);
        }
    }

    b8 Backend::render(u32 width, u32 height, const vk::ClearColorValue &bg_color)
    {
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

        const vk::ClearValue clear_value{bg_color};
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

    b8 Backend::create_vulkan_surface(GLFWwindow* window)
	{
		auto res = glfwCreateWindowSurface(context->_instance.get(), window, nullptr, &surface);
		if (res != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create Window Surface, VkResult: %d", res);
			return false;
		}
		return true;
	}

    // All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
    // Your real engine/app may not use them.
    void Backend::SetupVulkanWindow(ImGui_ImplVulkanH_Window *wd, vk::SurfaceKHR surface, int width, int height) {
        wd->Surface = surface;

        // Check for WSI support
        auto res = context->_physical_device.getSurfaceSupportKHR(context->_queue_family, wd->Surface);
        if (res != VK_TRUE) throw std::runtime_error("Error no WSI support on physical device 0\n");

        // Select surface format.
        const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(context->_physical_device, wd->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

        // Select present mode.
    #ifdef IMGUI_UNLIMITED_FRAME_RATE
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
    #else
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
    #endif
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(context->_physical_device, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
        // printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

        // Create SwapChain, RenderPass, Framebuffer, etc.
        IM_ASSERT(MinImageCount >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(context->_instance.get(), context->_physical_device, context->_device.get(), wd, context->_queue_family, nullptr, width, height, MinImageCount);
    }

    void Backend::CleanupVulkanWindow() {
        ImGui_ImplVulkanH_DestroyWindow(context->_instance.get(), context->_device.get(), &MainWindowData, nullptr);
    }

    void Backend::FrameRender(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data) {
        VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        const VkResult err = vkAcquireNextImageKHR(context->_device.get(), wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            SwapChainRebuild = true;
            return;
        }
        VK_CHECK_CALL(err);

        ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
        {
            VK_CHECK_CALL(vkWaitForFences(context->_device.get(), 1, &fd->Fence, VK_TRUE, UINT64_MAX)); // wait indefinitely instead of periodically checking
            VK_CHECK_CALL(vkResetFences(context->_device.get(), 1, &fd->Fence));
        }
        {
            VK_CHECK_CALL(vkResetCommandPool(context->_device.get(), fd->CommandPool, 0));
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK_CALL(vkBeginCommandBuffer(fd->CommandBuffer, &info));
        }
        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = wd->RenderPass;
            info.framebuffer = fd->Framebuffer;
            info.renderArea.extent.width = wd->Width;
            info.renderArea.extent.height = wd->Height;
            info.clearValueCount = 1;
            info.pClearValues = &wd->ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        // Record dear imgui primitives into command buffer
        ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

        // Submit command buffer
        vkCmdEndRenderPass(fd->CommandBuffer);
        {
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.waitSemaphoreCount = 1;
            info.pWaitSemaphores = &image_acquired_semaphore;
            info.pWaitDstStageMask = &wait_stage;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &fd->CommandBuffer;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &render_complete_semaphore;

            VK_CHECK_CALL(vkEndCommandBuffer(fd->CommandBuffer));
            VK_CHECK_CALL(vkQueueSubmit(context->_queue, 1, &info, fd->Fence));
        }
    }

    void Backend::FramePresent(ImGui_ImplVulkanH_Window *wd) {
        if (SwapChainRebuild) return;

        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &wd->Swapchain;
        info.pImageIndices = &wd->FrameIndex;
        VkResult err = vkQueuePresentKHR(context->_queue, &info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            SwapChainRebuild = true;
            return;
        }
        VK_CHECK_CALL(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores.
    }

} // namespace Quasa::Vulkan
