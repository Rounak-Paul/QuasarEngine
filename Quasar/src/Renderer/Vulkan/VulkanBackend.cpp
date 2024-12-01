#include "VulkanBackend.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanImgui.h"
#include "VulkanRenderpass.h"
#include "VulkanCommandbuffer.h"
#include "VulkanFramebuffer.h"
#include "VulkanFence.h"
#include "VulkanUtils.h"
#include "VulkanShader.h"
#include "VulkanBuffer.h"

#include <Math/Math.h>

namespace Quasar::Renderer
{

u32 cached_framebuffer_width = 0;
u32 cached_framebuffer_height = 0;

b8 Backend::init(String &app_name, Window *main_window)
{
#ifdef QS_DEBUG 
    if (!check_validation_layer_support()) {
        LOG_FATAL("Debug mode validation layers requested, but not available!");
        return false;
    }
#endif

    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = app_name.c_str();
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
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    populate_debug_messenger_create_info(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
#else
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;
    createInfo.pNext = nullptr;
#endif

    VkResult result = vkCreateInstance(&createInfo, context.allocator, &context.instance);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Instance creation failed with VkResult: %d", result); 
        return false;
    }

#ifdef QS_DEBUG
    result = create_debug_messenger();
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to set up debug messenger!");
        return false;
    }
#endif

    // TODO: implement multi-threading.
    multithreading_enabled = false;

    // Surface
    LOG_DEBUG("Creating Vulkan surface...");
    if (!create_vulkan_surface(main_window)) {
        LOG_ERROR("Failed to create platform surface!");
        return false;
    }

    // Device creation
    if (!vulkan_device_create(&context)) {
        LOG_ERROR("Failed to create device!");
        return false;
    }

    // Swapchain
    context.framebuffer_width = main_window->get_extent().width;
    context.framebuffer_height = main_window->get_extent().height;
    if (!vulkan_swapchain_create(&context, context.framebuffer_width, context.framebuffer_height, &context.swapchain)) {
        LOG_ERROR("Failed to create swapchain!");
        return false;
    }

    vulkan_renderpass_create(
        &context,
        &context.main_renderpass,
        0, 0, context.framebuffer_width, context.framebuffer_height,
        0.0f, 0.0f, 0.2f, 1.0f,
        1.0f,
        0
    );

    // Swapchain framebuffers.
    context.swapchain.framebuffers.resize(context.swapchain.image_count);
    regenerate_framebuffers(&context.swapchain, &context.main_renderpass);

    // Create command buffers.
    create_command_buffers();

    // Create sync objects.
    context.image_available_semaphores.resize(context.swapchain.max_frames_in_flight);
    context.queue_complete_semaphores.resize(context.swapchain.max_frames_in_flight);
    context.in_flight_fences.resize(context.swapchain.max_frames_in_flight);

    for (u8 i = 0; i < context.swapchain.max_frames_in_flight; ++i) {
        VkSemaphoreCreateInfo semaphore_create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(context.device.logical_device, &semaphore_create_info, context.allocator, &context.image_available_semaphores[i]);
        vkCreateSemaphore(context.device.logical_device, &semaphore_create_info, context.allocator, &context.queue_complete_semaphores[i]);
        // Create the fence in a signaled state, indicating that the first frame has already been "rendered".
        // This will prevent the application from waiting indefinitely for the first frame to render since it
        // cannot be rendered until a frame is "rendered" before it.
        vulkan_fence_create(&context, true, &context.in_flight_fences[i]);
    }

