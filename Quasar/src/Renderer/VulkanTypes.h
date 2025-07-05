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

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct Vertex {
	glm::vec3 position;
	f32 uv_x;
	glm::vec3 normal;
	f32 uv_y;
	glm::vec4 color;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {

    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
};
}