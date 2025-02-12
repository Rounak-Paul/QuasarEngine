#include "VulkanPipeline.h"
#include <Platform/File.h>
#include <Math/Math.h>

#include "VulkanBuffer.h"

namespace Quasar
{
std::vector<u8> LoadShaderSpv(const std::string& filename) {
    File f;
    if (!f.open(filename, File::Mode::READ, File::Type::BINARY)) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    size_t fileSize = f.get_size();
    std::vector<u8> buffer(fileSize / sizeof(u8));
    f.read_all_binary(buffer.data());
    f.close();

    return buffer;
}

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint8_t>& code) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

b8 VulkanPipeline::create(const VulkanContext *context, const VulkanPipelineConfig &config)
{
    _context = context;

    #ifdef QS_PLATFORM_APPLE
    auto vertShaderCode = LoadShaderSpv("./Shaders/Builtin.World.vert.spv");
    auto fragShaderCode = LoadShaderSpv("./Shaders/Builtin.World.frag.spv");
    #else
    auto vertShaderCode = LoadShaderSpv("../Shaders/Builtin.World.vert.spv");
    auto fragShaderCode = LoadShaderSpv("../Shaders/Builtin.World.frag.spv");
    #endif

    auto vertShaderModule = CreateShaderModule(_context->_device.logical_device, vertShaderCode);
    auto fragShaderModule = CreateShaderModule(_context->_device.logical_device, fragShaderCode);

    // Shader stage creation info
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};

    // Vertex Shader Stage
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vertShaderModule;
    shader_stages[0].pName = "main";

    // Fragment Shader Stage
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = fragShaderModule;
    shader_stages[1].pName = "main";

    // Vertex Input State
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Math::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Math::Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Math::Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertex_input_info.pVertexBindingDescriptions = &bindingDescription;
    vertex_input_info.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input Assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = config.topology;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Viewport State
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = nullptr;  // Dynamic state
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = nullptr;  // Dynamic state

    // Rasterization State
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = config.msaaSamples;
    multisampling.sampleShadingEnable = VK_FALSE;

    // Color Blend Attachment
    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.blendEnable = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = config.srcColorBlendFactor;
    color_blend_attachment.dstColorBlendFactor = config.dstColorBlendFactor;
    color_blend_attachment.colorBlendOp = config.colorBlendOp;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // Color Blending
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    // Dynamic State
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state_info = {};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = 2;
    dynamic_state_info.pDynamicStates = dynamic_states;

    descriptor_set_layout_create(_context->_device.logical_device);

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &_descriptor_set_layout;

    if (vkCreatePipelineLayout(_context->_device.logical_device, &pipeline_layout_info, nullptr, &_pipeline_layout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create pipeline layout!");
        return false;
    }

    // Graphics Pipeline
    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = _pipeline_layout;
    pipeline_info.renderPass = _context->_render_pass;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(_context->_device.logical_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &_pipeline) != VK_SUCCESS) {
        LOG_ERROR("Failed to create graphics pipeline!");
        return false;
    }

    if (vertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(_context->_device.logical_device, vertShaderModule, nullptr);
    }

    if (fragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(_context->_device.logical_device, fragShaderModule, nullptr);
        fragShaderModule = VK_NULL_HANDLE;
    }

    {
        // TODO: temp
        auto context = QS_RENDERER.get_vkcontext();
        VkDeviceSize bufferSize = sizeof(Math::UniformBufferObject);

        _uniform_buffers.resize(MAX_FRAMES_IN_FLIGHT);
        _uniform_buffers_mapped.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VulkanBufferCreateInfo buffer_info = {
                bufferSize, // size
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, // usage
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT // properties
            };
            _uniform_buffers[i].create(context, buffer_info);
            vkMapMemory(context->_device.logical_device, _uniform_buffers[i]._buffer_memory, 0, bufferSize, 0, &_uniform_buffers_mapped[i]);
        }
    }

    createDescriptorPool(_context->_device.logical_device);
    createDescriptorSets(_context->_device.logical_device);

    return true;
}

void VulkanPipeline::destroy()
{
    vkDestroyDescriptorPool(_context->_device.logical_device, _descriptor_pool, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        _uniform_buffers[i].destroy();
    }

    if (_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(_context->_device.logical_device, _pipeline, nullptr);
        _pipeline = VK_NULL_HANDLE;
    }

    if (_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(_context->_device.logical_device, _pipeline_layout, nullptr);
        _pipeline_layout = VK_NULL_HANDLE;
    }

    if (_descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(_context->_device.logical_device, _descriptor_set_layout, nullptr);
        _descriptor_set_layout = VK_NULL_HANDLE;
    }
}

void VulkanPipeline::descriptor_set_layout_create(VkDevice device)
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &_descriptor_set_layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void VulkanPipeline::createDescriptorPool(VkDevice device)
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &_descriptor_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void VulkanPipeline::createDescriptorSets(VkDevice device)
{
    // TODO: temp
    auto context = QS_RENDERER.get_vkcontext();

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, _descriptor_set_layout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptor_pool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    _descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, _descriptor_sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = _uniform_buffers[i]._buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(Math::UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = _descriptor_sets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }
}

} // namespace Quasar
