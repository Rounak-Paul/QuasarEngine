#include "Renderer.h"
#include "VulkanInitInfo.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "VulkanPipeline.h"

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

b8 Renderer::init(const std::string& name, const Window& window) {
    if (!initialize_validation_layers()) return false;
    fetch_api_version();

    if (!create_instance(name)) return false;
    setup_debug_messenger();

    if (!create_surface(window)) return false;
    if (!create_device()) return false;
    
    if (!create_swapchain(window)) return false;
    if (!create_allocator()) return false;

    if (!create_draw_image(window)) return false;
    if (!create_command_buffers()) return false;
    if (!create_sync_objects()) return false;

    create_descriptors();
    create_pipelines();

    init_imgui(window);

    return true;
}

b8 Renderer::begin_frame()
{
    // imgui new frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
		
    if (ImGui::Begin("background")) {
        
        ComputePipeline& selected = backgroundEffects[currentBackgroundEffect];
    
        ImGui::Text("Selected effect: %s", selected.name.c_str());
    
        ImGui::SliderInt("Effect Index", &currentBackgroundEffect,0, backgroundEffects.size() - 1);
    
        ImGui::InputFloat4("data1",(float*)& selected.data.data1);
        ImGui::InputFloat4("data2",(float*)& selected.data.data2);
        ImGui::InputFloat4("data3",(float*)& selected.data.data3);
        ImGui::InputFloat4("data4",(float*)& selected.data.data4);
        
        ImGui::Image(_draw_texture, {(f32)_draw_extent.width, (f32)_draw_extent.height});
    }
    ImGui::End();

    ImGui::Render();

    // wait until the gpu has finished rendering the last frame. Timeout of 1 second
	VK_CHECK(vkWaitForFences(_device.logical_device, 1, &get_current_frame().render_fence, true, 1000000000));
    get_current_frame().deletion_queue.flush();
	VK_CHECK(vkResetFences(_device.logical_device, 1, &get_current_frame().render_fence));

    //request image from the swapchain
	VK_CHECK(vkAcquireNextImageKHR(_device.logical_device, _swapchain.handle, 1000000000, get_current_frame().swapchain_semaphore, nullptr, &_swapchain.image_index));

	VkCommandBuffer cmd = get_current_frame().main_command_buffer;

	// now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	//begin the command buffer recording.
	VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	_draw_extent.width = _draw_image.extent.width;
	_draw_extent.height = _draw_image.extent.height;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));	

    return true;
}

