#pragma once
#include <qspch.h>
#include "../GuiWindow.h"

namespace Quasar
{
    class Dockspace : public GuiWindow {
        public:
        Dockspace();
        virtual ~Dockspace() = default;

        virtual void init() override;
        virtual void shutdown() override;
        virtual void update(render_packet* packet) override;
        virtual void render() override;

        b8 p_open = true;
        private:
        
        };
} // namespace Quasar