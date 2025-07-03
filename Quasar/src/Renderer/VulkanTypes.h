#pragma once

#include <qspch.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#define VK_CHECK(x)                                                           \
    do {                                                                     \
        VkResult err__ = (x);                                                \
        if (err__ != VK_SUCCESS) {                                           \
            LOG_ERROR("Detected Vulkan error: {}", string_VkResult(err__));  \
            abort();                                                         \
        }                                                                    \
    } while (0)

namespace Quasar {

constexpr unsigned int FRAME_OVERLAP = 3;

struct FrameData {
	VkCommandPool command_pool;
	VkCommandBuffer main_command_buffer;

    VkSemaphore swapchain_semaphore, render_semaphore;
	VkFence render_fence;

    DeletionQueue deletion_queue;
};

struct ComputePushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputePipeline {
    std::string name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
};
}