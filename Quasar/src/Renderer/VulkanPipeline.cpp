#include "VulkanPipeline.h"
#include <shaderc/shaderc.h>
#include <shaderc/status.h>
#include <Containers/File.h>

namespace Quasar
{
/**
     * Used for dynamic compilation of vulkan shaders (using the shaderc lib.)
     */
shaderc_compiler* shader_compiler;

b8 load_shader_module(const std::string& file_path, VkDevice device, VkShaderModule *outShaderModule)
{
    File f;
    if (!f.open_if_exists(file_path, File::Mode::READ, File::Type::TEXT)) {
        LOG_ERROR("Failed to open shader file!");
    }
    std::string data = f.read_all();

    shaderc_compile_options_t options = shaderc_compile_options_initialize();

    // Explicitly target Vulkan 1.2
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    // Ensure GLSL source language
    shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);

    // Optimization for performance
    shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);

    LOG_DEBUG("Compiling shader {} ...", file_path);

    shaderc_shader_kind shader_kind = shaderc_glsl_default_compute_shader;

    shader_compiler = shaderc_compiler_initialize();

    // Attempt to compile the shader.
    shaderc_compilation_result_t compilation_result = shaderc_compile_into_spv(
        shader_compiler,
        data.c_str(),
        data.length(),
        shader_kind,
        file_path.c_str(),
        "main",
        options);
    
    shaderc_compiler_release(shader_compiler);
    shader_compiler = nullptr;
    
    shaderc_compile_options_release(options);

    if (!compilation_result) {
        LOG_ERROR("An unknown error occurred while trying to compile the shader. Unable to process futher.");
        return false;
    }
    shaderc_compilation_status status = shaderc_result_get_compilation_status(compilation_result);

    // Handle errors, if any.
    if (status != shaderc_compilation_status_success) {
        const char *error_message = shaderc_result_get_error_message(compilation_result);
        u64 error_count = shaderc_result_get_num_errors(compilation_result);
        LOG_ERROR("Error compiling shader with {} errors.", error_count);
        LOG_ERROR("Error(s):\n{}", error_message);
        shaderc_result_release(compilation_result);
        return false;
    }

    LOG_DEBUG("Shader compiled successfully.");

    // Output warnings if there are any.
    u64 warning_count = shaderc_result_get_num_warnings(compilation_result);
    if (warning_count) {
        // NOTE: Not sure this it the correct way to obtain warnings.
        LOG_WARN("%llu warnings were generated during shader compilation:\n%s", warning_count, shaderc_result_get_error_message(compilation_result));
    }
    // Extract the data from the result.
    const char *bytes = shaderc_result_get_bytes(compilation_result);
    size_t result_length = shaderc_result_get_length(compilation_result);

    // Release the compilation result.
    shaderc_result_release(compilation_result);

    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // Use the resource's size and data directly.
    create_info.codeSize = result_length;
    create_info.pCode = (uint32_t*)bytes;

    VK_CHECK(vkCreateShaderModule(device, &create_info, nullptr, outShaderModule));

    return true;
}

b8 create_compute_pipeline(const ComputePipelineConfig& config, ComputePipeline& out_effect) {
    // Pipeline layout
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pSetLayouts = config.set_layouts;
    layout_info.setLayoutCount = config.set_layout_count;
    layout_info.pPushConstantRanges = config.push_constants;
    layout_info.pushConstantRangeCount = config.push_constant_count;

    if (vkCreatePipelineLayout(config.device, &layout_info, nullptr, &out_effect.layout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create compute pipeline layout for effect: {}", out_effect.name);
        return false;
    }

    // Load shader
    VkShaderModule shader_module;
    if (!load_shader_module(config.shader_path, config.device, &shader_module)) {
        LOG_ERROR("Failed to load compute shader: {}", config.shader_path);
        return false;
    }

    VkPipelineShaderStageCreateInfo stage_info{};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_info.module = shader_module;
    stage_info.pName = "main";

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.layout = out_effect.layout;
    pipeline_info.stage = stage_info;

    if (vkCreateComputePipelines(config.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &out_effect.pipeline) != VK_SUCCESS) {
        LOG_ERROR("Failed to create compute pipeline for effect: {}", out_effect.name);
        vkDestroyShaderModule(config.device, shader_module, nullptr);
        return false;
    }

    vkDestroyShaderModule(config.device, shader_module, nullptr);

    if (config.deletion_queue) {
        config.deletion_queue->push_function([device = config.device,
                                            layout = out_effect.layout,
                                            pipeline = out_effect.pipeline]() {
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, layout, nullptr);
        });
    }

    return true;
}

} // namespace Quasar
