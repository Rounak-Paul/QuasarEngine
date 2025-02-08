#include "VulkanCommmandBuffer.h"
#include "VulkanContext.h"
#include "VulkanCheckResult.h"
#include "VulkanCommmandBuffer.h"

namespace Quasar
{
void VulkanCommandBuffer::allocate(
    const VulkanContext* context,
    VkCommandPool pool,
    b8 is_primary)
{
    VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = pool;
    alloc_info.level = is_primary ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    alloc_info.commandBufferCount = 1;
    alloc_info.pNext = nullptr;

    state = COMMAND_BUFFER_STATE_NOT_ALLOCATED;
    VK_CALL(vkAllocateCommandBuffers(context->_device.logical_device, &alloc_info, &handle));
    state = COMMAND_BUFFER_STATE_READY;
}

void VulkanCommandBuffer::free(const VulkanContext *context, VkCommandPool pool)
{
    vkFreeCommandBuffers(
        context->_device.logical_device,
        pool,
        1,
        &handle);

    handle = 0;
    state = COMMAND_BUFFER_STATE_NOT_ALLOCATED;
}
void VulkanCommandBuffer::begin(b8 is_single_use, b8 is_renderpass_continue, b8 is_simultaneous_use)
{
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = 0;
    if (is_single_use) {
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    if (is_renderpass_continue) {
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    }
    if (is_simultaneous_use) {
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    }

    VK_CALL(vkBeginCommandBuffer(handle, &begin_info));
    state = COMMAND_BUFFER_STATE_RECORDING;
}
void VulkanCommandBuffer::end()
{
    VK_CALL(vkEndCommandBuffer(handle));
    state = COMMAND_BUFFER_STATE_RECORDING_ENDED;
}
void VulkanCommandBuffer::update_submitted()
{
    state = COMMAND_BUFFER_STATE_SUBMITTED;
}
void VulkanCommandBuffer::reset()
{
    // VK_CALL(vkResetCommandBuffer(handle, 0));
    state = COMMAND_BUFFER_STATE_READY;
}
void VulkanCommandBuffer::allocate_and_begin_single_use(const VulkanContext *context, VkCommandPool pool)
{
    allocate(context, pool, true);
    begin(true, false, false);
}
void VulkanCommandBuffer::end_single_use(const VulkanContext *context, VkCommandPool pool, VkQueue queue)
{
    end();
    // Submit the queue
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &handle;
    VK_CALL(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CALL(vkQueueWaitIdle(queue));
    free(context, pool);
}
}