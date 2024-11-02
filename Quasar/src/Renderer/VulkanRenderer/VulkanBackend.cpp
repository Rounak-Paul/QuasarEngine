#include "VulkanBackend.h"
#include "VulkanInfo.h"
#include "VulkanImage.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

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
        LOG_ERROR("Failed to create Device!");
        return false;
    }

    // initialize the memory allocator
    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.physicalDevice = device.physical_device;
    allocator_info.device = device.logical_device;
    allocator_info.instance = instance;
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocator_info, &allocator);

    main_deletion_queue.push_function([&]() {
        vmaDestroyAllocator(allocator);
    });

    // Swapchain creation
    LOG_DEBUG("Creating Vulkan Swapchain...");
    VkExtent2D extent = main_window->get_extent();
    if (!swapchain.create(&device, &surface, extent.width, extent.height, vkallocator)) {
        LOG_ERROR("Failed to create Swapchain!");
        return false;
    }

    //draw image size will match the window
	VkExtent3D draw_image_extent = {
		extent.width,
		extent.height,
		1
	};

	//hardcoding the draw format to 32 bit float
	draw_image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	draw_image.extent = draw_image_extent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = image_create_info(draw_image.format, drawImageUsages, draw_image_extent);

	//for the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(allocator, &rimg_info, &rimg_allocinfo, &draw_image.image, &draw_image.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = imageview_create_info(draw_image.format, draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(device.logical_device, &rview_info, nullptr, &draw_image.view));

	//add to deletion queues
	main_deletion_queue.push_function([=]() {
		vkDestroyImageView(device.logical_device, draw_image.view, nullptr);
		vmaDestroyImage(allocator, draw_image.image, draw_image.allocation);
	});

    //create a command pool for commands submitted to the graphics queue.
    //we also want the pool to allow for resetting of individual command buffers
    LOG_DEBUG("Creating Vulkan Command Pool & Buffer...");
    VkCommandPoolCreateInfo command_pool_info = command_pool_create_info(device.graphics_queue_index, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(device.logical_device, &command_pool_info, vkallocator, &frames[i].command_pool));
        VkCommandBufferAllocateInfo cmd_alloc_info = command_buffer_allocate_info(frames[i].command_pool, 1);
        VK_CHECK(vkAllocateCommandBuffers(device.logical_device, &cmd_alloc_info, &frames[i].main_command_buffer));
    }

    //create syncronization structures
	//one fence to control when the gpu has finished rendering the frame,
	//and 2 semaphores to syncronize rendering with swapchain
	//we want the fence to start signalled so we can wait on it on the first frame
    LOG_DEBUG("Creating Vulkan Sync objects...");
	VkFenceCreateInfo fence_info = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphore_info = semaphore_create_info(0);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(device.logical_device, &fence_info, vkallocator, &frames[i].render_fence));

		VK_CHECK(vkCreateSemaphore(device.logical_device, &semaphore_info, vkallocator, &frames[i].swapchain_semaphore));
		VK_CHECK(vkCreateSemaphore(device.logical_device, &semaphore_info, vkallocator, &frames[i].render_semaphore));
	}

    return true;
}

void Backend::shutdown() {
    vkDeviceWaitIdle(device.logical_device);

    LOG_DEBUG("Destroying Vulkan Command Pool & Buffer...");
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        vkDestroyCommandPool(device.logical_device, frames[i].command_pool, vkallocator);

        //destroy sync objects
        LOG_DEBUG("Destroying Vulkan Sync Objects...");
		vkDestroyFence(device.logical_device, frames[i].render_fence, nullptr);
		vkDestroySemaphore(device.logical_device, frames[i].render_semaphore, nullptr);
		vkDestroySemaphore(device.logical_device ,frames[i].swapchain_semaphore, nullptr);

        frames[i].deletion_queue.flush();
    }

    //flush the global deletion queue
    main_deletion_queue.flush();

    LOG_DEBUG("Destroying Vulkan Swapchain...");
    swapchain.destroy();

    LOG_DEBUG("Destroying Vulkan device...");
    device.destroy(vkallocator);

    LOG_DEBUG("Destroying Vulkan surface...");
    if (surface) {
        vkDestroySurfaceKHR(instance, surface, vkallocator);
        surface = 0;
    }
}

void Backend::draw()
{
    // wait until the gpu has finished rendering the last frame. Timeout of 1 second
	VK_CHECK(vkWaitForFences(device.logical_device, 1, &get_current_frame().render_fence, true, 1000000000));
	get_current_frame().deletion_queue.flush();

    //request image from the swapchain
	uint32_t swapchain_image_index;
	VK_CHECK(vkAcquireNextImageKHR(device.logical_device, swapchain.handle, 1000000000, get_current_frame().swapchain_semaphore, nullptr, &swapchain_image_index));

    VK_CHECK(vkResetFences(device.logical_device, 1, &get_current_frame().render_fence));

	// now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(get_current_frame().main_command_buffer, 0));

    //naming it cmd for shorter writing
	VkCommandBuffer cmd = get_current_frame().main_command_buffer;

	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmd_begin_info = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	draw_extent.width = draw_image.extent.width;
	draw_extent.height = draw_image.extent.height;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));	

	// transition our main draw image into general layout so we can write into it
	// we will overwrite it all so we dont care about what was the older layout
	transition_image(cmd, draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	draw_background(cmd);

	//transition the draw image and the swapchain image into their correct transfer layouts
	transition_image(cmd, draw_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	transition_image(cmd, swapchain.images[swapchain_image_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// execute a copy from the draw image into the swapchain
	copy_image_to_image(cmd, draw_image.image, swapchain.images[swapchain_image_index], draw_extent, swapchain.extent);

	// set swapchain image layout to Present so we can show it on the screen
	transition_image(cmd, swapchain.images[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));

    //prepare the submission to the queue. 
	//we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
	//we will signal the _renderSemaphore, to signal that rendering has finished

	VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);	
	
	VkSemaphoreSubmitInfo waitinfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,get_current_frame().swapchain_semaphore);
	VkSemaphoreSubmitInfo signalinfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().render_semaphore);	
	
	VkSubmitInfo2 submit = submit_info(&cmdinfo,&signalinfo,&waitinfo);	

	//submit command buffer to the queue and execute it.
	// render_fence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(device.graphics_queue, 1, &submit, get_current_frame().render_fence));

    //prepare present
	// this will put the image we just rendered to into the visible window.
	// we want to wait on the _renderSemaphore for that, 
	// as its necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentinfo = {};
	presentinfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentinfo.pNext = nullptr;
	presentinfo.pSwapchains = &swapchain.handle;
	presentinfo.swapchainCount = 1;

	presentinfo.pWaitSemaphores = &get_current_frame().render_semaphore;
	presentinfo.waitSemaphoreCount = 1;

	presentinfo.pImageIndices = &swapchain_image_index;

	VK_CHECK(vkQueuePresentKHR(device.graphics_queue, &presentinfo));

	//increase the number of frames drawn
	frame_count++;
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

void Backend::draw_background(VkCommandBuffer cmd)
{
	//make a clear-color from frame number. This will flash with a 120 frame period.
	VkClearColorValue clearValue;
	float flash = std::abs(std::sin(frame_count / 120.f));
	clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

	VkImageSubresourceRange clearRange = image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	//clear image
	vkCmdClearColorImage(cmd, draw_image.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}
}