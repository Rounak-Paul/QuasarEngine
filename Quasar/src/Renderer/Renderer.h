#pragma once

#include <qspch.h>
#include "VulkanTypes.h"
#include <Core/Window.h>
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanImage.h"
#include "VulkanDescriptor.h"

#include "VulkanLoader.h"

namespace Quasar
{
    struct FrameData {
        VkCommandPool command_pool;
        VkCommandBuffer main_command_buffer;

        VkSemaphore swapchain_semaphore, render_semaphore;
        VkFence render_fence;

        DeletionQueue deletion_queue;
        DescriptorAllocatorGrowable _frameDescriptors;
    };

    class Renderer {
        public:
        Renderer() = default;
        ~Renderer() = default;

        Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

        b8 init(const std::string& name, const Window& window);

        b8 begin_frame();
        void draw_background();
        void draw_geometry();
        void end_frame();
        void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

        void shutdown();

        FrameData& get_current_frame() { return _frames[_frame_number % FRAME_OVERLAP]; };

        AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        void destroy_buffer(const AllocatedBuffer& buffer);

        GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

        VulkanImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
        VulkanImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
        void destroy_image(const VulkanImage& img);

        private:
        const Window* _window;
        u32 _frame_number {0};
        FrameData _frames[FRAME_OVERLAP];
        b8 resize_requested = false;
        DeletionQueue _main_deletion_queue;

        u32 _api_major; // The instance-level api major version.
        u32 _api_minor; // The instance-level api minor version.
        u32 _api_patch; // The instance-level api patch version.
        b8 _validation_enabled = true;
        VkInstance _instance;
        VmaAllocator _allocator;
        VkDebugUtilsMessengerEXT _debug_messenger;
        VulkanDevice _device;
        VkSurfaceKHR _surface;
        VulkanSwapchain _swapchain;
        DescriptorAllocator global_descriptor_allocator;
        VkDescriptorSet _draw_image_descriptors;
        VkDescriptorSetLayout _draw_image_descriptor_layout;

        //draw resources
        VulkanImage _draw_image;
        VulkanImage _depthImage;
        VkExtent2D _draw_extent;
        f32 renderScale = 1.f;
        ImTextureID _draw_texture;

        // immediate submit structures
        VkFence _imm_fence;
        VkCommandBuffer _imm_command_buffer;
        VkCommandPool _imm_command_pool;
        VkSampler _imm_sampler;

        // Background pipelines
        std::vector<ComputePipeline> backgroundEffects;
        i32 currentBackgroundEffect{0};

        VkPipelineLayout _meshPipelineLayout;
        VkPipeline _meshPipeline;

        VulkanImage _whiteImage;
        VulkanImage _blackImage;
        VulkanImage _greyImage;
        VulkanImage _errorCheckerboardImage;

        VkSampler _defaultSamplerLinear;
        VkSampler _defaultSamplerNearest;

        std::vector<std::shared_ptr<MeshAsset>> testMeshes;

        GPUSceneData sceneData;

        VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
        VkDescriptorSetLayout _singleImageDescriptorLayout;

        b8 initialize_validation_layers();
        void fetch_api_version();
        b8 create_instance(const std::string& name);
        void setup_debug_messenger();
        b8 create_surface(const Window& window);
        b8 create_device();
        b8 create_swapchain(const Window& window);
        b8 create_allocator();
        b8 create_draw_image(const Window& window);
        b8 create_command_buffers();
        b8 create_sync_objects();
        void create_descriptors();
        void create_pipelines();
        void create_background_pipelines();
        void create_mesh_pipeline();
        void create_default_data();

        void init_imgui(const Window& window);
        void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
    };
} // namespace Quasar::Renderer
