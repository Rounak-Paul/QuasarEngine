#include "Renderer.h"

namespace Quasar {

static const std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation"
    // ,"VK_LAYER_LUNARG_api_dump" // For all vulkan calls
};

static b8 check_validation_layer_support();
static std::vector<const char*> get_required_extensions();
static void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& create_info);
static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);
static VkResult create_debug_utils_messenger_ext(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger);
static b8 platform_create_vulkan_surface(VkInstance instance, const Window& window, VkSurfaceKHR* surface);

b8 Renderer::init(const std::string& name, const Window& window)
{
    // Validate layers if validation is enabled
    if (_validation_enabled && !check_validation_layer_support()) {
        // TODO: Log error - validation layers requested but not available
        return false;
    }

    // Get API version
    u32 api_version = 0;
    vkEnumerateInstanceVersion(&api_version);
    _api_major = VK_VERSION_MAJOR(api_version);
    _api_minor = VK_VERSION_MINOR(api_version);
    _api_patch = VK_VERSION_PATCH(api_version);

    // Application info
    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = name.c_str();
    app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.pEngineName = "Quasar Engine";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.apiVersion = VK_MAKE_API_VERSION(0, _api_major, _api_minor, _api_patch);

    // Get required extensions
    auto extensions = get_required_extensions();
    
    // Instance create info
    VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app_info;
    
    #ifdef QS_PLATFORM_APPLE
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    #endif
    
    create_info.enabledExtensionCount = static_cast<u32>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    // Setup validation layers and debug messenger
    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    if (_validation_enabled) {
        create_info.enabledLayerCount = static_cast<u32>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();

        // Setup debug messenger for instance creation/destruction
        populate_debug_messenger_create_info(debug_create_info);
        create_info.pNext = &debug_create_info;
    } else {
        create_info.enabledLayerCount = 0;
        create_info.pNext = nullptr;
    }

    // Create instance
    VkResult result = vkCreateInstance(&create_info, nullptr, &_instance);
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create vulkan instance!");
        return false;
    }

    // Setup debug messenger
    if (_validation_enabled) {
        if (create_debug_utils_messenger_ext(_instance, &debug_create_info, nullptr, &_debug_messenger) != VK_SUCCESS) {
            LOG_WARN("Failed to create vulkan debug messenger! Validation errors may be ommited.");
        }
    }

    // Surface
    if (!platform_create_vulkan_surface(_instance, window, &_surface)) {
        LOG_FATAL("Failed to create primary surface for drawing!");
        return false;
    }

    // Device creation
    if (!vulkan_device_create(_instance, _surface, _device)) {
        LOG_ERROR("Failed to create device!");
        return false;
    }

    // Examine dynamic state support and load function pointer if need be.
    if (
        !(_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) &&
        (_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT)) {
        LOG_DEBUG("Vulkan device doesn't support native dynamic state, but does via extension. Using extension.");

        // Dynamic primitive topology.
        vkCmdSetPrimitiveTopologyEXT = (PFN_vkCmdSetPrimitiveTopologyEXT)vkGetInstanceProcAddr(_instance, "vkCmdSetPrimitiveTopologyEXT");

        // Dynamic front-cace
        vkCmdSetFrontFaceEXT = (PFN_vkCmdSetFrontFaceEXT)vkGetInstanceProcAddr(_instance, "vkCmdSetFrontFaceEXT");
        // Dynamic depth/stencil state
        vkCmdSetStencilOpEXT = (PFN_vkCmdSetStencilOpEXT)vkGetInstanceProcAddr(_instance, "vkCmdSetStencilOpEXT");
        vkCmdSetStencilTestEnableEXT = (PFN_vkCmdSetStencilTestEnableEXT)vkGetInstanceProcAddr(_instance, "vkCmdSetStencilTestEnableEXT");
        vkCmdSetDepthTestEnableEXT = (PFN_vkCmdSetDepthTestEnableEXT)vkGetInstanceProcAddr(_instance, "vkCmdSetDepthTestEnableEXT");
        vkCmdSetDepthWriteEnableEXT = (PFN_vkCmdSetDepthWriteEnableEXT)vkGetInstanceProcAddr(_instance, "vkCmdSetDepthWriteEnableEXT");

        // Dynamic rendering
        vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetInstanceProcAddr(_instance, "vkCmdBeginRenderingKHR");
        vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)vkGetInstanceProcAddr(_instance, "vkCmdEndRenderingKHR");
    } else {
        if (_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
            LOG_DEBUG("Vulkan device supports native dynamic state.");
        } else {
            LOG_WARN("Vulkan device does not support native or extension dynamic state. This may cause issues with the renderer.");
        }
    }

    Extent2D extent = window.get_extent();
    vulkan_swapchain_create(_device, _surface, extent.width, extent.height, _swapchain);

    return true;
}

