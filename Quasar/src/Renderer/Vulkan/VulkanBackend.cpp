#include "VulkanBackend.h"
#include <Math/Math.h>

#include <Gui/GuiFonts.h>
#include <Gui/GuiStyles.h>

#include <Resources/Shader.h>

namespace Quasar
{
    using namespace ImGui;

    const std::vector<const char*> validationLayers = { 
        "VK_LAYER_KHRONOS_validation" 
        // ,"VK_LAYER_LUNARG_api_dump" // For all vulkan calls
    };

    VkSampleCountFlagBits GetMaxUsableSampleCount(VkPhysicalDevice physical_device);
    VkDebugUtilsMessengerEXT _debug_messenger;
    std::vector<const char*> get_required_extensions();
        void Setup_debug_messenger(VkInstance instance);
        
        static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) {
            switch (messageSeverity)
            {
                default:
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                    LOG_ERROR(pCallbackData->pMessage);
                    break;
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                    LOG_WARN(pCallbackData->pMessage);
                    break;
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                    LOG_INFO(pCallbackData->pMessage);
                    break;
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                    LOG_TRACE(pCallbackData->pMessage);
                    break;
            }
        return VK_FALSE;
    }

    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* p_debug_messenger) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT) 
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, p_debug_messenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    static ImGui_ImplVulkanH_Window main_window_data;
    static u32 min_image_count = MAX_FRAMES_IN_FLIGHT;

    b8 Backend::init(String &app_name, Window *main_window)
    {
        // Create Vulkan Context
        context_create(main_window->get_GLFWwindow());

        auto extent = main_window->get_extent();
        _context.extent = {extent.width, extent.height};
        ImGui_ImplVulkanH_Window *wd = &main_window_data;
        vulkan_window_setup(wd, _context.surface, extent.width, extent.height);

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
        init_info.Instance = _context.instance;
        init_info.PhysicalDevice = _context.device.physical_device;
        init_info.Device = _context.device.logical_device;
        init_info.QueueFamily = _context.device.graphics_queue_index;
        init_info.Queue = _context.device.graphics_queue;
        init_info.PipelineCache = _context.pipeline_cache;
        init_info.DescriptorPool = _context.imgui_descriptor_pool;
        init_info.RenderPass = wd->RenderPass;
        init_info.Subpass = 0;
        init_info.MinImageCount = min_image_count;
        init_info.ImageCount = wd->ImageCount;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = _context.allocator;
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
        context_destroy();
    }

    void Backend::resize(u32 width, u32 height)
    {
        _context.extent = {width, height};
        vkDeviceWaitIdle(_context.device.logical_device);
        ImGui_ImplVulkan_SetMinImageCount(min_image_count);
        ImGui_ImplVulkanH_CreateOrResizeWindow(_context.instance, _context.device.physical_device, _context.device.logical_device, &main_window_data, _context.device.graphics_queue_index, nullptr, width, height, min_image_count);
        main_window_data.FrameIndex = _context.frame_index;
    }

    b8 Backend::frame_begin()
    {
        vkWaitForFences(_context.device.logical_device, 1, &_context.inFlightFences[_context.frame_index], VK_TRUE, UINT64_MAX);

        // Begin Command Buffer
        VulkanCommandBuffer *command_buffer = &_context.command_buffers[_context.frame_index];
        command_buffer->reset();
        command_buffer->begin(false, false, false);

        return true;
    }

    b8 Backend::frame_end() {
        _context.command_buffers[_context.frame_index].end();

        // Submit Command Buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &_context.command_buffers[_context.frame_index]._handle;

        VkSemaphore signalSemaphores[] = {_context.renderFinishedSemaphores[_context.frame_index]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkResetFences(_context.device.logical_device, 1, &_context.inFlightFences[_context.frame_index]);
        vkQueueSubmit(_context.device.graphics_queue, 1, &submitInfo, _context.inFlightFences[_context.frame_index]);

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
b8 check_validation_layer_support() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name : validationLayers) {
        bool layerFound = false;

        for (const auto& layer_properties : available_layers) {
            if (strcmp(layer_name, layer_properties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            return false;
        }
    }
    return true;
}

b8 Backend::vulkan_surface_create(GLFWwindow *window)
{
    VK_CALL(glfwCreateWindowSurface(_context.instance, window, nullptr, &_context.surface));
    return true;
}

b8 Backend::context_create(GLFWwindow* window)
{
    _context.allocator = nullptr;

    #ifdef QS_DEBUG 
        if (!check_validation_layer_support()) {
            LOG_ERROR("validation layers requested, but not available!");
        }
    #endif

    // Get the currently-installed instance version. Not necessarily what the device
    // uses, though. Use this to create the instance though.
    u32 api_version = 0;
    vkEnumerateInstanceVersion(&api_version);
    _context.device.api_major = VK_VERSION_MAJOR(api_version);
    _context.device.api_minor = VK_VERSION_MINOR(api_version);
    _context.device.api_patch = VK_VERSION_PATCH(api_version);

    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "Quasar";
    app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.pEngineName = "Quasar Engine";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.apiVersion = VK_MAKE_API_VERSION(0, _context.device.api_major, _context.device.api_minor, _context.device.api_patch);

    VkInstanceCreateInfo createInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &app_info;

    auto extensions = get_required_extensions();

    #ifdef QS_PLATFORM_APPLE
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    #endif
    createInfo.enabledExtensionCount = extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    #ifdef QS_DEBUG
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
    #else
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = 0;
    #endif

    LOG_INFO("Creating Vulkan instance...");
    VK_CALL(vkCreateInstance(&createInfo, _context.allocator, &_context.instance));

    #ifdef QS_DEBUG
    Setup_debug_messenger(_context.instance);
    #endif

    // TODO: implement multi-threading.
    _multithreading_enabled = false;

    // Surface
    LOG_INFO("Creating Vulkan surface...");
    if (!vulkan_surface_create(window)) {
        LOG_FATAL("Failed to create platform surface!");
        return false;
    }

    // Device creation
    if (!_context.device.create(&_context)) {
        LOG_ERROR("Failed to create device!");
        return false;
    }

    // Create descriptor pool for IMGUI.
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 }  // Increased to 100
        };

        VkDescriptorPoolCreateInfo descriptor_pool_info = {};
        descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descriptor_pool_info.maxSets = 100;  // Match descriptor count
        descriptor_pool_info.poolSizeCount = 1;
        descriptor_pool_info.pPoolSizes = pool_sizes;

        VK_CALL(vkCreateDescriptorPool(_context.device.logical_device, &descriptor_pool_info, nullptr, &_context.imgui_descriptor_pool));
    }
    
    // Render multisampled into the offscreen image, then resolve into a single-sampled resolve image.
    _context.msaa_samples = GetMaxUsableSampleCount(_context.device.physical_device);

    // Define attachments.
    VkAttachmentDescription attachments[2] = {};

    // Multi-sampled offscreen image attachment.
    attachments[0].flags = 0;
    attachments[0].format = _context.image_format;
    attachments[0].samples = _context.msaa_samples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Single-sampled resolve attachment.
    attachments[1].flags = 0;
    attachments[1].format = _context.image_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Define attachment references.
    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolve_attachment_ref = {};
    resolve_attachment_ref.attachment = 1;
    resolve_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Define subpass.
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pResolveAttachments = &resolve_attachment_ref;

    // Add subpass dependencies
    VkSubpassDependency dependencies[2] = {};
    
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Update render pass creation info
    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 2;  // Added dependencies
    render_pass_info.pDependencies = dependencies;

    // Create render pass.
    VK_CALL(vkCreateRenderPass(_context.device.logical_device, &render_pass_info, nullptr, &_context.render_pass));

    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = _context.device.graphics_queue_index;

    VK_CALL(vkCreateCommandPool(_context.device.logical_device, &command_pool_info, nullptr, &_context.command_pool));

    _context.command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (auto& command_buffer : _context.command_buffers) {
        command_buffer.allocate(&_context, _context.command_pool, true);
    }

    // Create sampler
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = 16;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VK_CALL(vkCreateSampler(_context.device.logical_device, &sampler_info, nullptr, &_context.texture_sampler));

    _context.renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    _context.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(_context.device.logical_device, &semaphoreInfo, nullptr, &_context.renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(_context.device.logical_device, &fenceInfo, nullptr, &_context.inFlightFences[i]) != VK_SUCCESS) {

            LOG_ERROR("failed to create synchronization objects for a frame!");
            return false;
        }
    }

    {
        VkDeviceSize buffer_size = sizeof(Math::Vertex) * 1024;
        VulkanBufferCreateInfo buffer_info = {
            buffer_size, // size
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, // usage
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT // properties
        };
        _context.vertex_buffer.create(&_context, buffer_info);
    }
    {
        VkDeviceSize buffer_size = sizeof(Math::Vertex) * 1024 * 3;
        VulkanBufferCreateInfo buffer_info = {
            buffer_size, // size
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, // usage
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT // properties
        };
        _context.index_buffer.create(&_context, buffer_info);
    }

    return true;
}

