#pragma once

#include <Core/System.h>
#include "Vulkan/VulkanBackend.h"
#include <Math/Math.h>
#include <Resources/Scene.h>

namespace Quasar {

typedef struct renderer_system_config {
    String application_name;
    Window* window;
} renderer_system_config;

typedef struct render_packet {
    f32 dt;
    b8 app_suspended;
    Scene* scene;
} render_packet;

class RendererAPI : public System {
    public:
    RendererAPI() {};
    ~RendererAPI() = default;

    virtual b8 init(void* config) override;
    virtual void shutdown() override;

    QS_INLINE b8 is_multithreaded() {return backend.multithreading_enabled;}

    b8 draw(render_packet* packet);
    void resize(u32 width, u32 height);

    VulkanContext* get_vkcontext() {return &backend.context;}

    private:
    Backend backend;
};
}