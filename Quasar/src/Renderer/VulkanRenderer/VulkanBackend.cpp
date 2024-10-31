#include "VulkanBackend.h"

namespace Quasar::Vulkan{
b8 check_validation_layer_support();

Backend::Backend(String name, u32 width, u32 height) {

    #ifdef QS_DEBUG 
        if (!check_validation_layer_support()) {
            LOG_ERROR("validation layers requested, but not available!");
        }
    #endif
}

Backend::~Backend() {

}

b8 check_validation_layer_support() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name : validation_layers) {
        bool layerFound = false;

        for (const auto& layer_properties : available_layers) {
            if (strcmp(layer_name, layer_properties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            return false;
        }
    }
    return true;
}
}