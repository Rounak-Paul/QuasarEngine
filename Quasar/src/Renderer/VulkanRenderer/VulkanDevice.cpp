#include "VulkanDevice.h"
#include <cstring>
#include "VulkanTypes.h"

namespace Quasar::Vulkan {

typedef struct vulkan_physical_device_requirements {
    b8 graphics;
    b8 present;
    b8 compute;
    b8 transfer;
    
    std::vector<const char*> device_extension_names;
    b8 sampler_anisotropy;
    b8 discrete_gpu;
    b8 wide_lines;
} vulkan_physical_device_requirements;

typedef struct vulkan_physical_device_queue_family_info {
    u32 graphics_family_index;
    u32 present_family_index;
    u32 compute_family_index;
    u32 transfer_family_index;
} vulkan_physical_device_queue_family_info;

b8 physical_device_meets_requirements(
    VkPhysicalDevice device,
    VkSurfaceKHR surface,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const vulkan_physical_device_requirements* requirements,
    vulkan_physical_device_queue_family_info* out_queue_family_info,
    swapchain_support_info* out_swapchain_support);

b8 VulkanDevice::create(VkInstance instance, VkSurfaceKHR* surface, VkAllocationCallbacks* allocator) {
    if (!select_physical_device(instance, surface, &swapchain_support, true)) {
        LOG_WARN("No Discrete GPU with Vulkan support found. Defaulting to Integrated GPU.")
        if (!select_physical_device(instance, surface, &swapchain_support, false)) {
            LOG_FATAL("No Device with Vulkan support found")
            return false;
        }
    }

    LOG_DEBUG("Creating logical device...");
    // NOTE: Do not create additional queues for shared indices.
    b8 present_shares_graphics_queue = graphics_queue_index == present_queue_index;
    b8 transfer_shares_graphics_queue = graphics_queue_index == transfer_queue_index;
    u32 index_count = 1;
    if (!present_shares_graphics_queue) {
        index_count++;
    }
    if (!transfer_shares_graphics_queue) {
        index_count++;
    }
    std::vector<u32> indices(index_count);
    u8 index = 0;
    indices[index++] = graphics_queue_index;
    if (!present_shares_graphics_queue) {
        indices[index++] = present_queue_index;
    }
    if (!transfer_shares_graphics_queue) {
        indices[index++] = transfer_queue_index;
    }

    VkQueueFamilyProperties props[32];
    u32 prop_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &prop_count, 0);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &prop_count, props);

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos(index_count);
    f32 queue_priority = 1.0f;
    for (u32 i = 0; i < index_count; ++i) {
        queue_create_infos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[i].queueFamilyIndex = indices[i];
        queue_create_infos[i].queueCount = 1;
        // if (present_shares_graphics_queue && indices[i] == graphics_queue_index) {
        //     if (props[present_queue_index].queueCount > 1) {
        //         queue_create_infos[i].queueCount = 2;
        //     }
        // }
        queue_create_infos[i].flags = 0;
        queue_create_infos[i].pNext = 0;
        queue_create_infos[i].pQueuePriorities = &queue_priority;
    }

    // Request device features.
    // TODO: should be config driven
    VkPhysicalDeviceFeatures device_features = {};
    device_features.samplerAnisotropy = VK_TRUE;  // Request anistrophy
    // device_features.wideLines = VK_TRUE;

    VkDeviceCreateInfo device_create_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create_info.queueCreateInfoCount = index_count;
    device_create_info.pQueueCreateInfos = queue_create_infos.data();
    device_create_info.pEnabledFeatures = &device_features;

    // std::vector<f32> queue_priority{1.0f, 1.0f};
    // device_create_info.pQueueCreateInfos->pQueuePriorities = queue_priority.data();
    
    std::vector<const char*> extension_names = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef QS_PLATFORM_APPLE
    extension_names.push_back("VK_KHR_portability_subset");
