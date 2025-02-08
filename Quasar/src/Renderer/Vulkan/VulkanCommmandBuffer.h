#pragma once

#include <qspch.h>

namespace Quasar
{
typedef enum CommandBufferState {
    COMMAND_BUFFER_STATE_READY,
    COMMAND_BUFFER_STATE_RECORDING,
    COMMAND_BUFFER_STATE_IN_RENDER_PASS,
    COMMAND_BUFFER_STATE_RECORDING_ENDED,
    COMMAND_BUFFER_STATE_SUBMITTED,
    COMMAND_BUFFER_STATE_NOT_ALLOCATED
} CommandBufferState;

class VulkanCommandBuffer {
    public:
    VulkanCommandBuffer() {};
    ~VulkanCommandBuffer() = default;

    void allocate(
        const struct VulkanContext* context,
        VkCommandPool pool,
        b8 is_primary);

    void free(
        const struct VulkanContext* context,
        VkCommandPool pool);

    void begin(
        b8 is_single_use,
        b8 is_renderpass_continue,
        b8 is_simultaneous_use);

    void end();

    void update_submitted();

    void reset();

    /**
     * Allocates and begins recording to out_command_buffer.
     */
    void allocate_and_begin_single_use(
        const struct VulkanContext* context,
        VkCommandPool pool);

    /**
     * Ends recording, submits to and waits for queue operation and frees the provided command buffer.
     */
    void end_single_use(
        const struct VulkanContext* context,
        VkCommandPool pool,
        VkQueue queue);

    VkCommandBuffer handle;
    // Command buffer state.
    CommandBufferState state;
};
}