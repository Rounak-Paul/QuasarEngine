#include "VulkanShaderUtils.h"

#include "Platform/File.h"

namespace Quasar::Renderer {

b8 create_shader_module(
    vulkan_context* context,
    const String& name,
    const String& type_str,
    VkShaderStageFlagBits shader_stage_flag,
    u32 stage_index,
    vulkan_shader_stage* shader_stages) {
    // Build file name.
    String file_name = "../Shaders/" + name + "." + type_str + ".spv";
    memset(&shader_stages[stage_index].create_info, 0, sizeof(VkShaderModuleCreateInfo));
    shader_stages[stage_index].create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // Obtain file handle.
    File handle;
    if (!handle.open(file_name, File::Mode::READ, File::Type::BINARY)) {
        LOG_ERROR("Unable to read shader module: %s.", file_name.c_str());
        return false;
    }
    // Read the entire file as binary.
    u64 file_size = handle.get_size();
    u8* file_buffer = (u8*)QSMEM.allocate(file_size);
    if (!handle.read_all_binary(file_buffer)) {
        LOG_ERROR("Unable to binary read shader module: %s.", file_name.c_str());
        return false;
    }
    shader_stages[stage_index].create_info.codeSize = file_size;
    shader_stages[stage_index].create_info.pCode = (u32*)file_buffer;
    // Close the file.
    handle.close();
    VK_CHECK(vkCreateShaderModule(
        context->device.logical_device,
        &shader_stages[stage_index].create_info,
        context->allocator,
        &shader_stages[stage_index].handle));
    // Shader stage info
    memset(&shader_stages[stage_index].shader_stage_create_info, 0, sizeof(VkPipelineShaderStageCreateInfo));
    shader_stages[stage_index].shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[stage_index].shader_stage_create_info.stage = shader_stage_flag;
    shader_stages[stage_index].shader_stage_create_info.module = shader_stages[stage_index].handle;
    shader_stages[stage_index].shader_stage_create_info.pName = "main";
    if (file_buffer) {
        QSMEM.free(file_buffer);
        file_buffer = 0;
    }
    return true;
}

}