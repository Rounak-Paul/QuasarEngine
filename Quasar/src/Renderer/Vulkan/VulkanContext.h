#pragma once

#include <qspch.h>

namespace Quasar::Renderer {

struct VulkanContext {
    VulkanContext(std::vector<const char *> extensions);
    ~VulkanContext() = default; // Using unique handles, so no need to manually destroy anything.

    vk::UniqueInstance _instance;
    vk::PhysicalDevice _physical_device;
    vk::UniqueDevice _device;
    u32 _queue_family = (u32)-1;
    vk::Queue _queue;
    vk::UniquePipelineCache _pipeline_cache;
    vk::UniqueDescriptorPool _descriptor_pool;

    vk::SampleCountFlagBits _msaa_samples;
    vk::UniqueRenderPass _render_pass;
    vk::UniquePipeline _graphics_pipeline;
    vk::UniqueCommandPool _command_pool;
    std::vector<vk::UniqueCommandBuffer> _command_buffers;
    vk::UniqueSampler _texture_sampler;
    vk::Extent2D _extent;

    // The scene is rendered to an offscreen image and then resolved to this image using MSAA.
    vk::UniqueImage ResolveImage;
    vk::UniqueImageView ResolveImageView;
    vk::UniqueDeviceMemory ResolveImageMemory;

    // Find a discrete GPU, or the first available (integrated) GPU.
    vk::PhysicalDevice find_physical_device() const;
    u32 find_memory_type(u32 type_filter, vk::MemoryPropertyFlags) const;

    b8 CreateGraphicsPipeline();
};

}