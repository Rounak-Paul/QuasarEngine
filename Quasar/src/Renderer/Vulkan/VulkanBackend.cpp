#include "VulkanBackend.h"
#include <Math/Math.h>

#include <Gui/GuiFonts.h>
#include <Gui/GuiStyles.h>

// #define IMGUI_UNLIMITED_FRAME_RATE

namespace Quasar
{
    using namespace ImGui;

    static ImGui_ImplVulkanH_Window main_window_data;
    static u32 min_image_count = 3;

    b8 Backend::init(String &app_name, Window *main_window)
    {
        _context.create(main_window->get_GLFWwindow());

        auto extent = main_window->get_extent();
        _context._extent = {extent.width, extent.height};
        ImGui_ImplVulkanH_Window *wd = &main_window_data;
        vulkan_window_setup(wd, _context._surface, extent.width, extent.height);

        // Setup ImGui context.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows

        // io.IniFilename = nullptr; // Disable ImGui's .ini file saving

        StyleColorsDark();
        // StyleColorsLight();
        customize_style();

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForVulkan(main_window->get_GLFWwindow(), true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = _context._instance;
        init_info.PhysicalDevice = _context._device.physical_device;
        init_info.Device = _context._device.logical_device;
        init_info.QueueFamily = _context._device.graphics_queue_index;
        init_info.Queue = _context._device.graphics_queue;
        init_info.PipelineCache = _context._pipeline_cache;
        init_info.DescriptorPool = _context._descriptor_pool;
        init_info.RenderPass = wd->RenderPass;
        init_info.Subpass = 0;
        init_info.MinImageCount = min_image_count;
        init_info.ImageCount = wd->ImageCount;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = check_vk_imgui;
        ImGui_ImplVulkan_Init(&init_info);

        // Load Fonts
        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(Roboto_Medium, Roboto_Medium_size, 12.0f, &font_cfg);

        return true;
    }

    void Backend::shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        vulkan_window_cleanup();
        _context.destroy();
    }

    void Backend::resize(u32 width, u32 height)
    {
        vkDeviceWaitIdle(_context._device.logical_device);
        ImGui_ImplVulkan_SetMinImageCount(min_image_count);
        ImGui_ImplVulkanH_CreateOrResizeWindow(_context._instance, _context._device.physical_device, _context._device.logical_device, &main_window_data, _context._device.graphics_queue_index, nullptr, width, height, min_image_count);
        main_window_data.FrameIndex = 0;
    }

    b8 Backend::frame_begin()
    {
        // Begin Command Buffer
        VulkanCommandBuffer *commandBuffer = &_context._command_buffers[_context._frame_index];
        commandBuffer->reset();
        commandBuffer->begin(false, false, false);

        return true;
    }

    b8 Backend::frame_end() {
        _context._command_buffers[_context._frame_index].end();

        // Submit Command Buffer
        VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &_context._command_buffers[_context._frame_index]._handle;
        vkQueueSubmit(_context._device.graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);

        vkDeviceWaitIdle(_context._device.logical_device);
        return true;
    }

    b8 Backend::imgui_frame_begin()
    {
        ImGui_ImplVulkanH_Window *wd = &main_window_data;
        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        return true;
    }

    b8 Backend::imgui_frame_end()
    {
        // FIXME: remove demo window
        static b8 demo_is_visible = true;
        if (demo_is_visible) ShowDemoWindow(&demo_is_visible);
        
        ImGui_ImplVulkanH_Window *wd = &main_window_data;
        // Rendering
        ImGui::Render();
        ImDrawData *draw_data = GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized) {
            static const ImVec4 clear_color{0.0f, 0.0f, 0.0f, 1.f};
            wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            wd->ClearValue.color.float32[3] = clear_color.w;
            frame_render(wd, draw_data);
            frame_present(wd);
        }

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        return true;
    }

// -----------------------------------------------------------------//
//                       Helper Functions                           //
// -----------------------------------------------------------------//

    // All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
    // Your real engine/app may not use them.
    void Backend::vulkan_window_setup(ImGui_ImplVulkanH_Window *wd, VkSurfaceKHR surface, int width, int height) {
        wd->Surface = surface;

        // Check for WSI support
        VkBool32 supported = VK_FALSE;
        VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(
            _context._device.physical_device,
            _context._device.graphics_queue_index,
            wd->Surface,
            &supported
        );
        if (res != VK_SUCCESS) throw std::runtime_error("Error no WSI support on physical device 0\n");

        // Select surface format.
        const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(_context._device.physical_device, wd->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

        // Select present mode.
    #ifdef IMGUI_UNLIMITED_FRAME_RATE
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
    #else
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
    #endif
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(_context._device.physical_device, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
        // printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

        // Create SwapChain, RenderPass, Framebuffer, etc.
        IM_ASSERT(min_image_count >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(_context._instance, _context._device.physical_device, _context._device.logical_device, wd, _context._device.graphics_queue_index, nullptr, width, height, min_image_count);
    }

    void Backend::vulkan_window_cleanup() {
        ImGui_ImplVulkanH_DestroyWindow(_context._instance, _context._device.logical_device, &main_window_data, nullptr);
    }

    void Backend::frame_render(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data) {
        VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        const VkResult err = vkAcquireNextImageKHR(_context._device.logical_device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            return;
        }
        VK_CALL(err);

        ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
        {
            VK_CALL(vkWaitForFences(_context._device.logical_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX)); // wait indefinitely instead of periodically checking
            VK_CALL(vkResetFences(_context._device.logical_device, 1, &fd->Fence));
        }
        {
            VK_CALL(vkResetCommandPool(_context._device.logical_device, fd->CommandPool, 0));
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

            VK_CALL(vkEndCommandBuffer(fd->CommandBuffer));
            VK_CALL(vkQueueSubmit(_context._device.graphics_queue, 1, &info, fd->Fence));
        }
    }

    void Backend::frame_present(ImGui_ImplVulkanH_Window *wd) {
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &wd->Swapchain;
        info.pImageIndices = &wd->FrameIndex;
        VkResult err = vkQueuePresentKHR(_context._device.graphics_queue, &info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            return;
        }
        VK_CHECK_CALL(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores.
    }

} // namespace Quasa::Vulkan
