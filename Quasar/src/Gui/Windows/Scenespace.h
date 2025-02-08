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

        b8 p_open = true; // not used, disabled close of Scenespace
        private:
        
        };
} // namespace Quasar