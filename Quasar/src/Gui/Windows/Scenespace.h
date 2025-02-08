#pragma once
#include <qspch.h>
#include "../GuiWindow.h"

namespace Quasar
{
    class Scenespace : public GuiWindow {
        public:
        Scenespace();
        virtual ~Scenespace() = default;

        virtual void init() override;
        virtual void shutdown() override;
        virtual void update(render_packet* packet) override;
        virtual void render() override;

        b8 p_open = true; // not used, disabled close of Scenespace

        private:
        ImVec2 _content_region = {800, 600};
        VkDescriptorSet descriptor_set;
        Scene* _scene;
        b8 scene_updated = false;
        };
} // namespace Quasar