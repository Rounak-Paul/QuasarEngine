#pragma once

#include <qspch.h>
#include <Renderer/RenderTarget.h>

namespace Quasar {
class Scene {
public:
    Scene();
    ~Scene() = default;
    
    b8 create();
    b8 update(u32 width, u32 height, const VkClearColorValue &bg_color, u8 frame_index);
    void destroy();
private:
    // Render target

    // The scene is rendered to an offscreen image and then resolved to this image using MSAA.

    RenderTarget _render_target;

    friend class Scenespace;
    friend class ImageGUI;

};
}