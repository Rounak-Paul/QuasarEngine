#pragma once
#include <qspch.h>

namespace Quasar
{
typedef struct VulkanBufferCreateInfo {
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkMemoryPropertyFlags properties;
} VulkanBufferCreateInfo;

class VulkanBuffer {
    public:
    VulkanBuffer() = default;
    ~VulkanBuffer() = default;

    b8 create(struct VulkanContext* context, VulkanBufferCreateInfo& create_info);
    void destroy();

    static b8 copy(VulkanBuffer* srcBuffer, VulkanBuffer* dstBuffer, VkDeviceSize size);

    static b8 upload_vertices(VulkanBuffer* vertex_buffer, DynamicArray<Math::Vertex>& vertices);

    VkBuffer buffer;
    VkDeviceMemory bufferMemory;

    private:
    struct VulkanContext* _context;
};
} // namespace Quasar
