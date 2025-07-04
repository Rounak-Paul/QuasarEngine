#include "VulkanDevice.h"

namespace Quasar
{
typedef struct VulkanPhysicalDeviceRequirements {
    b8 graphics;
    b8 present;
    b8 compute;
    b8 transfer;
    
    std::vector<const char*> device_extension_names;
    b8 sampler_anisotropy;
    b8 discrete_gpu;
    b8 wide_lines;
} VulkanPhysicalDeviceRequirements;

typedef struct VulkanPhysicalDeviceQueueFamilyInfo {
    u32 graphics_family_index;
    u32 present_family_index;
    u32 compute_family_index;
    u32 transfer_family_index;
} VulkanPhysicalDeviceQueueFamilyInfo;

static b8 select_physical_device(VkInstance instance, VkSurfaceKHR surface, b8 discrete_gpu, VulkanDevice& device);
b8 PhysicalDeviceMeetsRequirements(
    VkPhysicalDevice device,
    VkSurfaceKHR surface,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const VulkanPhysicalDeviceRequirements* requirements,
    VulkanPhysicalDeviceQueueFamilyInfo* out_queue_family_info,
    VulkanSwapchainSupportInfo* out_swapchain_support);

static b8 select_physical_device(VkInstance instance, VkSurfaceKHR surface, b8 discrete_gpu, VulkanDevice& device) {
    uint32_t physical_device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));
    if (physical_device_count == 0) {
        LOG_FATAL("No devices which support Vulkan were found.");
        return false;
    }

    std::vector<VkPhysicalDevice> physical_devices;
    physical_devices.resize(physical_device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data()));
    for (u32 i = 0; i < physical_device_count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_devices[i], &properties);

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physical_devices[i], &features);

        VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        // Check for dynamic topology support via extension.
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_next = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        features2.pNext = &dynamic_state_next;
        // Check for smooth line rasterisation support via extension.
        VkPhysicalDeviceLineRasterizationFeaturesEXT smooth_line_next = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT};
        dynamic_state_next.pNext = &smooth_line_next;
        // Check for synchronization2 support via extension.
        VkPhysicalDeviceSynchronization2Features sync2_next = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        smooth_line_next.pNext = &sync2_next;
        // Perform the query.
        vkGetPhysicalDeviceFeatures2(physical_devices[i], &features2);

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
        VulkanPhysicalDeviceRequirements requirements = {};
        requirements.graphics = true;
        requirements.present = true;
        requirements.transfer = true;
        requirements.compute = true;
        requirements.sampler_anisotropy = true;
        requirements.discrete_gpu = discrete_gpu;
        requirements.device_extension_names = {};
        requirements.device_extension_names.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requirements.device_extension_names.emplace_back("VK_KHR_synchronization2");
        requirements.wide_lines = true;
#ifdef QS_PLATFORM_APPLE
        requirements.discrete_gpu = false;
        requirements.device_extension_names.emplace_back("VK_KHR_portability_subset");
