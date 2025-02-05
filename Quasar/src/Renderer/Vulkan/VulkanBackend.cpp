#include "VulkanBackend.h"
#include "VulkanCheckReslt.h"
#include <Math/Math.h>

#include <Gui/GuiFonts.h>
#include <Gui/GuiStyles.h>

#define IMGUI_UNLIMITED_FRAME_RATE

namespace Quasar
{
    std::vector<const char*> get_required_extensions();

    using namespace ImGui;

    static ImGui_ImplVulkanH_Window main_window_data;
    static u32 min_image_count = 3;

    b8 Backend::init(String &app_name, Window *main_window)
    {
        auto extensions = get_required_extensions();
        context = std::make_unique<VulkanContext>(extensions);
        if (!create_vulkan_surface(main_window->get_GLFWwindow())) {
            LOG_FATAL("Failed to create surface for rendering!!!")
        }

        auto extent = main_window->get_extent();
        ImGui_ImplVulkanH_Window *wd = &main_window_data;
        SetupVulkanWindow(wd, surface, extent.width, extent.height);

        // Setup ImGui context.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows

        // io.IniFilename = nullptr; // Disable ImGui's .ini file saving

        StyleColorsDark();
        // StyleColorsLight();
        customize_style();

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
        init_info.MinImageCount = min_image_count;
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
        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(Roboto_Medium, Roboto_Medium_size, 12.0f, &font_cfg);

        return true;
    }

    void Backend::shutdown()
    {
    }

    void Backend::resize(u32 width, u32 height)
    {
        context->_device->waitIdle();
        ImGui_ImplVulkan_SetMinImageCount(min_image_count);
        ImGui_ImplVulkanH_CreateOrResizeWindow(context->_instance.get(), context->_physical_device, context->_device.get(), &main_window_data, context->_queue_family, nullptr, width, height, min_image_count);
        main_window_data.FrameIndex = 0;
    }

    b8 Backend::frame_begin()
    {
        ImGui_ImplVulkanH_Window *wd = &main_window_data;
        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        return true;
    }

    b8 Backend::frame_end() {
        // FIXME: remove demo window
        static b8 demo_is_visible = true;
        if (demo_is_visible) ShowDemoWindow(&demo_is_visible);

        ImGui_ImplVulkanH_Window *wd = &main_window_data;
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
        IM_ASSERT(min_image_count >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(context->_instance.get(), context->_physical_device, context->_device.get(), wd, context->_queue_family, nullptr, width, height, min_image_count);
    }

    void Backend::CleanupVulkanWindow() {
        ImGui_ImplVulkanH_DestroyWindow(context->_instance.get(), context->_device.get(), &main_window_data, nullptr);
    }

    void Backend::FrameRender(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data) {
        VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        const VkResult err = vkAcquireNextImageKHR(context->_device.get(), wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
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
            return;
        }
        VK_CHECK_CALL(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores.
    }

} // namespace Quasa::Vulkan
