#pragma once

#include <qspch.h>
#include "RendererTypes.h"

namespace Quasar
{
    class Renderer {
        public:
        Renderer() = default;
        ~Renderer() = default;

        b8 init();

        b8 begin_frame();
        void end_frame();

        void shutdown();
    };
} // namespace Quasar::Renderer