#endif

        VulkanPhysicalDeviceQueueFamilyInfo queue_info = {};
        b8 result = PhysicalDeviceMeetsRequirements(
            physical_devices[i],
            surface,
            &properties,
            &features,
            &requirements,
            &queue_info,
            &device.swapchain_support);

        if (result) {
            LOG_DEBUG("Selected device: '{}'.", properties.deviceName);
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
                "GPU Driver version: {}.{}.{}",
                VK_VERSION_MAJOR(properties.driverVersion),
                VK_VERSION_MINOR(properties.driverVersion),
                VK_VERSION_PATCH(properties.driverVersion));

            // Save off the device-supported API version.
            device.api_major = VK_VERSION_MAJOR(properties.apiVersion);
            device.api_minor = VK_VERSION_MINOR(properties.apiVersion);
            device.api_patch = VK_VERSION_PATCH(properties.apiVersion);

            // Vulkan API version.
            LOG_DEBUG(
                "Vulkan API version: {}.{}.{}",
                device.api_major,
                device.api_minor,
                device.api_minor);

            // Memory information
            for (u32 j = 0; j < memory.memoryHeapCount; ++j) {
                f32 memory_size_gib = (((f32)memory.memoryHeaps[j].size) / 1024.0f / 1024.0f / 1024.0f);
                if (memory.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                    LOG_DEBUG("Local GPU memory: {} GiB", memory_size_gib);
                } else {
                    LOG_DEBUG("Shared System memory: {} GiB", memory_size_gib);
                }
            }

            device.physical_device = physical_devices[i];
            device.graphics_queue_index = queue_info.graphics_family_index;
            device.present_queue_index = queue_info.present_family_index;
            device.transfer_queue_index = queue_info.transfer_family_index;
            device.compute_queue_index = queue_info.compute_family_index;

            // Keep a copy of properties, features and memory info for later use.
            device.properties = properties;
            device.features = features;
            device.memory = memory;
            device.supports_device_local_host_visible = supports_device_local_host_visible;

            // The device may or may not support dynamic state, so save that here.
            if (device.api_major >= 1 && device.api_minor > 2) {
                device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT;
            }
            if (device.api_major >= 1 && device.api_minor >= 2) {
                device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_12_FEATURES_BIT;
            }
            // Check for smooth line rasterization support.
            if (smooth_line_next.smoothLines) {
                device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT;
            }
            
            break;
        }
    }

    // Ensure a device was selected
    if (!device.physical_device) {
        LOG_ERROR("No physical devices were found which meet the requirements.");
        return false;
    }

    physical_devices.clear();
    LOG_DEBUG("Physical device selected.");
    return true;
}