    // In flight fences should not yet exist at this point, so clear the list. These are stored in pointers
    // because the initial state should be 0, and will be 0 when not in use. Acutal fences are not owned
    // by this list.
    context.images_in_flight.resize(context.swapchain.image_count);
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        context.images_in_flight[i] = 0;
    }

    // Create builtin shaders
    if (!vulkan_object_shader_create(&context, &context.object_shader)) {
        LOG_ERROR("Error loading built-in basic_lighting shader.");
        return false;
    }

    create_buffers();

    // TODO: temporary test code
    const u32 vert_count = 4;
    Math::Vertex3d verts[vert_count];
    memset(verts, 0, sizeof(Math::Vertex3d) * vert_count);
    const f32 f = 10.0f;
    verts[0].position.x = -0.5 * f;
    verts[0].position.y = -0.5 * f;
    verts[1].position.y = 0.5 * f;
    verts[1].position.x = 0.5 * f;
    verts[2].position.x = -0.5 * f;
    verts[2].position.y = 0.5 * f;
    verts[3].position.x = 0.5 * f;
    verts[3].position.y = -0.5 * f;
    const u32 index_count = 6;
    u32 indices[index_count] = {0, 1, 2, 0, 3, 1};
    upload_data_range(context.device.graphics_command_pool, 0, context.device.graphics_queue, &context.object_vertex_buffer, 0, sizeof(Math::Vertex3d) * vert_count, verts);
    upload_data_range(context.device.graphics_command_pool, 0, context.device.graphics_queue, &context.object_index_buffer, 0, sizeof(u32) * index_count, indices);
    // TODO: end temp code

    LOG_DEBUG("Vulkan renderer initialized successfully.");

    return true;
}

void Backend::shutdown()
{
    vkDeviceWaitIdle(context.device.logical_device);

    // Destroy buffers
    vulkan_buffer_destroy(&context, &context.object_vertex_buffer);
    vulkan_buffer_destroy(&context, &context.object_index_buffer);

    vulkan_object_shader_destroy(&context, &context.object_shader);

    // Sync objects
    for (u8 i = 0; i < context.swapchain.max_frames_in_flight; ++i) {
        if (context.image_available_semaphores[i]) {
            vkDestroySemaphore(
                context.device.logical_device,
                context.image_available_semaphores[i],
                context.allocator);
            context.image_available_semaphores[i] = 0;
        }
        if (context.queue_complete_semaphores[i]) {
            vkDestroySemaphore(
                context.device.logical_device,
                context.queue_complete_semaphores[i],
                context.allocator);
            context.queue_complete_semaphores[i] = 0;
        }
        vulkan_fence_destroy(&context, &context.in_flight_fences[i]);
    }
    context.image_available_semaphores.destroy();
    context.queue_complete_semaphores.destroy();
    context.in_flight_fences.destroy();
    context.images_in_flight.destroy();

    // Command buffers
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        if (context.graphics_command_buffers[i].handle) {
            vulkan_command_buffer_free(
                &context,
                context.device.graphics_command_pool,
                &context.graphics_command_buffers[i]);
            context.graphics_command_buffers[i].handle = 0;
        }
    }
    context.graphics_command_buffers.destroy();

    // Destroy framebuffers
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        vulkan_framebuffer_destroy(&context, &context.swapchain.framebuffers[i]);
    }

    vulkan_renderpass_destroy(&context, &context.main_renderpass);
    vulkan_swapchain_destroy(&context, &context.swapchain);
    vulkan_device_destroy(&context);
    vkDestroySurfaceKHR(context.instance, context.surface, context.allocator);
#ifdef QS_DEBUG
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(context.instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(context.instance, context.debug_messenger, context.allocator);
    }
#endif
    vkDestroyInstance(context.instance, context.allocator);
}

void Backend::resize(u32 width, u32 height)
{
    cached_framebuffer_width = width;
    cached_framebuffer_height = height;
    context.framebuffer_size_generation++;
    LOG_DEBUG("Vulkan renderer backend resized [w, h, gen] [%i, %i, %llu]", width, height, context.framebuffer_size_generation);
}

