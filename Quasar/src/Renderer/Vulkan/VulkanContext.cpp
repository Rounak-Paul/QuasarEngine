#include "VulkanContext.h"
#include <Platform/File.h>

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
    _instance = vk::createInstanceUnique(instance_info);


    #ifdef QS_DEBUG
    const vk::DispatchLoaderDynamic dldi{_instance.get(), vkGetInstanceProcAddr};
    const auto messenger = _instance->createDebugUtilsMessengerEXTUnique(
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

    _physical_device = find_physical_device();

    const auto queue_family_props = _physical_device.getQueueFamilyProperties();
    _queue_family = std::distance(
        queue_family_props.begin(),
        std::find_if(queue_family_props.begin(), queue_family_props.end(), [](const auto &qfp) {
            return qfp.queueFlags & vk::QueueFlagBits::eGraphics;
        })
    );
    if (_queue_family == static_cast<u32>(-1)) throw std::runtime_error("No graphics queue family found.");

    // Create logical device (with 1 queue).
    std::vector<const char *> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    #ifdef QS_PLATFORM_APPLE
    device_extensions.push_back("VK_KHR_portability_subset");
    #endif
    const std::array<float, 1> queue_priority = {1.0f};
    const vk::DeviceQueueCreateInfo queue_info{{}, _queue_family, 1, queue_priority.data()};
    _device = _physical_device.createDeviceUnique({{}, queue_info, {}, device_extensions});
    _queue = _device->getQueue(_queue_family, 0);

    // Create descriptor pool.
    const std::array<vk::DescriptorPoolSize, 1> pool_sizes = {
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 2},
    };
    _descriptor_pool = _device->createDescriptorPoolUnique({vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 2, pool_sizes});

    // Pipeline
    CreateGraphicsPipeline();
}

vk::PhysicalDevice VulkanContext::find_physical_device() const {
    const auto physical_devices = _instance->enumeratePhysicalDevices();
    if (physical_devices.empty()) throw std::runtime_error("No Vulkan devices found.");

    vk::PhysicalDevice selected_device = physical_devices[0]; // Default to the first device

    for (const auto &device : physical_devices) {
        if (device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            selected_device = device;
            break;
        }
    }

    // Log the selected device
    auto properties = selected_device.getProperties();
    auto memory_properties = selected_device.getMemoryProperties();
    auto features = selected_device.getFeatures();

    LOG_TRACE("Selected Vulkan Device: %s", properties.deviceName);

    LOG_TRACE("Device Type: %s", 
        properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ? "Integrated GPU" :
        properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? "Discrete GPU" :
        properties.deviceType == vk::PhysicalDeviceType::eVirtualGpu ? "Virtual GPU" :
        properties.deviceType == vk::PhysicalDeviceType::eCpu ? "CPU" : "Other");

    LOG_TRACE("Memory Heaps: %u", memory_properties.memoryHeapCount);
    for (uint32_t i = 0; i < memory_properties.memoryHeapCount; i++) {
        LOG_TRACE("Heap %d - Size: %llu MB - Flags: %s", i, 
            memory_properties.memoryHeaps[i].size / (1024 * 1024),
            (memory_properties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) ? "Device Local" : "Host Visible");
    }

    return selected_device;
}

u32 VulkanContext::find_memory_type(u32 type_filter, vk::MemoryPropertyFlags prop_flags) const {
    auto mem_props = _physical_device.getMemoryProperties();
    for (u32 i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

static const auto ImageFormat = vk::Format::eB8G8R8A8Unorm;

vk::SampleCountFlagBits GetMaxUsableSampleCount(const vk::PhysicalDevice physical_device) {
    const auto props = physical_device.getProperties();
    const auto counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e64) return vk::SampleCountFlagBits::e64;
    if (counts & vk::SampleCountFlagBits::e32) return vk::SampleCountFlagBits::e32;
    if (counts & vk::SampleCountFlagBits::e16) return vk::SampleCountFlagBits::e16;
    if (counts & vk::SampleCountFlagBits::e8) return vk::SampleCountFlagBits::e8;
    if (counts & vk::SampleCountFlagBits::e4) return vk::SampleCountFlagBits::e4;
    if (counts & vk::SampleCountFlagBits::e2) return vk::SampleCountFlagBits::e2;

    return vk::SampleCountFlagBits::e1;
}

std::vector<u8> LoadShaderSpv(const std::string& filename) {
    File f;
    if (!f.open(filename, File::Mode::READ, File::Type::BINARY)) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    size_t fileSize = f.get_size();
    std::vector<u8> buffer(fileSize / sizeof(u8));
    f.read_all_binary(buffer.data());
    f.close();

    return buffer;
}

vk::UniqueShaderModule CreateShaderModule(vk::Device device, const std::vector<u8>& code) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size() * sizeof(u8);
    createInfo.pCode = (u32*)code.data();

    return device.createShaderModuleUnique(createInfo);
}

b8 VulkanContext::CreateGraphicsPipeline() {
    auto vertShaderCode = LoadShaderSpv("../Shaders/Builtin.World.vert.spv");
    auto fragShaderCode = LoadShaderSpv("../Shaders/Builtin.World.frag.spv");

    auto vertShaderModule = CreateShaderModule(_device.get(), vertShaderCode);
    auto fragShaderModule = CreateShaderModule(_device.get(), fragShaderCode);

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = {
        {{}, vk::ShaderStageFlagBits::eVertex, *vertShaderModule, "main"},
        {{}, vk::ShaderStageFlagBits::eFragment, *fragShaderModule, "main"}
    };

    // (Rest of your pipeline creation code using shaderStages...)
    return true;
}

}