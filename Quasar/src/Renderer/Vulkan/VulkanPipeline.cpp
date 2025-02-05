#include "VulkanPipeline.h"
#include <Platform/File.h>

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

vk::UniqueShaderModule CreateShaderModule(vk::Device device, const std::vector<u8>& code) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size() * sizeof(u8);
    createInfo.pCode = (u32*)code.data();

    return device.createShaderModuleUnique(createInfo);
}

VulkanPipeline::VulkanPipeline(const vk::UniqueDevice& device, const vk::PhysicalDevice& physical_device, const vk::UniqueRenderPass& render_pass, const vk::SampleCountFlagBits& msaa_samples)
{
    #ifdef QS_PLATFORM_APPLE
    auto vertShaderCode = LoadShaderSpv("./Shaders/Builtin.World.vert.spv");
    auto fragShaderCode = LoadShaderSpv("./Shaders/Builtin.World.frag.spv");
    #else
    auto vertShaderCode = LoadShaderSpv("../Shaders/Builtin.World.vert.spv");
    auto fragShaderCode = LoadShaderSpv("../Shaders/Builtin.World.frag.spv");
    #endif

    auto vertShaderModule = CreateShaderModule(device.get(), vertShaderCode);
    auto fragShaderModule = CreateShaderModule(device.get(), fragShaderCode);

    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = {
        {{}, vk::ShaderStageFlagBits::eVertex, *vertShaderModule, "main"},
        {{}, vk::ShaderStageFlagBits::eFragment, *fragShaderModule, "main"}
    };

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info{{}, 0u, nullptr, 0u, nullptr};
    const vk::PipelineInputAssemblyStateCreateInfo input_assemply{{}, vk::PrimitiveTopology::eTriangleList, false};
    const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};
    const vk::PipelineRasterizationStateCreateInfo rasterizer{{}, false, false, vk::PolygonMode::eFill, {}, vk::FrontFace::eCounterClockwise, {}, {}, {}, {}, 1.0f};
    const vk::PipelineMultisampleStateCreateInfo multisampling{{}, msaa_samples, false};
    const vk::PipelineColorBlendAttachmentState color_blend_attachment{
        {},
        /*srcCol*/ vk::BlendFactor::eOne,
        /*dstCol*/ vk::BlendFactor::eZero,
        /*colBlend*/ vk::BlendOp::eAdd,
        /*srcAlpha*/ vk::BlendFactor::eOne,
        /*dstAlpha*/ vk::BlendFactor::eZero,
        /*alphaBlend*/ vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    const vk::PipelineColorBlendStateCreateInfo color_blending{{}, false, vk::LogicOp::eCopy, 1, &color_blend_attachment};
    const std::array dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state_info{{}, dynamic_states};

    auto pipeline_layout = device->createPipelineLayoutUnique({}, nullptr);
    const vk::GraphicsPipelineCreateInfo pipeline_info{
        {},
        shader_stages,
        &vertex_input_info,
        &input_assemply,
        nullptr,
        &viewport_state,
        &rasterizer,
        &multisampling,
        nullptr,
        &color_blending,
        &dynamic_state_info,
        *pipeline_layout,
        *render_pass,
    };
    _graphics_pipeline = device->createGraphicsPipelineUnique({}, pipeline_info).value;
}

} // namespace Quasar
