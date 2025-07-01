// #include "VulkanDevice.h"

// namespace Quasar
// {
// static b8 select_physical_device(VkInstance instance, b8 discrete_gpu);

// b8 vulkan_device_create(VkInstance instance)
// {
//     return b8();
// }

// static b8 select_physical_device(VkInstance instance, b8 discrete_gpu) {
//     uint32_t physical_device_count = 0;
//     VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));
//     if (physical_device_count == 0) {
//         LOG_FATAL("No devices which support Vulkan were found.");
//         return false;
//     }

//     std::vector<VkPhysicalDevice> physical_devices;
//     physical_devices.resize(physical_device_count);
//     VK_CHECK(vkEnumeratePhysicalDevices(context->instance, &physical_device_count, physical_devices.data()));
//     for (u32 i = 0; i < physical_device_count; ++i) {
//         VkPhysicalDeviceProperties properties;
//         vkGetPhysicalDeviceProperties(physical_devices[i], &properties);

//         VkPhysicalDeviceFeatures features;
//         vkGetPhysicalDeviceFeatures(physical_devices[i], &features);

//         VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
//         // Check for dynamic topology support via extension.
//         VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_next = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
//         features2.pNext = &dynamic_state_next;
//         // Check for smooth line rasterisation support via extension.
//         VkPhysicalDeviceLineRasterizationFeaturesEXT smooth_line_next = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT};
//         dynamic_state_next.pNext = &smooth_line_next;
//         // Perform the query.
//         vkGetPhysicalDeviceFeatures2(physical_devices[i], &features2);

//         VkPhysicalDeviceMemoryProperties memory;
//         vkGetPhysicalDeviceMemoryProperties(physical_devices[i], &memory);

//         // Check if device supports local/host visible combo
//         b8 supports_device_local_host_visible = false;
//         for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
//             // Check each memory type to see if its bit is set to 1.
//             if (
//                 ((memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) &&
//                 ((memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)) {
//                 supports_device_local_host_visible = true;
//                 break;
//             }
//         }

//         // TODO: These requirements should probably be driven by engine
//         // configuration.
//         vulkan_physical_device_requirements requirements = {};
//         requirements.graphics = true;
//         requirements.present = true;
//         requirements.transfer = true;
//         // NOTE: Enable this if compute will be required.
//         // requirements.compute = true;
//         requirements.sampler_anisotropy = true;
//         requirements.discrete_gpu = discrete_gpu;
//         requirements.device_extension_names = {};
//         requirements.device_extension_names.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
//         requirements.wide_lines = true;
// #ifdef QS_PLATFORM_APPLE
//         requirements.discrete_gpu = false;
//         requirements.device_extension_names.emplace_back("VK_KHR_portability_subset");
// #endif

//         vulkan_physical_device_queue_family_info queue_info = {};
//         b8 result = physical_device_meets_requirements(
//             physical_devices[i],
//             context->surface,
//             &properties,
//             &features,
//             &requirements,
//             &queue_info,
//             &context->device.swapchain_support);

//         if (result) {
//             QS_CORE_INFO("Selected device: '%s'.", properties.deviceName);
//             // GPU type, etc.
//             switch (properties.deviceType) {
//                 default:
//                 case VK_PHYSICAL_DEVICE_TYPE_OTHER:
//                     QS_CORE_INFO("GPU type is Unknown.");
//                     break;
//                 case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
//                     QS_CORE_INFO("GPU type is Integrated.");
//                     break;
//                 case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
//                     QS_CORE_INFO("GPU type is Descrete.");
//                     break;
//                 case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
//                     QS_CORE_INFO("GPU type is Virtual.");
//                     break;
//                 case VK_PHYSICAL_DEVICE_TYPE_CPU:
//                     QS_CORE_INFO("GPU type is CPU.");
//                     break;
//             }

//             QS_CORE_INFO(
//                 "GPU Driver version: %d.%d.%d",
//                 VK_VERSION_MAJOR(properties.driverVersion),
//                 VK_VERSION_MINOR(properties.driverVersion),
//                 VK_VERSION_PATCH(properties.driverVersion));

//             // Save off the device-supported API version.
//             context->device.api_major = VK_VERSION_MAJOR(properties.apiVersion);
//             context->device.api_minor = VK_VERSION_MINOR(properties.apiVersion);
//             context->device.api_patch = VK_VERSION_PATCH(properties.apiVersion);

//             // Vulkan API version.
//             QS_CORE_INFO(
//                 "Vulkan API version: %d.%d.%d",
//                 context->device.api_major,
//                 context->device.api_minor,
//                 context->device.api_minor);

//             // Memory information
//             for (u32 j = 0; j < memory.memoryHeapCount; ++j) {
//                 f32 memory_size_gib = (((f32)memory.memoryHeaps[j].size) / 1024.0f / 1024.0f / 1024.0f);
//                 if (memory.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
//                     QS_CORE_INFO("Local GPU memory: %.2f GiB", memory_size_gib);
//                 } else {
//                     QS_CORE_INFO("Shared System memory: %.2f GiB", memory_size_gib);
//                 }
//             }

//             context->device.physical_device = physical_devices[i];
//             context->device.graphics_queue_index = queue_info.graphics_family_index;
//             context->device.present_queue_index = queue_info.present_family_index;
//             context->device.transfer_queue_index = queue_info.transfer_family_index;
//             // NOTE: set compute index here if needed.

//             // Keep a copy of properties, features and memory info for later use.
//             context->device.properties = properties;
//             context->device.features = features;
//             context->device.memory = memory;
//             context->device.supports_device_local_host_visible = supports_device_local_host_visible;

//             // The device may or may not support dynamic state, so save that here.
//             if (context->device.api_major >= 1 && context->device.api_minor > 2) {
//                 context->device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT;
//             }
//             // If not supported natively, it might be supported via extension.
//             if (dynamic_state_next.extendedDynamicState) {
//                 context->device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT;
//             }
//             // Check for smooth line rasterization support.
//             if (smooth_line_next.smoothLines) {
//                 context->device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT;
//             }
            
//             break;
//         }
//     }

//     // Ensure a device was selected
//     if (!context->device.physical_device) {
//         QS_CORE_ERROR("No physical devices were found which meet the requirements.");
//         return false;
//     }

//     physical_devices.clear();
//     QS_CORE_INFO("Physical device selected.");
//     return true;
// }

// } // namespace Quasar