#endif
    device_create_info.enabledExtensionCount = static_cast<uint32_t>(extension_names.size());
    device_create_info.ppEnabledExtensionNames = extension_names.data();

    // Deprecated and ignored, so pass nothing.
    device_create_info.enabledLayerCount = 0;
    device_create_info.ppEnabledLayerNames = 0;

    // Create the device.
    VK_CHECK(vkCreateDevice(
        physical_device,
        &device_create_info,
        allocator,
        &logical_device));

    LOG_DEBUG("Logical device created.");

    // Get queues.
    vkGetDeviceQueue(
        logical_device,
        graphics_queue_index,
        0,
        &graphics_queue);

    vkGetDeviceQueue(
        logical_device,
        present_queue_index,
        0,
        &present_queue);

    vkGetDeviceQueue(
        logical_device,
        transfer_queue_index,
        0,
        &transfer_queue);
    LOG_DEBUG("Queues obtained.");

    return true;
}

void VulkanDevice::destroy(VkAllocationCallbacks* allocator) {
    // Unset queues
    graphics_queue = 0;
    present_queue = 0;
    transfer_queue = 0;

    LOG_DEBUG("Destroying command pools...");
    vkDestroyCommandPool(
        logical_device,
        graphics_command_pool,
        allocator);

    // Destroy logical device
    LOG_DEBUG("Destroying logical device...");
    if (logical_device) {
        vkDestroyDevice(logical_device, allocator);
        logical_device = 0;
    }

    // Physical devices are not destroyed.
    LOG_DEBUG("Releasing physical device resources...");
    physical_device = 0;

    if (!swapchain_support.formats.empty()) {
        swapchain_support.formats.clear();
        swapchain_support.format_count = 0;
    }

    if (!swapchain_support.present_modes.empty()) {
        swapchain_support.present_modes.clear();
        swapchain_support.present_mode_count = 0;
    }

    swapchain_support.capabilities = {};

    graphics_queue_index = -1;
    present_queue_index = -1;
    transfer_queue_index = -1;
}

void VulkanDevice::query_swapchain_support(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    swapchain_support_info* out_support_info) {
    // Surface capabilities
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device,
        surface,
        &out_support_info->capabilities));

    // Surface formats
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device,
        surface,
        &out_support_info->format_count,
        nullptr));

    if (out_support_info->format_count != 0) {
        if (!out_support_info->formats.empty()) {
            out_support_info->formats.clear();
        }
        out_support_info->formats.resize(out_support_info->format_count);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical_device,
            surface,
            &out_support_info->format_count,
            out_support_info->formats.data()));
    }

    // Present modes
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device,
        surface,
        &out_support_info->present_mode_count,
        nullptr));
    if (out_support_info->present_mode_count != 0) {
        if (out_support_info->present_modes.empty()) {
            out_support_info->present_modes.clear();
            out_support_info->present_modes.resize(out_support_info->present_mode_count);
        }
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical_device,
            surface,
            &out_support_info->present_mode_count,
            out_support_info->present_modes.data()));
    }
}

