#pragma once
#include <qspch.h>
#include "../GuiWindow.h"

namespace Quasar
{
    class ImageGUI : public GuiWindow {
        public:
        ImageGUI();
        virtual ~ImageGUI() = default;

        virtual void init() override;
        virtual void shutdown() override;
        virtual void update(render_packet* packet) override;

        b8 p_open = true; // not used, disabled close of Scenespace
        private:
        VkDescriptorSet descriptor_set;
        Scene image_scene;
        };
} // namespace Quasar