b8 PhysicalDeviceMeetsRequirements(
    VkPhysicalDevice device,
    VkSurfaceKHR surface,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const VulkanPhysicalDeviceRequirements* requirements,
    VulkanPhysicalDeviceQueueFamilyInfo* out_queue_info,
    VulkanSwapchainSupportInfo* out_swapchain_support) {
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
                        LOG_WARN("Warning: Different queue index used for present vs graphics: {}.", i);
                    }
                    break;
                }
            }
    }

    // Print out some info about the device
    LOG_DEBUG("       {} |       {} |       {} |        {} | {}",
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
        LOG_TRACE("Graphics Family Index: {}", out_queue_info->graphics_family_index);
        LOG_TRACE("Present Family Index:  {}", out_queue_info->present_family_index);
        LOG_TRACE("Transfer Family Index: {}", out_queue_info->transfer_family_index);
        LOG_TRACE("Compute Family Index:  {}", out_queue_info->compute_family_index);

        // Query swapchain support.
        query_swapchain_support(
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
                        if (strcmp(requirements->device_extension_names[i], available_extensions[j].extensionName) == 0) {
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        LOG_DEBUG("Required extension not found: '{}', skipping device.", requirements->device_extension_names[i]);
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

        // Sampler anisotropy
        if (requirements->wide_lines && !features->wideLines) {
            LOG_DEBUG("Device does not support wide lines, this is preffered, not mandatory.");
        }

        // Device meets all requirements.
        return true;
    }

    return false;
}

void query_swapchain_support(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    VulkanSwapchainSupportInfo* out_support_info) {
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

b8 vulkan_device_create(VkInstance instance, VkSurfaceKHR surface, VulkanDevice& device)
{
    device = {};
    if (!select_physical_device(instance, surface, true, device)) {
        LOG_WARN("No Discrete GPU with Vulkan support found. Defaulting to Integrated GPU.");
        if (!select_physical_device(instance, surface, false, device)) {
            LOG_FATAL("No Device with Vulkan support found");
            return false;
        }
    }

    LOG_DEBUG("Creating logical device...");
    b8 present_shares_graphics_queue = device.graphics_queue_index == device.present_queue_index;
    b8 transfer_shares_graphics_queue = device.graphics_queue_index == device.transfer_queue_index;
    b8 compute_shares_graphics_queue = device.graphics_queue_index == device.compute_queue_index;
    b8 present_must_share_graphics = false;
    u32 index_count = 1;
    if (!present_shares_graphics_queue) index_count++;
    if (!transfer_shares_graphics_queue) index_count++;
    if (!compute_shares_graphics_queue &&
        device.compute_queue_index != device.present_queue_index &&
        device.compute_queue_index != device.transfer_queue_index) {
        index_count++;
    }

    std::vector<i32> indices(index_count);
    u8 index = 0;
    indices[index++] = device.graphics_queue_index;
    if (!present_shares_graphics_queue) indices[index++] = device.present_queue_index;
    if (!transfer_shares_graphics_queue) indices[index++] = device.transfer_queue_index;
    if (!compute_shares_graphics_queue &&
        device.compute_queue_index != device.present_queue_index &&
        device.compute_queue_index != device.transfer_queue_index) {
        indices[index++] = device.compute_queue_index;
    }

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos(index_count);
    f32 queue_priorities[2] = {0.9f, 1.0f};

    VkQueueFamilyProperties props[32];
    u32 prop_count;
    vkGetPhysicalDeviceQueueFamilyProperties(device.physical_device, &prop_count, nullptr);
    vkGetPhysicalDeviceQueueFamilyProperties(device.physical_device, &prop_count, props);

    for (u32 i = 0; i < index_count; ++i) {
        queue_create_infos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[i].queueFamilyIndex = indices[i];
        queue_create_infos[i].queueCount = 1;
        if (present_shares_graphics_queue && indices[i] == device.present_queue_index) {
            if (props[device.present_queue_index].queueCount > 1) {
                queue_create_infos[i].queueCount = 2;
            } else {
                present_must_share_graphics = true;
            }
        }
        queue_create_infos[i].flags = 0;
        queue_create_infos[i].pNext = nullptr;
        queue_create_infos[i].pQueuePriorities = queue_priorities;
    }

    #if defined(QS_PLATFORM_APPLE)
    // NOTE: On macOS set environment variable to configure MoltenVK for using Metal argument buffers (needed for descriptor indexing).
    //     - MoltenVK supports Metal argument buffers on macOS, iOS possible in future (see https://github.com/KhronosGroup/MoltenVK/issues/1651)
    setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
    #endif

    std::vector<const char*> extension_names = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME
    };

    // Add sync2 extension if supported but not native
    if (device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT) {
        // Device supports core Vulkan 1.3, no need for KHR extensions
    } else if (device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_12_FEATURES_BIT) {
        extension_names.push_back("VK_KHR_synchronization2");  
        extension_names.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
        extension_names.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        extension_names.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    } else {
        LOG_FATAL("Selected device does not support minimum vulkan 1.2.");
        return false;
    }

    #ifdef QS_PLATFORM_APPLE
    extension_names.push_back("VK_KHR_portability_subset");
    #endif


    if (device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT) {
        extension_names.push_back(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME);
    }

    // Requested features and extensions
    VkPhysicalDeviceFeatures2 device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    device_features.features.samplerAnisotropy = device.features.samplerAnisotropy;
    device_features.features.fillModeNonSolid = device.features.fillModeNonSolid;

    VkPhysicalDeviceLineRasterizationFeaturesEXT line_rasterization_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT};
    if (device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT) {
        line_rasterization_ext.smoothLines = VK_TRUE;
    }

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT};
    descriptor_indexing_features.descriptorBindingPartiallyBound = VK_TRUE;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
    extended_dynamic_state.extendedDynamicState = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dynamic_rendering_ext.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceSynchronization2Features sync2_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    sync2_features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bdaFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR};
    bdaFeatures.bufferDeviceAddress = VK_TRUE;

    // Chain the pNext fields: sync2 → rendering → dyn_state → descriptor → raster
    sync2_features.pNext = &dynamic_rendering_ext;
    dynamic_rendering_ext.pNext = &extended_dynamic_state;
    extended_dynamic_state.pNext = &descriptor_indexing_features;
    descriptor_indexing_features.pNext = &bdaFeatures;
    bdaFeatures.pNext = (device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT) ? &line_rasterization_ext : nullptr;;
    device_features.pNext = &sync2_features;