b8 Backend::begin_frame(f32 dt)
{
    vulkan_device* device = &context.device;
    // Check if recreating swap chain and boot out.
    if (context.recreating_swapchain) {
        VkResult result = vkDeviceWaitIdle(device->logical_device);
        if (!vulkan_result_is_success(result)) {
            LOG_ERROR("vulkan_renderer_backend_begin_frame vkDeviceWaitIdle (1) failed: '%s'", vulkan_result_string(result, true));
            return false;
        }
        LOG_INFO("Recreating swapchain, booting.");
        return false;
    }
    // Check if the framebuffer has been resized. If so, a new swapchain must be created.
    if (context.framebuffer_size_generation != context.framebuffer_size_last_generation) {
        VkResult result = vkDeviceWaitIdle(device->logical_device);
        if (!vulkan_result_is_success(result)) {
            LOG_ERROR("vulkan_renderer_backend_begin_frame vkDeviceWaitIdle (2) failed: '%s'", vulkan_result_string(result, true));
            return false;
        }
        // If the swapchain recreation failed (because, for example, the window was minimized),
        // boot out before unsetting the flag.
        if (!recreate_swapchain()) {
            return false;
        }
        LOG_INFO("Resized, booting.");
        return false;
    }
    // Wait for the execution of the current frame to complete. The fence being free will allow this one to move on.
    if (!vulkan_fence_wait(
            &context,
            &context.in_flight_fences[context.current_frame],
            UINT64_MAX)) {
        LOG_WARN("In-flight fence wait failure!");
        return false;
    }
    // Acquire the next image from the swap chain. Pass along the semaphore that should signaled when this completes.
    // This same semaphore will later be waited on by the queue submission to ensure this image is available.
    if (!vulkan_swapchain_acquire_next_image_index(
            &context,
            &context.swapchain,
            UINT64_MAX,
            context.image_available_semaphores[context.current_frame],
            0,
            &context.image_index)) {
        return false;
    }
    // Begin recording commands.
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];
    vulkan_command_buffer_reset(command_buffer);
    vulkan_command_buffer_begin(command_buffer, false, false, false);
    // Dynamic state
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = (f32)context.framebuffer_height;
    viewport.width = (f32)context.framebuffer_width;
    viewport.height = -(f32)context.framebuffer_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    // Scissor
    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = context.framebuffer_width;
    scissor.extent.height = context.framebuffer_height;
    vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);
    context.main_renderpass.w = context.framebuffer_width;
    context.main_renderpass.h = context.framebuffer_height;
    // Begin the render pass.
    vulkan_renderpass_begin(
        command_buffer,
        &context.main_renderpass,
        context.swapchain.framebuffers[context.image_index].handle);

    return true;
}

void Backend::update_global_state(Math::Mat4 projection, Math::Mat4 view, Math::Vec3 view_position, Math::Vec4 ambient_colour, i32 mode) {
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];
    vulkan_object_shader_use(&context, &context.object_shader);
    context.object_shader.global_ubo.projection = projection;
    context.object_shader.global_ubo.view = view;
    // TODO: other ubo properties
    vulkan_object_shader_update_global_state(&context, &context.object_shader);

    // TODO: temporary test code
    vulkan_object_shader_use(&context, &context.object_shader);
    // Bind vertex buffer at offset.
    VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(command_buffer->handle, 0, 1, &context.object_vertex_buffer.handle, (VkDeviceSize*)offsets);
    // Bind index buffer at offset.
    vkCmdBindIndexBuffer(command_buffer->handle, context.object_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
    // Issue the draw.
    vkCmdDrawIndexed(command_buffer->handle, 6, 1, 0, 0, 0);
    // TODO: end temporary test code
}

