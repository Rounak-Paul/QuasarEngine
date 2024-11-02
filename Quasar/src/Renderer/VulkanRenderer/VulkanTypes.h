#pragma once

#define VK_CHECK(expr)                  \
{                                      \
    assert(expr == VK_SUCCESS);        \
} 

namespace Quasar::Vulkan {

constexpr u8 FRAME_OVERLAP = 2;

struct FrameData {
	VkCommandPool command_pool;
	VkCommandBuffer main_command_buffer;
};

}