#if defined(QS_PLATFORM_APPLE)
    setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
#endif

    VkDeviceCreateInfo device_create_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create_info.queueCreateInfoCount = index_count;
    device_create_info.pQueueCreateInfos = queue_create_infos.data();
    device_create_info.pEnabledFeatures = nullptr;
    device_create_info.enabledExtensionCount = static_cast<u32>(extension_names.size());
    device_create_info.ppEnabledExtensionNames = extension_names.data();
    device_create_info.enabledLayerCount = 0;
    device_create_info.ppEnabledLayerNames = nullptr;
    device_create_info.pNext = &device_features;

    VK_CHECK(vkCreateDevice(device.physical_device, &device_create_info, nullptr, &device.logical_device));
    LOG_DEBUG("Logical device created.");

    // Get queues
    vkGetDeviceQueue(device.logical_device, device.graphics_queue_index, 0, &device.graphics_queue);
    vkGetDeviceQueue(device.logical_device, device.present_queue_index,
        present_must_share_graphics ? 0 : (device.graphics_queue_index == device.present_queue_index ? 1 : 0),
        &device.present_queue);
    vkGetDeviceQueue(device.logical_device, device.transfer_queue_index, 0, &device.transfer_queue);
    vkGetDeviceQueue(device.logical_device, device.compute_queue_index, 0, &device.compute_queue);
    LOG_DEBUG("Queues obtained.");

    // Examine dynamic state support and load function pointer if need be.
    if (device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT) {
        LOG_DEBUG("Vulkan device supports native dynamic state, sync2, dynamic rendering.");

    } else if (device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_12_FEATURES_BIT) { 
        LOG_DEBUG("Vulkan device doesn't support native dynamic state, loading extensions.");
        // Dynamic primitive topology.
        device.vkCmdSetPrimitiveTopologyEXT = (PFN_vkCmdSetPrimitiveTopologyEXT)vkGetDeviceProcAddr(device.logical_device, "vkCmdSetPrimitiveTopologyEXT");
        // Dynamic front-face
        device.vkCmdSetFrontFaceEXT = (PFN_vkCmdSetFrontFaceEXT)vkGetDeviceProcAddr(device.logical_device, "vkCmdSetFrontFaceEXT");
        // Dynamic depth/stencil state
        device.vkCmdSetStencilOpEXT = (PFN_vkCmdSetStencilOpEXT)vkGetDeviceProcAddr(device.logical_device, "vkCmdSetStencilOpEXT");
        device.vkCmdSetStencilTestEnableEXT = (PFN_vkCmdSetStencilTestEnableEXT)vkGetDeviceProcAddr(device.logical_device, "vkCmdSetStencilTestEnableEXT");
        device.vkCmdSetDepthTestEnableEXT = (PFN_vkCmdSetDepthTestEnableEXT)vkGetDeviceProcAddr(device.logical_device, "vkCmdSetDepthTestEnableEXT");
        device.vkCmdSetDepthWriteEnableEXT = (PFN_vkCmdSetDepthWriteEnableEXT)vkGetDeviceProcAddr(device.logical_device, "vkCmdSetDepthWriteEnableEXT");
        
        // Dynamic rendering
        LOG_DEBUG("Vulkan device doesn't support native dynamic rendering, loading extensions.");
        device.vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(device.logical_device, "vkCmdBeginRenderingKHR");
        device.vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(device.logical_device, "vkCmdEndRenderingKHR");

        // Sync2
        LOG_DEBUG("Vulkan device doesn't support native synchronization2, loading extensions.");
        device.vkCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR)vkGetDeviceProcAddr(device.logical_device, "vkCmdPipelineBarrier2KHR");
        device.vkCmdWriteTimestamp2KHR = (PFN_vkCmdWriteTimestamp2KHR)vkGetDeviceProcAddr(device.logical_device, "vkCmdWriteTimestamp2KHR");
        device.vkQueueSubmit2KHR = (PFN_vkQueueSubmit2KHR)vkGetDeviceProcAddr(device.logical_device, "vkQueueSubmit2KHR");
        device.vkCmdWaitEvents2KHR = (PFN_vkCmdWaitEvents2KHR)vkGetDeviceProcAddr(device.logical_device, "vkCmdWaitEvents2KHR");
        device.vkCmdSetEvent2KHR = (PFN_vkCmdSetEvent2KHR)vkGetDeviceProcAddr(device.logical_device, "vkCmdSetEvent2KHR");
        device.vkCmdResetEvent2KHR = (PFN_vkCmdResetEvent2KHR)vkGetDeviceProcAddr(device.logical_device, "vkCmdResetEvent2KHR");
        // Copy
        device.vkCmdBlitImage2KHR = (PFN_vkCmdBlitImage2)vkGetDeviceProcAddr(device.logical_device, "vkCmdBlitImage2KHR");
    } else {
        LOG_WARN("Vulkan device does not support native or extension dynamic state. This may cause issues with the renderer.");
    }

    return true;
}