b8 Backend::end_frame(f32 dt)
{
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];
    // End renderpass
    vulkan_renderpass_end(command_buffer, &context.main_renderpass);
    vulkan_command_buffer_end(command_buffer);
    // Make sure the previous frame is not using this image (i.e. its fence is being waited on)
    if (context.images_in_flight[context.image_index] != VK_NULL_HANDLE) {  // was frame
        vulkan_fence_wait(
            &context,
            context.images_in_flight[context.image_index],
            UINT64_MAX);
    }
    // Mark the image fence as in-use by this frame.
    context.images_in_flight[context.image_index] = &context.in_flight_fences[context.current_frame];
    // Reset the fence for use on the next frame
    vulkan_fence_reset(&context, &context.in_flight_fences[context.current_frame]);
    // Submit the queue and wait for the operation to complete.
    // Begin queue submission
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    // Command buffer(s) to be executed.
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer->handle;
    // The semaphore(s) to be signaled when the queue is complete.
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &context.queue_complete_semaphores[context.current_frame];
    // Wait semaphore ensures that the operation cannot begin until the image is available.
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &context.image_available_semaphores[context.current_frame];
    // Each semaphore waits on the corresponding pipeline stage to complete. 1:1 ratio.
    // VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT prevents subsequent colour attachment
    // writes from executing until the semaphore signals (i.e. one frame is presented at a time)
    VkPipelineStageFlags flags[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = flags;
    VkResult result = vkQueueSubmit(
        context.device.graphics_queue,
        1,
        &submit_info,
        context.in_flight_fences[context.current_frame].handle);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkQueueSubmit failed with result: %s", vulkan_result_string(result, true));
        return false;
    }
    vulkan_command_buffer_update_submitted(command_buffer);
    // End queue submission
    // Give the image back to the swapchain.
    vulkan_swapchain_present(
        &context,
        &context.swapchain,
        context.device.graphics_queue,
        context.device.present_queue,
        context.queue_complete_semaphores[context.current_frame],
        context.image_index);
    return true;
}

b8 Backend::check_validation_layer_support() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name : validationLayers) {
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

std::vector<const char*> Backend::get_required_extensions() {
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

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
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

void Backend::populate_debug_messenger_create_info(
    VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debug_callback;
    createInfo.pUserData = nullptr;  // Optional
}

VkResult Backend::create_debug_messenger() {
#ifdef QS_DEBUG
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
    populate_debug_messenger_create_info(debugCreateInfo);
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(context.instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(context.instance, &debugCreateInfo, context.allocator, &context.debug_messenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
#endif
    return VK_SUCCESS;
}

b8 Backend::create_vulkan_surface(Window* window)
{
    GLFWwindow* w = window->get_GLFWwindow();
    auto res = glfwCreateWindowSurface(context.instance, w, context.allocator, &context.surface);
    if (res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create Window Surface, VkResult: %d", res);
        return false;
    }
    return true;
}
void Backend::create_command_buffers()
{
    if (context.graphics_command_buffers.is_empty()) {
        context.graphics_command_buffers.resize(context.swapchain.image_count);
        for (u32 i = 0; i < context.swapchain.image_count; ++i) {
            memset(&context.graphics_command_buffers[i], 0, sizeof(vulkan_command_buffer));
        }
    }
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        if (context.graphics_command_buffers[i].handle) {
            vulkan_command_buffer_free(
                &context,
                context.device.graphics_command_pool,
                &context.graphics_command_buffers[i]);
        }
        memset(&context.graphics_command_buffers[i], 0, sizeof(vulkan_command_buffer));
        vulkan_command_buffer_allocate(
            &context,
            context.device.graphics_command_pool,
            true,
            &context.graphics_command_buffers[i]);
    }
    LOG_DEBUG("Vulkan command buffers created.");
}
void Backend::regenerate_framebuffers(vulkan_swapchain *swapchain, vulkan_renderpass *renderpass)
{
    for (u32 i = 0; i < swapchain->image_count; ++i) {
        // TODO: make this dynamic based on the currently configured attachments
        u32 attachment_count = 2;
        VkImageView attachments[] = {
            swapchain->views[i],
            swapchain->depth_attachment.view};
        vulkan_framebuffer_create(
            &context,
            renderpass,
            context.framebuffer_width,
            context.framebuffer_height,
            attachment_count,
            attachments,
            &context.swapchain.framebuffers[i]);
    }
}

b8 Backend::recreate_swapchain() {
    // If already being recreated, do not try again.
    if (context.recreating_swapchain) {
        LOG_DEBUG("recreate_swapchain called when already recreating. Booting.");
        return false;
    }

    // Detect if the window is too small to be drawn to
    if (context.framebuffer_width == 0 || context.framebuffer_height == 0) {
        LOG_DEBUG("recreate_swapchain called when window is < 1 in a dimension. Booting.");
        return false;
    }

    // Mark as recreating if the dimensions are valid.
    context.recreating_swapchain = true;

    // Wait for any operations to complete.
    vkDeviceWaitIdle(context.device.logical_device);
    // Clear these out just in case.
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        context.images_in_flight[i] = 0;
    }

    // Requery support
    vulkan_device_query_swapchain_support(
        context.device.physical_device,
        context.surface,
        &context.device.swapchain_support);
    vulkan_device_detect_depth_format(&context.device);
    vulkan_swapchain_recreate(
        &context,
        cached_framebuffer_width,
        cached_framebuffer_height,
        &context.swapchain);
    
    // Sync the framebuffer size with the cached sizes.
    context.framebuffer_width = cached_framebuffer_width;
    context.framebuffer_height = cached_framebuffer_height;
    context.main_renderpass.w = context.framebuffer_width;
    context.main_renderpass.h = context.framebuffer_height;
    cached_framebuffer_width = 0;
    cached_framebuffer_height = 0;

    // Update framebuffer size generation.
    context.framebuffer_size_last_generation = context.framebuffer_size_generation;

    // cleanup swapchain
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        vulkan_command_buffer_free(&context, context.device.graphics_command_pool, &context.graphics_command_buffers[i]);
    }
    
    // Framebuffers.
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        vulkan_framebuffer_destroy(&context, &context.swapchain.framebuffers[i]);
    }
    context.main_renderpass.x = 0;
    context.main_renderpass.y = 0;
    context.main_renderpass.w = context.framebuffer_width;
    context.main_renderpass.h = context.framebuffer_height;
    regenerate_framebuffers(&context.swapchain, &context.main_renderpass);
    create_command_buffers();

    // Clear the recreating flag.
    context.recreating_swapchain = false;
    return true;
}

