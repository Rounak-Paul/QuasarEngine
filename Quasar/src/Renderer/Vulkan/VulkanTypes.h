#pragma once
#include <qspch.h>
#include <vulkan/vulkan.h>

namespace Quasar::Renderer {

const std::vector<const char*> validationLayers = { 
    "VK_LAYER_KHRONOS_validation" 
    // ,"VK_LAYER_LUNARG_api_dump" // For all vulkan calls
};

typedef struct VulkanContext {
    VkInstance instance;
    VkAllocationCallbacks* allocator = nullptr;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;
} VkContext;

}