void vulkan_device_destroy(VkInstance instance, VulkanDevice &device)
{
    LOG_DEBUG("Starting vulkan_device_destroy...");
    
    if (device.logical_device != VK_NULL_HANDLE) {
        LOG_DEBUG("Device handle is valid, proceeding with destruction...");
    } else {
        LOG_WARN("Device logical_device is null, skipping device destruction.");
        return;
    }

    // Clear queue handles (these are not destroyed, just unset)
    device.graphics_queue = VK_NULL_HANDLE;
    device.present_queue = VK_NULL_HANDLE;
    device.transfer_queue = VK_NULL_HANDLE;
    device.compute_queue = VK_NULL_HANDLE;

    // Destroy logical device
    if (device.logical_device != VK_NULL_HANDLE) {
        LOG_DEBUG("Destroying logical device (handle: {})...", 
                    (unsigned long long)device.logical_device);
        try {
            vkDestroyDevice(device.logical_device, nullptr);
            device.logical_device = VK_NULL_HANDLE;
            LOG_DEBUG("Logical device destroyed successfully.");
        } catch (...) {
            LOG_ERROR("Exception occurred while destroying logical device!");
            device.logical_device = VK_NULL_HANDLE; // Mark as destroyed anyway
        }
    }

    // Physical devices are not destroyed
    device.physical_device = VK_NULL_HANDLE;

    // Clean up swapchain support info
    if (!device.swapchain_support.formats.empty()) {
        device.swapchain_support.formats.clear();
    }
    device.swapchain_support.format_count = 0;

    if (!device.swapchain_support.present_modes.empty()) {
        device.swapchain_support.present_modes.clear();
    }
    device.swapchain_support.present_mode_count = 0;

    device.swapchain_support.capabilities = {};

    // Reset queue indices
    device.graphics_queue_index = UINT32_MAX;
    device.present_queue_index = UINT32_MAX;
    device.transfer_queue_index = UINT32_MAX;
    device.compute_queue_index = UINT32_MAX;

    LOG_DEBUG("vulkan_device_destroy completed.");
}

b8 vulkan_device_detect_depth_format(VulkanDevice& device) {
    // Format candidates
    const u64 candidate_count = 2;
    VkFormat candidates[2] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT};
    
    u8 sizes[2] = {
        4,
        3};

    u32 flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    for (u64 i = 0; i < candidate_count; ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(device.physical_device, candidates[i], &properties);

        if ((properties.linearTilingFeatures & flags) == flags) {
            device.depth_format = candidates[i];
            device.depth_channel_count = sizes[i];
            return true;
        } else if ((properties.optimalTilingFeatures & flags) == flags) {
            device.depth_format = candidates[i];
            device.depth_channel_count = sizes[i];
            return true;
        }
    }

    return false;
}

} // namespace Quasar
