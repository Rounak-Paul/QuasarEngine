#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanCheckResult.h"
#include "VulkanCommmandBuffer.h"

b8 Quasar::VulkanBuffer::create(VulkanContext* context, VulkanBufferCreateInfo& create_info)
{
    _context = context;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = create_info.size;
    bufferInfo.usage = create_info.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(context->_device.logical_device, &bufferInfo, context->_allocator, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context->_device.logical_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context->find_memory_type(memRequirements.memoryTypeBits, create_info.properties);

    if (vkAllocateMemory(context->_device.logical_device, &allocInfo, context->_allocator, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(context->_device.logical_device, buffer, bufferMemory, 0);

    return true;
}

void Quasar::VulkanBuffer::destroy()
{
    vkDestroyBuffer(_context->_device.logical_device, buffer, _context->_allocator);
    vkFreeMemory(_context->_device.logical_device, bufferMemory, _context->_allocator);
}

b8 Quasar::VulkanBuffer::copy(VulkanBuffer *srcBuffer, VulkanBuffer *dstBuffer, VkDeviceSize size)
{
    if (srcBuffer->_context != dstBuffer->_context) {
        LOG_ERROR("source and destination buffers must be created in the same context!");
        return false;
    }

    VulkanCommandBuffer cmdBuffer;
    cmdBuffer.allocate_and_begin_single_use(srcBuffer->_context, srcBuffer->_context->_command_pool);
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;
        vkCmdCopyBuffer(cmdBuffer._handle, srcBuffer->buffer, dstBuffer->buffer, 1, &copyRegion);
    cmdBuffer.end_single_use(srcBuffer->_context, srcBuffer->_context->_command_pool, srcBuffer->_context->_device.graphics_queue);

    return true;
}

b8 Quasar::VulkanBuffer::upload_vertices(VulkanBuffer *vertex_buffer, DynamicArray<Math::Vertex> &vertices)
{
    VkDeviceSize buffer_size = sizeof(Math::Vertex) * vertices.get_size();
    VulkanBuffer staging_buffer;
    {
        VulkanBufferCreateInfo buffer_info = {
            buffer_size, // size
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // usage
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT // properties
        };
        staging_buffer.create(vertex_buffer->_context, buffer_info);
    }
    void* data;
    vkMapMemory(vertex_buffer->_context->_device.logical_device, staging_buffer.bufferMemory, 0, buffer_size, 0, &data);
        memcpy(data, vertices.get_data(), (size_t) buffer_size);
    vkUnmapMemory(vertex_buffer->_context->_device.logical_device, staging_buffer.bufferMemory);

    VulkanBuffer::copy(&staging_buffer, vertex_buffer, buffer_size);

    staging_buffer.destroy();

    return true;
}