b8 VulkanDevice::select_physical_device(VkInstance instance, VkSurfaceKHR* surface, swapchain_support_info* swapchain_support, b8 discreteGPU) {
    uint32_t physical_device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));
    if (physical_device_count == 0) {
        LOG_FATAL("No devices which support Vulkan were found.");
        return false;
    }

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    physical_devices.resize(physical_device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data()));
    for (u32 i = 0; i < physical_device_count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_devices[i], &properties);

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physical_devices[i], &features);

        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(physical_devices[i], &memory);

        // Check if device supports local/host visible combo
        b8 supports_device_local_host_visible = false;
        for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
            // Check each memory type to see if its bit is set to 1.
            if (
                ((memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) &&
                ((memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)) {
                supports_device_local_host_visible = true;
                break;
            }
        }

        // TODO: These requirements should probably be driven by engine
        // configuration.
        vulkan_physical_device_requirements requirements = {};
        requirements.graphics = true;
        requirements.present = true;
        requirements.transfer = true;
        // NOTE: Enable this if compute will be required.
        // requirements.compute = true;
        requirements.sampler_anisotropy = true;
        requirements.discrete_gpu = discreteGPU;
        requirements.device_extension_names = {};
        requirements.device_extension_names.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requirements.wide_lines = true;
#ifdef QS_PLATFORM_APPLE
        requirements.discrete_gpu = false;
        requirements.device_extension_names.emplace_back("VK_KHR_portability_subset");
#endif

        vulkan_physical_device_queue_family_info queue_info = {};
        b8 result = physical_device_meets_requirements(
            physical_devices[i],
            *surface,
            &properties,
            &features,
            &requirements,
            &queue_info,
            swapchain_support);

        if (result) {
            LOG_DEBUG("Selected device: '%s'.", properties.deviceName);
            // GPU type, etc.
            switch (properties.deviceType) {
                default:
                case VK_PHYSICAL_DEVICE_TYPE_OTHER:
                    LOG_DEBUG("GPU type is Unknown.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                    LOG_DEBUG("GPU type is Integrated.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                    LOG_DEBUG("GPU type is Descrete.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                    LOG_DEBUG("GPU type is Virtual.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_CPU:
                    LOG_DEBUG("GPU type is CPU.");
                    break;
            }

            LOG_DEBUG(
                "GPU Driver version: %d.%d.%d",
                VK_VERSION_MAJOR(properties.driverVersion),
                VK_VERSION_MINOR(properties.driverVersion),
                VK_VERSION_PATCH(properties.driverVersion));

            // Vulkan API version.
            LOG_DEBUG(
                "Vulkan API version: %d.%d.%d",
                VK_VERSION_MAJOR(properties.apiVersion),
                VK_VERSION_MINOR(properties.apiVersion),
                VK_VERSION_PATCH(properties.apiVersion));

            // Memory information
            for (u32 j = 0; j < memory.memoryHeapCount; ++j) {
                f32 memory_size_gib = (((f32)memory.memoryHeaps[j].size) / 1024.0f / 1024.0f / 1024.0f);
                if (memory.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                    LOG_DEBUG("Local GPU memory: %.2f GiB", memory_size_gib);
                } else {
                    LOG_DEBUG("Shared System memory: %.2f GiB", memory_size_gib);
                }
            }

            physical_device = physical_devices[i];
            graphics_queue_index = queue_info.graphics_family_index;
            present_queue_index = queue_info.present_family_index;
            transfer_queue_index = queue_info.transfer_family_index;
            // NOTE: set compute index here if needed.

            // Keep a copy of properties, features and memory info for later use.
            properties = properties;
            features = features;
            memory = memory;
            supports_device_local_host_visible = supports_device_local_host_visible;
            break;
        }
    }

    // Ensure a device was selected
    if (!physical_device) {
        LOG_ERROR("No physical devices were found which meet the requirements.");
        return false;
    }

    physical_devices.clear();
    LOG_DEBUG("Physical device selected.");
    return true;
}

b8 physical_device_meets_requirements(
    VkPhysicalDevice device,
    VkSurfaceKHR surface,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const vulkan_physical_device_requirements* requirements,
    vulkan_physical_device_queue_family_info* out_queue_info,
    swapchain_support_info* out_swapchain_support) {
    // Evaluate device properties to determine if it meets the needs of our applcation.
    out_queue_info->graphics_family_index = -1;
    out_queue_info->present_family_index = -1;
    out_queue_info->compute_family_index = -1;
    out_queue_info->transfer_family_index = -1;

    // Discrete GPU?
    if (requirements->discrete_gpu) {
        if (properties->deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            LOG_DEBUG("Device is not a discrete GPU, and one is required. Skipping.");
            return false;
        }
    }

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    // Look at each queue and see what queues it supports
    LOG_DEBUG("Graphics | Present | Compute | Transfer | Name");
    u8 min_transfer_score = 255;
    for (u32 i = 0; i < queue_family_count; ++i) {
        u8 current_transfer_score = 0;

        // Graphics queue?
        if (out_queue_info->graphics_family_index == -1 && queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            out_queue_info->graphics_family_index = i;
            ++current_transfer_score;

            // If also a present queue, this prioritizes grouping of the 2.
            VkBool32 supports_present = VK_FALSE;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supports_present));
            if (supports_present) {
                out_queue_info->present_family_index = i;
                ++current_transfer_score;
            }
        }

        // Compute queue?
        if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            out_queue_info->compute_family_index = i;
            ++current_transfer_score;
        }

        // Transfer queue?
        if (queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            // Take the index if it is the current lowest. This increases the
            // liklihood that it is a dedicated transfer queue.
            if (current_transfer_score <= min_transfer_score) {
                min_transfer_score = current_transfer_score;
                out_queue_info->transfer_family_index = i;
            }
        }
    }

        // If a present queue hasn't been found, iterate again and take the first one.
        // This should only happen if there is a queue that supports graphics but NOT
        // present.
        if (out_queue_info->present_family_index == -1) {
            for (u32 i = 0; i < queue_family_count; ++i) {
                VkBool32 supports_present = VK_FALSE;
                VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supports_present));
                if (supports_present) {
                    out_queue_info->present_family_index = i;

                    // If they differ, bleat about it and move on. This is just here for troubleshooting
                    // purposes.
                    if (out_queue_info->present_family_index != out_queue_info->graphics_family_index) {
                        LOG_WARN("Warning: Different queue index used for present vs graphics: %u.", i);
                    }
                    break;
                }
            }
    }

    // Print out some info about the device
    LOG_DEBUG("       %d |       %d |       %d |        %d | %s",
          out_queue_info->graphics_family_index != -1,
          out_queue_info->present_family_index != -1,
          out_queue_info->compute_family_index != -1,
          out_queue_info->transfer_family_index != -1,
          properties->deviceName);

    if (
        (!requirements->graphics || (requirements->graphics && out_queue_info->graphics_family_index != -1)) &&
        (!requirements->present || (requirements->present && out_queue_info->present_family_index != -1)) &&
        (!requirements->compute || (requirements->compute && out_queue_info->compute_family_index != -1)) &&
        (!requirements->transfer || (requirements->transfer && out_queue_info->transfer_family_index != -1))) {
        LOG_DEBUG("Device meets queue requirements.");
        LOG_DEBUG("Graphics Family Index: %i", out_queue_info->graphics_family_index);
        LOG_DEBUG("Present Family Index:  %i", out_queue_info->present_family_index);
        LOG_DEBUG("Transfer Family Index: %i", out_queue_info->transfer_family_index);
        LOG_DEBUG("Compute Family Index:  %i", out_queue_info->compute_family_index);

        // Query swapchain support.
        VulkanDevice::query_swapchain_support(
            device,
            surface,
            out_swapchain_support);

        if (out_swapchain_support->format_count < 1 || out_swapchain_support->present_mode_count < 1) {
            if (!out_swapchain_support->formats.empty()) {
                out_swapchain_support->formats.clear();
            }
            if (!out_swapchain_support->present_modes.empty()) {
                out_swapchain_support->present_modes.clear();
            }
            LOG_DEBUG("Required swapchain support not present, skipping device.");
            return false;
        }

        // Device extensions.
        if (!requirements->device_extension_names.empty()) {
            u32 available_extension_count = 0;
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                device,
                nullptr,
                &available_extension_count,
                nullptr));
            std::vector<VkExtensionProperties> available_extensions(available_extension_count);
            if (available_extension_count != 0) {
                VK_CHECK(vkEnumerateDeviceExtensionProperties(
                    device,
                    0,
                    &available_extension_count,
                    available_extensions.data()));

                u32 required_extension_count = requirements->device_extension_names.size();
                for (u32 i = 0; i < required_extension_count; ++i) {
                    b8 found = false;
                    for (u32 j = 0; j < available_extension_count; ++j) {
                        if (strcmp(requirements->device_extension_names[i], available_extensions[j].extensionName)) {
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        LOG_DEBUG("Required extension not found: '%s', skipping device.", requirements->device_extension_names[i]);
                        return false;
                    }
                }
            }
        }

        // Sampler anisotropy
        if (requirements->sampler_anisotropy && !features->samplerAnisotropy) {
            LOG_DEBUG("Device does not support samplerAnisotropy, skipping.");
            return false;
        }

        // if (requirements->wide_lines && !features->wideLines) {
        //     LOG_DEBUG("Device does not support wide lines, skipping.");
        //     return false;
        // }

        // Device meets all requirements.
        return true;
    }

    return false;
}


b8 VulkanDevice::detect_depth_format() {
    // Format candidates
    const u64 candidate_count = 3;
    VkFormat candidates[3] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT};
    
    u8 sizes[3] = {
        4,
        4,
        3};

    u32 flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    for (u64 i = 0; i < candidate_count; ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical_device, candidates[i], &properties);

        if ((properties.linearTilingFeatures & flags) == flags) {
            depth_format = candidates[i];
            depth_channel_count = sizes[i];
            return true;
        } else if ((properties.optimalTilingFeatures & flags) == flags) {
            depth_format = candidates[i];
            depth_channel_count = sizes[i];
            return true;
        }
    }

    return false;
}

}