void Backend::context_destroy()
{
    vkDeviceWaitIdle(_context.device.logical_device);

    _context.vertex_buffer.destroy();
    _context.index_buffer.destroy();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(_context.device.logical_device, _context.renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(_context.device.logical_device, _context.inFlightFences[i], nullptr);
    }

    if (_context.texture_sampler) {
        vkDestroySampler(_context.device.logical_device, _context.texture_sampler, _context.allocator);
        _context.texture_sampler = VK_NULL_HANDLE;
    }

    for (auto& command_buffer : _context.command_buffers) {
        command_buffer.free(&_context, _context.command_pool);
    }

    if (_context.command_pool) {
        vkDestroyCommandPool(_context.device.logical_device, _context.command_pool, _context.allocator);
        _context.command_pool = VK_NULL_HANDLE;
    }

    if (_context.render_pass) {
        vkDestroyRenderPass(_context.device.logical_device, _context.render_pass, _context.allocator);
        _context.render_pass = VK_NULL_HANDLE;
    }

    if (_context.imgui_descriptor_pool) {
        vkDestroyDescriptorPool(_context.device.logical_device, _context.imgui_descriptor_pool, _context.allocator);
        _context.imgui_descriptor_pool = VK_NULL_HANDLE;
    }

    #ifdef QS_DEBUG
    if (_debug_messenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(_context.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(_context.instance, _debug_messenger, _context.allocator);
        }
        _debug_messenger = VK_NULL_HANDLE;
    }
    #endif

    _context.device.destroy(&_context);

    if (_context.instance) {
        vkDestroyInstance(_context.instance, _context.allocator);
        _context.instance = VK_NULL_HANDLE;
    }
}

