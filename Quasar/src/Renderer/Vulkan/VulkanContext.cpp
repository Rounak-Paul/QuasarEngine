#include "VulkanContext.h"
#include <Platform/File.h>
#include "VulkanCheckResult.h"

namespace Quasar {
VkSampleCountFlagBits GetMaxUsableSampleCount(VkPhysicalDevice physical_device);

VkDebugUtilsMessengerEXT _debug_messenger;

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



inline static bool is_extention_available(const std::vector<VkExtensionProperties> &properties, const char *extension) {
    for (const auto &p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

const std::vector<const char*> validationLayers = { 
    "VK_LAYER_KHRONOS_validation" 
    // ,"VK_LAYER_LUNARG_api_dump" // For all vulkan calls
};

b8 VulkanContext::create(GLFWwindow* window) {
    _allocator = nullptr;

    #ifdef QS_DEBUG 
        if (!check_validation_layer_support()) {
            LOG_ERROR("validation layers requested, but not available!");
        }
    #endif

    // Get the currently-installed instance version. Not necessarily what the device
    // uses, though. Use this to create the instance though.
    u32 api_version = 0;
    vkEnumerateInstanceVersion(&api_version);
    _device.api_major = VK_VERSION_MAJOR(api_version);
    _device.api_minor = VK_VERSION_MINOR(api_version);
    _device.api_patch = VK_VERSION_PATCH(api_version);

    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "Quasar";
    app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.pEngineName = "Quasar Engine";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.apiVersion = VK_MAKE_API_VERSION(0, _device.api_major, _device.api_minor, _device.api_patch);

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
    VK_CALL(vkCreateInstance(&createInfo, _allocator, &_instance));

    #ifdef QS_DEBUG
    Setup_debug_messenger(_instance);
    #endif

    // TODO: implement multi-threading.
    _multithreading_enabled = false;

    // Surface
    LOG_INFO("Creating Vulkan surface...");
    if (!create_vulkan_surface(window)) {
        LOG_FATAL("Failed to create platform surface!");
        return false;
    }

    // Device creation
    if (!_device.create(this)) {
        LOG_ERROR("Failed to create device!");
        return false;
    }

    // TODO: move to shader ??
    // Render
    // Create descriptor pool.
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

        VK_CALL(vkCreateDescriptorPool(_device.logical_device, &descriptor_pool_info, nullptr, &_descriptor_pool));
    }

    // Renderpass
    // Render multisampled into the offscreen image, then resolve into a single-sampled resolve image.
    _msaa_samples = GetMaxUsableSampleCount(_device.physical_device);
    // Define attachments.
    VkAttachmentDescription attachments[2] = {};

    // Multi-sampled offscreen image attachment.
    attachments[0].flags = 0;
    attachments[0].format = _image_format;
    attachments[0].samples = _msaa_samples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Single-sampled resolve attachment.
    attachments[1].flags = 0;
    attachments[1].format = _image_format;
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
    VK_CALL(vkCreateRenderPass(_device.logical_device, &render_pass_info, nullptr, &_render_pass));

    // Pipeline
    VulkanPipelineConfig config;
    config.polygonMode = VK_POLYGON_MODE_FILL;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTestEnable = VK_TRUE;
    config.depthWriteEnable = VK_TRUE;
    config.msaaSamples = _msaa_samples;

    if (!_pipeline.create(_device.logical_device, _render_pass, config)) {
        LOG_ERROR("Failed to create pipeline.")
        return false;
    }

    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = _device.graphics_queue_index;

    VK_CALL(vkCreateCommandPool(_device.logical_device, &command_pool_info, nullptr, &_command_pool));

    _command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (auto& command_buffer : _command_buffers) {
        command_buffer.allocate(this, _command_pool, true);
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

    VK_CALL(vkCreateSampler(_device.logical_device, &sampler_info, nullptr, &_texture_sampler));

    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(_device.logical_device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(_device.logical_device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {

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
        vertex_buffer.create(this, buffer_info);
    }
    {
        VkDeviceSize buffer_size = sizeof(Math::Vertex) * 1024 * 3;
        VulkanBufferCreateInfo buffer_info = {
            buffer_size, // size
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, // usage
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT // properties
        };
        index_buffer.create(this, buffer_info);
    }

    return true;
}

void VulkanContext::destroy()
{
    vkDeviceWaitIdle(_device.logical_device);

    vertex_buffer.destroy();
    index_buffer.destroy();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(_device.logical_device, renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(_device.logical_device, inFlightFences[i], nullptr);
    }

    if (_texture_sampler) {
        vkDestroySampler(_device.logical_device, _texture_sampler, _allocator);
        _texture_sampler = VK_NULL_HANDLE;
    }

    for (auto& command_buffer : _command_buffers) {
        command_buffer.free(this, _command_pool);
    }

    if (_command_pool) {
        vkDestroyCommandPool(_device.logical_device, _command_pool, _allocator);
        _command_pool = VK_NULL_HANDLE;
    }

    if (_pipeline._graphics_pipeline) {
        _pipeline.destroy(_device.logical_device);
    }

    if (_render_pass) {
        vkDestroyRenderPass(_device.logical_device, _render_pass, _allocator);
        _render_pass = VK_NULL_HANDLE;
    }

    if (_descriptor_pool) {
        vkDestroyDescriptorPool(_device.logical_device, _descriptor_pool, _allocator);
        _descriptor_pool = VK_NULL_HANDLE;
    }

    #ifdef QS_DEBUG
    if (_debug_messenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(_instance, _debug_messenger, _allocator);
        }
        _debug_messenger = VK_NULL_HANDLE;
    }
#endif

    _device.destroy(this);

    if (_instance) {
        vkDestroyInstance(_instance, _allocator);
        _instance = VK_NULL_HANDLE;
    }
}

b8 VulkanContext::create_vulkan_surface(GLFWwindow* window)
{
    VK_CALL(glfwCreateWindowSurface(_instance, window, nullptr, &_surface));
    return true;
}

std::vector<const char*> VulkanContext::get_required_extensions() {
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

b8 VulkanContext::check_validation_layer_support() {
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

u32 VulkanContext::find_memory_type(u32 type_filter, u32 prop_flags) const {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(_device.physical_device, &memory_properties);
    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        // Check each memory type to see if its bit is set to 1.
        if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & prop_flags) == prop_flags) {
            return i;
        }
    }
    LOG_WARN("Unable to find suitable memory type!");
    return -1;
}

}