b8 Backend::create_buffers()
{
    VkMemoryPropertyFlagBits memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const u64 vertex_buffer_size = sizeof(Math::Vertex3d) * 1024 * 1024;
    if (!vulkan_buffer_create(
            &context,
            vertex_buffer_size,
            (VkBufferUsageFlagBits)(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
            memory_property_flags,
            true,
            &context.object_vertex_buffer)) {
        LOG_ERROR("Error creating vertex buffer.");
        return false;
    }
    context.geometry_vertex_offset = 0;
    const u64 index_buffer_size = sizeof(u32) * 1024 * 1024;
    if (!vulkan_buffer_create(
            &context,
            index_buffer_size,
            (VkBufferUsageFlagBits)(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
            memory_property_flags,
            true,
            &context.object_index_buffer)) {
        LOG_ERROR("Error creating vertex buffer.");
        return false;
    }
    context.geometry_index_offset = 0;
    return true;
}

void Backend::upload_data_range(VkCommandPool pool, VkFence fence, VkQueue queue, vulkan_buffer* buffer, u64 offset, u64 size, void* data) {
    // Create a host-visible staging buffer to upload to. Mark it as the source of the transfer.
    VkBufferUsageFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vulkan_buffer staging;
    vulkan_buffer_create(&context, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, flags, true, &staging);
    // Load the data into the staging buffer.
    vulkan_buffer_load_data(&context, &staging, 0, size, 0, data);
    // Perform the copy from staging to the device local buffer.
    vulkan_buffer_copy_to(&context, pool, fence, queue, staging.handle, 0, buffer->handle, offset, size);
    // Clean up the staging buffer.
    vulkan_buffer_destroy(&context, &staging);
}
} // namespace Quasa::Vulkan
