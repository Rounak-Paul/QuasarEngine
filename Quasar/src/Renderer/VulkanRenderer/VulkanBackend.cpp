#include "VulkanBackend.h"

namespace Quasar::Vulkan{
b8 check_validation_layer_support();
std::vector<const char*> get_required_extensions();
void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
b8 platform_create_vulkan_surface(VkInstance instance, GLFWwindow* window, VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);

b8 Backend::init(String name, Window* window) {
    engine_name = name;
    main_window = window;

#ifdef QS_DEBUG 
    if (!check_validation_layer_support()) {
        LOG_ERROR("validation layers requested, but not available!");
        return false;
    }
#endif

    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = engine_name.c_str();
    app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.pEngineName = "Quasar Engine";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &app_info;

    auto extensions = get_required_extensions();
    
#ifdef QS_PLATFORM_APPLE
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    createInfo.enabledExtensionCount = extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef QS_DEBUG
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
    createInfo.ppEnabledLayerNames = validation_layers.data();

    populate_debug_messenger_create_info(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
#else
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = 0;
#endif

    LOG_DEBUG("Creating Vulkan instance...");
	VkResult result = vkCreateInstance(&createInfo, vkallocator, &instance);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Instance creation failed with VkResult: %d", result)
        return false;
    }

    // TODO: implement multi-threading.
    multithreading_enabled = false;

    // Surface
    LOG_DEBUG("Creating Vulkan surface...");
    if (!platform_create_vulkan_surface(instance, main_window->get_GLFWwindow(), vkallocator, &surface)) {
        LOG_ERROR("Failed to create platform surface");
        return false;
    }

    // Device creation
    LOG_DEBUG("Creating Vulkan Device...");
    if (!device.create(instance, &surface, vkallocator)) {
        LOG_ERROR("Failed to create device!");
        return false;
    }
}

void Backend::shutdown() {
    LOG_DEBUG("Destroying Vulkan device...");
    device.destroy(vkallocator);

    LOG_DEBUG("Destroying Vulkan surface...");
    if (surface) {
        vkDestroySurfaceKHR(instance, surface, vkallocator);
        surface = 0;
    }
}

b8 check_validation_layer_support() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name : validation_layers) {
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

void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = Backend::debug_callback;
    createInfo.pUserData = nullptr;  // Optional
}

VKAPI_ATTR VkBool32 VKAPI_CALL Backend::debug_callback(
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

b8 platform_create_vulkan_surface(VkInstance instance, GLFWwindow* window, VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    auto res = glfwCreateWindowSurface(instance, window, allocator, surface);
    if (res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create Window Surface, VkResult: %d", res);
        return false;
    }
    return true;
}
}