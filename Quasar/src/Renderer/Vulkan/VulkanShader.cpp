#include "VulkanShader.h"
#include "Platform/File.h"

namespace Quasar::Renderer
{
    VkShaderModule vulkan_shader_module_create(VulkanContext* context, String shader_file)
    {
        File f;
        f.open(shader_file, File::Mode::READ, File::Type::BINARY);
        u32 file_size = f.get_size();
        u8* frag_shader_code = (u8*)QSMEM.allocate(file_size);
        f.read_all_binary(frag_shader_code);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = file_size;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(frag_shader_code);

        VkShaderModule shader_module;
        if (vkCreateShaderModule(context->device.logical_device, &createInfo, context->allocator, &shader_module) != VK_SUCCESS) {
            LOG_ERROR("failed to create shader module!");
        }
        return shader_module;
    }

    void vulkan_shader_module_destroy(VulkanContext *context, VkShaderModule module)
    {
        vkDestroyShaderModule(context->device.logical_device, module, context->allocator);
    }

} // namespace Quasar::Renderer
