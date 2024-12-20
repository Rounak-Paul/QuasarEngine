#pragma once

#include <Core/System.h>
#include "Vulkan/VulkanBackend.h"

namespace Quasar {

typedef struct renderer_system_config {
    String application_name;
    Window* window;
} renderer_system_config;

class RendererAPI : public System {
    public:
    RendererAPI() {};
    ~RendererAPI() = default;
    virtual b8 init(void* config) override;
    virtual void shutdown() override;
    b8 is_multithreaded() {return backend.multithreading_enabled;}
    void draw();
    void resize(u32 width, u32 height);

    private:
    Renderer::Backend backend;
};
}