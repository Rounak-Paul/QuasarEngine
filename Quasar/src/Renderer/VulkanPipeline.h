#pragma once

#include <qspch.h>
#include "VulkanTypes.h"

namespace Quasar
{
struct ComputePipelineConfig {
    VkDevice device;
    const char* shader_path;

    VkDescriptorSetLayout* set_layouts;
    u32 set_layout_count;

    VkPushConstantRange* push_constants = nullptr;
    u32 push_constant_count = 0;

    DeletionQueue* deletion_queue = nullptr;
};

enum ShaderStage {
    SHADER_STAGE_UNDEFINED,
    SHADER_STAGE_COMPUTE,
    SHADER_STAGE_VERTEX,
    SHADER_STAGE_FRAGMENT
};

b8 load_shader_module(const std::string& file_path, VkDevice device, ShaderStage stage, VkShaderModule *outShaderModule);
b8 create_compute_pipeline(const ComputePipelineConfig& config, ComputePipeline& out_effect);

class PipelineBuilder {
//> pipeline
public:
    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;

    VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
    VkPipelineRasterizationStateCreateInfo _rasterizer;
    VkPipelineColorBlendAttachmentState _colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo _multisampling;
    VkPipelineLayout _pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo _depthStencil;
    VkPipelineRenderingCreateInfo _renderInfo;
    VkFormat _colorAttachmentformat;

	PipelineBuilder(){ clear(); }

    void clear();

    VkPipeline build_pipeline(VkDevice device);
//< pipeline
    void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
    void set_input_topology(VkPrimitiveTopology topology);
    void set_polygon_mode(VkPolygonMode mode);
    void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace);
    void set_multisampling_none();
    void disable_blending();
    void enable_blending_additive();
    void enable_blending_alphablend();

    void set_color_attachment_format(VkFormat format);
	void set_depth_format(VkFormat format);
	void disable_depthtest();
    void enable_depthtest(bool depthWriteEnable,VkCompareOp op);
};
} // namespace Quasar
