#pragma once

#include <qspch.h>
#include <Renderer/RenderTarget.h>

namespace Quasar {
class Scene {
public:
    Scene();
    ~Scene() = default;
    
    b8 create();
    b8 update(u32 width, u32 height, const VkClearColorValue &bg_color);
    void destroy();
private:
    // Render target
    // The scene is rendered to an offscreen image and then resolved to this image using MSAA.
    RenderTarget _render_target;

    u32 frame_index = 0;

    friend class Scenespace;

};
}