b8 Renderer::begin_frame()
{
    return true;
}

void Renderer::end_frame()
{
    // TODO: Implement frame end logic
}

void Renderer::shutdown()
{
    // Wait for device to finish all operations before cleanup
    if (_device.logical_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(_device.logical_device);
    }

    vulkan_swapchain_destroy(_device, _swapchain);

    // if (_validation_enabled && _debug_messenger != VK_NULL_HANDLE) {
    //     auto destroy_func = (PFN_vkDestroyDebugUtilsMessengerEXT) 
    //         vkGetInstanceProcAddr(_instance, "vkDestroyDebugUtilsMessengerEXT");
    //     if (destroy_func != nullptr) {
    //         destroy_func(_instance, _debug_messenger, nullptr);
    //         _debug_messenger = VK_NULL_HANDLE;
    //     }
    // }

    // Destroy surface
    if (_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        _surface = VK_NULL_HANDLE;
    }
    
    if (_device.logical_device != VK_NULL_HANDLE) {
        vulkan_device_destroy(_instance, _device);
        _device.logical_device = VK_NULL_HANDLE;
    }
    
    // if (_instance != VK_NULL_HANDLE) {
    //     vkDestroyInstance(_instance, nullptr);
    //     _instance = VK_NULL_HANDLE;
    // }
    
    // Reset API version info
    _api_major = 0;
    _api_minor = 0;
    _api_patch = 0;
}

// Helper function to setup debug messenger create info
static void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& create_info)
{
    create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity = 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ;
        // VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        // VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    create_info.messageType = 
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = vk_debug_callback;
    create_info.pUserData = nullptr;
}

static b8 check_validation_layer_support() 
{
    u32 layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name : validation_layers) {
        b8 layer_found = false;

        for (const auto& layer_properties : available_layers) {
            if (strcmp(layer_name, layer_properties.layerName) == 0) {
                layer_found = true;
                break;
            }
        }
        
        if (!layer_found) {
            return false;
        }
    }

    return true;
}

static std::vector<const char*> get_required_extensions()
{
    u32 glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);
    
    #ifdef QS_PLATFORM_APPLE
    extensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    #endif
    
    #ifdef QS_DEBUG 
    extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    #endif
    
    return extensions;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) 
{
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            LOG_ERROR(pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            LOG_WARN(pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            LOG_DEBUG(pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            LOG_DEBUG(pCallbackData->pMessage);
            break;
        default:
            LOG_TRACE(pCallbackData->pMessage);
            break;
    }
    return VK_FALSE;
}

static VkResult create_debug_utils_messenger_ext(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) 
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) 
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static b8 platform_create_vulkan_surface(VkInstance instance, const Window& window, VkSurfaceKHR* surface) {
    GLFWwindow* w = window.get_GLFWwindow();
    auto res = glfwCreateWindowSurface(instance, w, nullptr, surface);
    if (res != VK_SUCCESS)
    {
        LOG_ERROR("Surface creation failed");
        return false;
    }
    return true;
}
} // namespace Quasar