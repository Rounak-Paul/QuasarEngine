#pragma once

#include <qspch.h>

namespace Quasar::Renderer {

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) {
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
            LOG_DEBUG(pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            LOG_TRACE(pCallbackData->pMessage);
            break;
    }
    return VK_FALSE;
}

inline static bool is_extention_available(const std::vector<vk::ExtensionProperties> &properties, const char *extension) {
    for (const vk::ExtensionProperties &p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

struct VulkanContext {
    VulkanContext(std::vector<const char *> extensions);
    ~VulkanContext() = default; // Using unique handles, so no need to manually destroy anything.

    vk::UniqueInstance Instance;
    vk::PhysicalDevice PhysicalDevice;
    vk::UniqueDevice Device;
    u32 QueueFamily = (u32)-1;
    vk::Queue Queue;
    vk::UniquePipelineCache PipelineCache;
    vk::UniqueDescriptorPool DescriptorPool;

    // Find a discrete GPU, or the first available (integrated) GPU.
    vk::PhysicalDevice find_physical_device() const;
    u32 find_memory_type(u32 type_filter, vk::MemoryPropertyFlags) const;
};

}