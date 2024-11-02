#pragma once
#include <vk_mem_alloc.h>

#define VK_CHECK(expr)                  \
{                                      \
    assert(expr == VK_SUCCESS);        \
} 

namespace Quasar::Vulkan {

constexpr u8 FRAME_OVERLAP = 2;

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

struct FrameData {
	VkCommandPool command_pool;
	VkCommandBuffer main_command_buffer;
    VkSemaphore swapchain_semaphore, render_semaphore;
	VkFence render_fence;
	DeletionQueue deletion_queue;
};

struct AllocatedImage {
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    VkExtent3D extent;
    VkFormat format;
};

}