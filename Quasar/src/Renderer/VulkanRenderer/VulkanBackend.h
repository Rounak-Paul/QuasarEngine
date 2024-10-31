#pragma once

namespace Quasar::Vulkan
{
    const std::vector<const char*> validation_layers = { 
        "VK_LAYER_KHRONOS_validation" 
        // ,"VK_LAYER_LUNARG_api_dump" // For all vulkan calls
    };

    class Backend {
        public:
        Backend() {};
        Backend(String name, u32 width, u32 height);
        ~Backend();

    };
} // namespace Quasar
