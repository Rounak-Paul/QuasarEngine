#include "VulkanContext.h"

namespace Quasar::Renderer {

VulkanContext::VulkanContext(std::vector<const char *> extensions) {
    const auto instance_props = vk::enumerateInstanceExtensionProperties();

    vk::InstanceCreateFlags flags;
    #ifdef QS_PLATFORM_APPLE
    flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    #endif

    std::vector<const char *> validation_layers{};
    #ifdef QS_DEBUG
    validation_layers.push_back("VK_LAYER_KHRONOS_validation");
    #endif
    const vk::ApplicationInfo app{
        "Quasar",                               // Application Name
        VK_MAKE_VERSION(1, 0, 0),               // Application Version
        "Quasar Engine",                        // Engine Name
        VK_MAKE_VERSION(1, 0, 0),               // Engine Version
        VK_API_VERSION_1_3                      // API Version for Vulkan 1.3
    };
    const vk::InstanceCreateInfo instance_info{
        flags, &app,
        static_cast<uint32_t>(validation_layers.size()), validation_layers.empty() ? nullptr : validation_layers.data(),
        static_cast<uint32_t>(extensions.size()), extensions.data()
    };
    Instance = vk::createInstanceUnique(instance_info);


    #ifdef QS_DEBUG
    const vk::DispatchLoaderDynamic dldi{Instance.get(), vkGetInstanceProcAddr};
    const auto messenger = Instance->createDebugUtilsMessengerEXTUnique(
        vk::DebugUtilsMessengerCreateInfoEXT{
            {},
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            debug_callback,
        },
        nullptr, dldi
    );
    #endif

    PhysicalDevice = FindPhysicalDevice();

    const auto queue_family_props = PhysicalDevice.getQueueFamilyProperties();
    QueueFamily = std::distance(
        queue_family_props.begin(),
        std::find_if(queue_family_props.begin(), queue_family_props.end(), [](const auto &qfp) {
            return qfp.queueFlags & vk::QueueFlagBits::eGraphics;
        })
    );
    if (QueueFamily == static_cast<u32>(-1)) throw std::runtime_error("No graphics queue family found.");

    // Create logical device (with 1 queue).
    std::vector<const char *> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    #ifdef QS_PLATFORM_APPLE
    device_extensions.push_back("VK_KHR_portability_subset");
    #endif
    const std::array<float, 1> queue_priority = {1.0f};
    const vk::DeviceQueueCreateInfo queue_info{{}, QueueFamily, 1, queue_priority.data()};
    Device = PhysicalDevice.createDeviceUnique({{}, queue_info, {}, device_extensions});
    Queue = Device->getQueue(QueueFamily, 0);

    // Create descriptor pool.
    const std::array<vk::DescriptorPoolSize, 1> pool_sizes = {
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 2},
    };
    DescriptorPool = Device->createDescriptorPoolUnique({vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 2, pool_sizes});
}

vk::PhysicalDevice VulkanContext::FindPhysicalDevice() const {
    const auto physical_devices = Instance->enumeratePhysicalDevices();
    if (physical_devices.empty()) throw std::runtime_error("No Vulkan devices found.");

    for (const auto &device : physical_devices) {
        if (device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) return device;
    }
    return physical_devices[0];
}

u32 VulkanContext::FindMemoryType(u32 type_filter, vk::MemoryPropertyFlags prop_flags) const {
    auto mem_props = PhysicalDevice.getMemoryProperties();
    for (u32 i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}
}