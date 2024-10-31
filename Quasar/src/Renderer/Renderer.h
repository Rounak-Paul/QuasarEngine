#pragma once

#include <Core/System.h>
#include "VulkanRenderer/VulkanBackend.h"

namespace Quasar {

typedef struct renderer_system_config {
    String application_name;
    Window* window;
} renderer_system_config;

class Renderer : public System {
    public:
    Renderer() {};
    ~Renderer() = default;
    virtual b8 init(void* config) override;
    virtual void shutdown() override;
    b8 is_multithreaded() {return backend.multithreading_enabled;}

    private:
    Vulkan::Backend backend;
};
}