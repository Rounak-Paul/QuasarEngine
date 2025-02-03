#pragma once
#include <qspch.h>
#include <Math/Math.h>
#include <vulkan/vulkan.hpp>

#include "VulkanContext.h"

namespace Quasar::Renderer
{
    class Backend {
        public:
        Backend() {};
        ~Backend() = default;

        b8 init(String& app_name, Window* main_window);
        void shutdown();
        void resize(u32 width, u32 height);

        b8 multithreading_enabled = false;

        private:
        std::unique_ptr<VulkanContext> context;
    };
} // namespace Vulkan
