#include "VulkanBackend.h"

#include <Math/Math.h>

namespace Quasar::Renderer
{
    std::vector<const char*> get_required_extensions();

    b8 Backend::init(String &app_name, Window *main_window)
    {
        auto extensions = get_required_extensions();
        context = std::make_unique<VulkanContext>(extensions);
        return true;
    }

    void Backend::shutdown()
    {
    }

    void Backend::resize(u32 width, u32 height)
    {
    }

    std::vector<const char*> get_required_extensions() {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> requiredExtensions;
        for(uint32_t i = 0; i < glfwExtensionCount; i++) {
            requiredExtensions.emplace_back(glfwExtensions[i]);
        }
    #ifdef QS_PLATFORM_APPLE
        requiredExtensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    #endif
    #ifdef QS_DEBUG 
            requiredExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    #endif
        return requiredExtensions;
    }

} // namespace Quasa::Vulkan