void Renderer::draw_background()
{
    VkCommandBuffer cmd = get_current_frame().main_command_buffer;

    // transition our main draw image into general layout so we can write into it
	// we will overwrite it all so we dont care about what was the older layout
	transition_image(_device, cmd, _draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    ComputePipeline& effect = backgroundEffects[currentBackgroundEffect];

	// bind the background compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	// bind the descriptor set containing the draw image for the compute pipeline
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.layout, 0, 1, &_draw_image_descriptors, 0, nullptr);

	vkCmdPushConstants(cmd, effect.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, std::ceil(_draw_extent.width / 16.0), std::ceil(_draw_extent.height / 16.0), 1);

    //transition the draw image and the swapchain image into their correct transfer layouts
	transition_image(_device, cmd, _draw_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void Renderer::draw_geometry()
{
    VkCommandBuffer cmd = get_current_frame().main_command_buffer;

    //begin a render pass  connected to our draw image
	VkRenderingAttachmentInfo colorAttachment = attachment_info(_draw_image.view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = rendering_info(_draw_extent, &colorAttachment, nullptr);
	if (_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT) {
        vkCmdBeginRendering(cmd, &renderInfo);
    }
    else {
        _device.vkCmdBeginRenderingKHR(cmd, &renderInfo);
    } 

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

	//set dynamic viewport and scissor
	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = _draw_extent.width;
	viewport.height = _draw_extent.height;
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = _draw_extent.width;
	scissor.extent.height = _draw_extent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	//launch a draw command to draw 3 vertices
	vkCmdDraw(cmd, 3, 1, 0, 0);

	if (_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT) {
        vkCmdEndRendering(cmd);
    }
    else {
        _device.vkCmdEndRenderingKHR(cmd);
    } 

    transition_image(_device, cmd, _draw_image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Renderer::end_frame()
{
    //prepare the submission to the queue. 
	//we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
	//we will signal the _renderSemaphore, to signal that rendering has finished
    VkCommandBuffer cmd = get_current_frame().main_command_buffer;

	// transition_image(_device, cmd, _swapchain.images[_swapchain.image_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// execute a copy from the draw image into the swapchain
	// copy_image_to_image(_device, cmd, _draw_image.image, _swapchain.images[_swapchain.image_index], _draw_extent, _swapchain.extent);

	// set swapchain image layout to Attachment Optimal so we can draw it
	transition_image(_device, cmd, _swapchain.images[_swapchain.image_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	//draw imgui into the swapchain image
	draw_imgui(cmd,  _swapchain.views[_swapchain.image_index]);

	// set swapchain image layout to Present so we can draw it
	transition_image(_device, cmd, _swapchain.images[_swapchain.image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);	
	
	VkSemaphoreSubmitInfo waitInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame().swapchain_semaphore);
	VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().render_semaphore);	
	
	VkSubmitInfo2 submit = submit_info(&cmdinfo,&signalInfo,&waitInfo);	

	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
    if (_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT) {
        VK_CHECK(vkQueueSubmit2(_device.graphics_queue, 1, &submit, get_current_frame().render_fence));
    }
    else {
        VK_CHECK(_device.vkQueueSubmit2KHR(_device.graphics_queue, 1, &submit, get_current_frame().render_fence));
    } 
    
    //prepare present
	// this will put the image we just rendered to into the visible window.
	// we want to wait on the _renderSemaphore for that, 
	// as its necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchain.handle;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame().render_semaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &_swapchain.image_index;

	VK_CHECK(vkQueuePresentKHR(_device.graphics_queue, &presentInfo));

	//increase the number of frames drawn
	_frame_number++;
}

void Renderer::immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function)
{
    VK_CHECK(vkResetFences(_device.logical_device, 1, &_imm_fence));
	VK_CHECK(vkResetCommandBuffer(_imm_command_buffer, 0));

	VkCommandBuffer cmd = _imm_command_buffer;

	VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit = submit_info(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(_device.graphics_queue, 1, &submit, _imm_fence));

	VK_CHECK(vkWaitForFences(_device.logical_device, 1, &_imm_fence, true, 9999999999));
}

void Renderer::shutdown()
{
    // Wait for device to finish all operations before cleanup
    if (_device.logical_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(_device.logical_device);
    }

    for (int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroyCommandPool(_device.logical_device, _frames[i].command_pool, nullptr);

		//destroy sync objects
		vkDestroyFence(_device.logical_device, _frames[i].render_fence, nullptr);
		vkDestroySemaphore(_device.logical_device, _frames[i].render_semaphore, nullptr);
		vkDestroySemaphore(_device.logical_device, _frames[i].swapchain_semaphore, nullptr);

        _frames[i].deletion_queue.flush();
	}

    //flush the global deletion queue
    _main_deletion_queue.flush();

    vulkan_swapchain_destroy(_device, _swapchain);

    // Destroy surface
    if (_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        _surface = VK_NULL_HANDLE;
    }
    
    if (_device.logical_device != VK_NULL_HANDLE) {
        vulkan_device_destroy(_instance, _device);
        _device.logical_device = VK_NULL_HANDLE;
    }

    if (_validation_enabled && _debug_messenger != VK_NULL_HANDLE) {
        auto destroy_func = (PFN_vkDestroyDebugUtilsMessengerEXT) 
            vkGetInstanceProcAddr(_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_func != nullptr) {
            destroy_func(_instance, _debug_messenger, nullptr);
            _debug_messenger = VK_NULL_HANDLE;
        }
    }
    
    if (_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(_instance, nullptr);
        _instance = VK_NULL_HANDLE;
    }
    
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
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
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

b8 Renderer::initialize_validation_layers() {
    if (_validation_enabled && !check_validation_layer_support()) {
        LOG_ERROR("validation layers requested but not available");
        return false;
    }
    return true;
}

void Renderer::fetch_api_version() {
    u32 api_version = 0;
    vkEnumerateInstanceVersion(&api_version);
    _api_major = VK_VERSION_MAJOR(api_version);
    _api_minor = VK_VERSION_MINOR(api_version);
    _api_patch = VK_VERSION_PATCH(api_version);
}

b8 Renderer::create_instance(const std::string& name) {
    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = name.c_str();
    app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.pEngineName = "Quasar Engine";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app_info.apiVersion = VK_MAKE_API_VERSION(0, _api_major, _api_minor, _api_patch);

    auto extensions = get_required_extensions();

    VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app_info;
#ifdef QS_PLATFORM_APPLE
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    create_info.enabledExtensionCount = static_cast<u32>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    if (_validation_enabled) {
        create_info.enabledLayerCount = static_cast<u32>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();
        populate_debug_messenger_create_info(debug_create_info);
        create_info.pNext = &debug_create_info;
    } else {
        create_info.enabledLayerCount = 0;
        create_info.pNext = nullptr;
    }

    if (vkCreateInstance(&create_info, nullptr, &_instance) != VK_SUCCESS) {
        LOG_FATAL("Failed to create vulkan instance!");
        return false;
    }

    return true;
}

void Renderer::setup_debug_messenger() {
    if (_validation_enabled) {
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
        populate_debug_messenger_create_info(debug_create_info);
        if (create_debug_utils_messenger_ext(_instance, &debug_create_info, nullptr, &_debug_messenger) != VK_SUCCESS) {
            LOG_WARN("Failed to create vulkan debug messenger! Validation errors may be omitted.");
        }
    }
}

b8 Renderer::create_surface(const Window& window) {
    if (!platform_create_vulkan_surface(_instance, window, &_surface)) {
        LOG_FATAL("Failed to create primary surface for drawing!");
        return false;
    }
    return true;
}

b8 Renderer::create_device() {
    if (!vulkan_device_create(_instance, _surface, _device)) {
        LOG_ERROR("Failed to create device!");
        return false;
    }
    return true;
}

b8 Renderer::create_swapchain(const Window& window) {
    Extent2D extent = window.get_extent();
    vulkan_swapchain_create(_device, _surface, extent.width, extent.height, _swapchain);
    return true;
}

b8 Renderer::create_allocator() {
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _device.physical_device;
    allocatorInfo.device = _device.logical_device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; // literally enables c-style pointers for device memory

    if (vmaCreateAllocator(&allocatorInfo, &_allocator) != VK_SUCCESS) {
        LOG_ERROR("Failed to create memory allocator!");
        return false;
    }

    _main_deletion_queue.push_function([&]() {
        vmaDestroyAllocator(_allocator);
    });

    return true;
}

b8 Renderer::create_draw_image(const Window& window) {
    Extent2D extent = window.get_extent();
    VkExtent3D drawImageExtent = { extent.width, extent.height, 1 };

    _draw_image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    _draw_image.extent = drawImageExtent;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo image_info = image_create_info(_draw_image.format, usage, drawImageExtent);
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    vmaCreateImage(_allocator, &image_info, &alloc_info, &_draw_image.image, &_draw_image.allocation, nullptr);

    VkImageViewCreateInfo view_info = imageview_create_info(_draw_image.format, _draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device.logical_device, &view_info, nullptr, &_draw_image.view));

    _main_deletion_queue.push_function([=, this]() {
        vkDestroyImageView(_device.logical_device, _draw_image.view, nullptr);
        vmaDestroyImage(_allocator, _draw_image.image, _draw_image.allocation);
    });

    return true;
}

b8 Renderer::create_command_buffers() {
    VkCommandPoolCreateInfo pool_info = command_pool_create_info(
        _device.graphics_queue_index,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    );

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(_device.logical_device, &pool_info, nullptr, &_frames[i].command_pool));

        VkCommandBufferAllocateInfo cmd_info = command_buffer_allocate_info(_frames[i].command_pool, 1);
        VK_CHECK(vkAllocateCommandBuffers(_device.logical_device, &cmd_info, &_frames[i].main_command_buffer));
    }

    VK_CHECK(vkCreateCommandPool(_device.logical_device, &pool_info, nullptr, &_imm_command_pool));

	// allocate the command buffer for immediate submits
	VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(_imm_command_pool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device.logical_device, &cmdAllocInfo, &_imm_command_buffer));

	_main_deletion_queue.push_function([=, this]() { 
        vkDestroyCommandPool(_device.logical_device, _imm_command_pool, nullptr);
	});

    return true;
}

b8 Renderer::create_sync_objects() {
    VkFenceCreateInfo fence_info = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphore_info = semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(_device.logical_device, &fence_info, nullptr, &_frames[i].render_fence));
        VK_CHECK(vkCreateSemaphore(_device.logical_device, &semaphore_info, nullptr, &_frames[i].swapchain_semaphore));
        VK_CHECK(vkCreateSemaphore(_device.logical_device, &semaphore_info, nullptr, &_frames[i].render_semaphore));
    }

    VK_CHECK(vkCreateFence(_device.logical_device, &fence_info, nullptr, &_imm_fence));
	_main_deletion_queue.push_function([=, this]() { vkDestroyFence(_device.logical_device, _imm_fence, nullptr); });

    return true;
}
void Renderer::create_descriptors()
{
    //create a descriptor pool that will hold 10 sets with 1 image each
	std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
	};

	global_descriptor_allocator.init_pool(_device.logical_device, 10, sizes);

	//make the descriptor set layout for our compute draw
	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_draw_image_descriptor_layout = builder.build(_device.logical_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

    //allocate a descriptor set for our draw image
	_draw_image_descriptors = global_descriptor_allocator.allocate(_device.logical_device,_draw_image_descriptor_layout);	

	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imgInfo.imageView = _draw_image.view;
	
	VkWriteDescriptorSet drawImageWrite = {};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWrite.pNext = nullptr;
	
	drawImageWrite.dstBinding = 0;
	drawImageWrite.dstSet = _draw_image_descriptors;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(_device.logical_device, 1, &drawImageWrite, 0, nullptr);

	//make sure both the descriptor allocator and the new layout get cleaned up properly
	_main_deletion_queue.push_function([&]() {
		global_descriptor_allocator.destroy_pool(_device.logical_device);

		vkDestroyDescriptorSetLayout(_device.logical_device, _draw_image_descriptor_layout, nullptr);
	});
}
void Renderer::create_pipelines()
{
    create_background_pipelines();
    create_triangle_pipeline();
}
void Renderer::create_background_pipelines()
{
    // Set up shared pipeline layout
    VkPushConstantRange push_range{};
    push_range.offset = 0;
    push_range.size = sizeof(ComputePushConstants);
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayout compute_set_layouts[] = { _draw_image_descriptor_layout };

    ComputePipelineConfig base_cfg{};
    base_cfg.device = _device.logical_device;
    base_cfg.shader_path = nullptr; // will set per-shader below
    base_cfg.set_layouts = compute_set_layouts;
    base_cfg.set_layout_count = 1;
    base_cfg.push_constants = &push_range;
    base_cfg.push_constant_count = 1;
    base_cfg.deletion_queue = &_main_deletion_queue;

    // === Create Gradient Effect ===
    ComputePipeline gradient{};
    gradient.name = "gradient";
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    base_cfg.shader_path = "../Assets/shaders/gradient_color.comp";

    if (!create_compute_pipeline(base_cfg, gradient)) {
        LOG_FATAL("Failed to create compute pipeline for gradient");
    }

    // === Create Sky Effect ===
    ComputePipeline sky{};
    sky.name = "sky";
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    base_cfg.shader_path = "../Assets/shaders/sky.comp";

    if (!create_compute_pipeline(base_cfg, sky)) {
        LOG_FATAL("Failed to create compute pipeline for sky");
    }

    // === Add to array ===
    backgroundEffects.push_back(gradient);
    backgroundEffects.push_back(sky);
}

