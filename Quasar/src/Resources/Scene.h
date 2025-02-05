#pragma once

#include <qspch.h>

namespace Quasar {
class Scene {
public:
    Scene();
    ~Scene() = default;
    
    b8 render(u32 width, u32 height, const vk::ClearColorValue &bg_color);
private:
    // Render target
    // The scene is rendered to an offscreen image and then resolved to this image using MSAA.
    vk::UniqueImage ResolveImage;
    vk::UniqueImageView ResolveImageView;
    vk::UniqueDeviceMemory ResolveImageMemory;

    u32 frame_index = 0;

    friend class Scenespace;

};
}