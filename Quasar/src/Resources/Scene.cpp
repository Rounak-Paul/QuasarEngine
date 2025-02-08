#include "Scene.h"

namespace Quasar {
Scene::Scene()
{

}

b8 Scene::create()
{
    _render_target.create();
    return true;
}

b8 Scene::update(u32 width, u32 height, const VkClearColorValue &bg_color)
{
    return _render_target.render({width, height}, bg_color);
}
}