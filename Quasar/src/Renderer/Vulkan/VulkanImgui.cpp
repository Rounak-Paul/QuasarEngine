#include "VulkanImgui.h"

namespace Quasar::Renderer {
    static void check_vk_result(VkResult err)
    {
        if (err == 0)
            return;
        fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
        if (err < 0)
            abort();
    }

    b8 vulkan_imgui_init(VulkanContext* context, Window* window)
    {
        context->imgui_context = ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
        ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
        ImGui::GetIO().ConfigViewportsNoTaskBarIcon = false;

        ImGui::GetIO().Fonts->AddFontDefault();

        // Setup Dear ImGui style
        // ImGui::StyleColorsClassic();
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();

        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        auto err = vkCreateDescriptorPool(context->device.logical_device, &pool_info, context->allocator, &context->imgui_descriptorpool);
        VK_CHECK(err);

        // Setup Platform/Renderer backends
        if(!ImGui_ImplGlfw_InitForVulkan(window->get_GLFWwindow(), true)) {
            return false;
        };
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = context->instance;
        init_info.PhysicalDevice = context->device.physical_device;
        init_info.Device = context->device.logical_device;
        init_info.QueueFamily = context->device.graphics_queue_index;
        init_info.Queue = context->device.graphics_queue;
        init_info.PipelineCache = nullptr;
        init_info.DescriptorPool = context->imgui_descriptorpool;
        init_info.RenderPass = context->renderpass;
        init_info.Subpass = 0;
        init_info.MinImageCount = 3;
        init_info.ImageCount = context->swapchain.image_count;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = context->allocator;
        init_info.CheckVkResultFn = check_vk_result;
        if(!ImGui_ImplVulkan_Init(&init_info)) {
            return false;
        };
        return true;
    }
    void vulkan_imgui_shutdown(VulkanContext *context)
    {
        auto err = vkDeviceWaitIdle(context->device.logical_device);
        check_vk_result(err);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(context->device.logical_device, context->imgui_descriptorpool, nullptr);
    }
    void vulkan_imgui_render()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto gui_in_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);

        b8 demo_window = true;
        ImGui::ShowDemoWindow(&demo_window);
        
        ImGui::Render();
    }
    void vulkan_imgui_post_render()
    {
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}