VulkanPipeline *Backend::pipeline_create(Shader* s, const VulkanPipelineConfig &config)
{
    VulkanPipeline* pipeline = (VulkanPipeline*)QSMEM.allocate(sizeof(VulkanPipeline));
    auto vk_shader = (VulkanShader*)s->_internal_data;

    u8 stage_count = s->_config.stage_count;
    DynamicArray<VkPipelineShaderStageCreateInfo> shader_stages;
    shader_stages.resize(stage_count);
    for (u8 i=0; i<stage_count; i++) {
        shader_stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stages[i].stage = s->_config.stages[i].stage;
        shader_stages[i].module = vk_shader->shader_modules[i];
        shader_stages[i].pName = s->_config.stages[i].entry_point.c_str();
    }
    return nullptr;
}

b8 Backend::shader_create(const ShaderConfig &config, Shader *s)
{
    s->_internal_data = QSMEM.allocate(sizeof(VulkanShader));
    auto vk_shader = (VulkanShader*)s->_internal_data;

    for (u8 i=0; i<config.stage_count; i++) {
        VkShaderModuleCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = config.stages[i].shader_code.get_size() * sizeof(u32);
        create_info.pCode = config.stages[i].shader_code.get_data();

        VkShaderModule shader_module;
        if (vkCreateShaderModule(_context.device.logical_device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
            LOG_ERROR("Failed to create shader module!");
            return false;
        }

        vk_shader->shader_modules.push_back(shader_module);
    }
    return true;
}

VkSampleCountFlagBits GetMaxUsableSampleCount(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_device, &props);

    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    
    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    
    return VK_SAMPLE_COUNT_1_BIT;
}

void Setup_debug_messenger(VkInstance instance) {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = vk_debug_callback;
    createInfo.pUserData = nullptr; // Optional

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &_debug_messenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

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

    void Backend::vulkan_window_setup(ImGui_ImplVulkanH_Window *wd, VkSurfaceKHR surface, int width, int height) {
        wd->Surface = surface;

        // Check for WSI support
        VkBool32 supported = VK_FALSE;
        VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(
            _context.device.physical_device,
            _context.device.graphics_queue_index,
            wd->Surface,
            &supported
        );
        if (res != VK_SUCCESS) throw std::runtime_error("Error no WSI support on physical device 0\n");

        // Select surface format.
        const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(_context.device.physical_device, wd->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

        // Select present mode.
    #ifdef IMGUI_UNLIMITED_FRAME_RATE
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
    #else
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
    #endif
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(_context.device.physical_device, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
        // printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

        // Create SwapChain, RenderPass, Framebuffer, etc.
        IM_ASSERT(min_image_count >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(_context.instance, _context.device.physical_device, _context.device.logical_device, wd, _context.device.graphics_queue_index, nullptr, width, height, min_image_count);
    }

    void Backend::vulkan_window_cleanup() {
        ImGui_ImplVulkanH_DestroyWindow(_context.instance, _context.device.logical_device, &main_window_data, nullptr);
    }

    void Backend::frame_render(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data) {
        VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

        VkSemaphore waitSemaphores[] = {_context.renderFinishedSemaphores[_context.frame_index], image_acquired_semaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

        wd->FrameIndex = _context.frame_index; // FIXME: any better way to sync?
        ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
        {
            VK_CALL(vkWaitForFences(_context.device.logical_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX)); // wait indefinitely instead of periodically checking
            VK_CALL(vkResetFences(_context.device.logical_device, 1, &fd->Fence));
        }
        const VkResult err = vkAcquireNextImageKHR(_context.device.logical_device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            return;
        }
        VK_CALL(err);
        {
            VK_CALL(vkResetCommandPool(_context.device.logical_device, fd->CommandPool, 0));
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
            VkSubmitInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.waitSemaphoreCount = 2;
            info.pWaitSemaphores = waitSemaphores;
            info.pWaitDstStageMask = waitStages;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &fd->CommandBuffer;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &render_complete_semaphore;

            VK_CALL(vkEndCommandBuffer(fd->CommandBuffer));
            VK_CALL(vkQueueSubmit(_context.device.graphics_queue, 1, &info, fd->Fence));
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
        VkResult err = vkQueuePresentKHR(_context.device.graphics_queue, &info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            return;
        }
        VK_CHECK_CALL(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores.
    }

} // namespace Quasa::Vulkan