void Renderer::create_triangle_pipeline()
{
    VkShaderModule triangleFragShader;
	if (!load_shader_module("../Assets/shaders/colored_triangle.frag", _device.logical_device, SHADER_STAGE_FRAGMENT, &triangleFragShader)) {
		LOG_ERROR("Error when building the triangle fragment shader module");
	}
	else {
		LOG_DEBUG("Triangle fragment shader succesfully loaded");
	}

	VkShaderModule triangleVertexShader;
	if (!load_shader_module("../Assets/shaders/colored_triangle.vert", _device.logical_device, SHADER_STAGE_VERTEX, &triangleVertexShader)) {
		LOG_ERROR("Error when building the triangle vertex shader module");
	}
	else {
		LOG_DEBUG("Triangle vertex shader succesfully loaded");
	}
	
	//build the pipeline layout that controls the inputs/outputs of the shader
	//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = pipeline_layout_create_info();
	VK_CHECK(vkCreatePipelineLayout(_device.logical_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

    PipelineBuilder pipelineBuilder;

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
	//connecting the vertex and pixel shaders to the pipeline
	pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
	//it will draw triangles
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	//filled triangles
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	//no backface culling
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	//no multisampling
	pipelineBuilder.set_multisampling_none();
	//no blending
	pipelineBuilder.disable_blending();
	//no depth testing
	pipelineBuilder.disable_depthtest();

	//connect the image format we will draw into, from draw image
	pipelineBuilder.set_color_attachment_format(_draw_image.format);
	pipelineBuilder.set_depth_format(VK_FORMAT_UNDEFINED);

	//finally build the pipeline
	_trianglePipeline = pipelineBuilder.build_pipeline(_device.logical_device);

	//clean structures
	vkDestroyShaderModule(_device.logical_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device.logical_device, triangleVertexShader, nullptr);

	_main_deletion_queue.push_function([&]() {
		vkDestroyPipelineLayout(_device.logical_device, _trianglePipelineLayout, nullptr);
		vkDestroyPipeline(_device.logical_device, _trianglePipeline, nullptr);
	});
}

void Renderer::init_imgui(const Window& window)
{
    // 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device.logical_device, &pool_info, nullptr, &imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// this initializes imgui for SDL
	ImGui_ImplGlfw_InitForVulkan(window.get_GLFWwindow(), true);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _device.physical_device;
	init_info.Device = _device.logical_device;
	init_info.Queue = _device.graphics_queue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchain.image_format.format;
	

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

    {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 1.0f;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        sampler_info.unnormalizedCoordinates = VK_FALSE;

        vkCreateSampler(_device.logical_device, &sampler_info, nullptr, &_imm_sampler);
    }

    _draw_texture = (ImTextureID)ImGui_ImplVulkan_AddTexture(_imm_sampler, _draw_image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// add the destroy the imgui created structures
	_main_deletion_queue.push_function([=, this]() {
        vkDestroySampler(_device.logical_device, _imm_sampler, nullptr);
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device.logical_device, imguiPool, nullptr);
	});
}
void Renderer::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment = attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = rendering_info(_swapchain.extent, &colorAttachment, nullptr);

    if (_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT) {
        vkCmdBeginRendering(cmd, &renderInfo);
    }
    else {
        _device.vkCmdBeginRenderingKHR(cmd, &renderInfo);
    } 

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    if (_device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_13_FEATURES_BIT) {
        vkCmdEndRendering(cmd);
    }
    else {
        _device.vkCmdEndRenderingKHR(cmd);
    } 
}
} // namespace Quasar