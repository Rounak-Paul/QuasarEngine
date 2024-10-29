#include "Application.h"

namespace Quasar {
    Application* Application::instance = nullptr;

    Application::Application(app_create_info info) : engine_name{info.app_name}, window{info.width, info.height, info.app_name.c_str()} {
        assert(!instance);
        instance = this;

        Log::init();
        LOG_INFO("Booting Quasar Engine...");
        VkExtent2D extent = window.get_extent();
        LOG_INFO("Created main windoW [%u, %u]", extent.width, extent.height)
    }

    Application::~Application() {

    }

    void Application::run() {
        LOG_INFO("Running...");
        while (!window.should_close() && running)
        {
            if (suspended) { 
                window.wait_events();
                continue; 
            }

            window.poll_events(); 
            // VkExtent2D extent = window.get_extent();
            // LOG_INFO("Created main windoW [%u, %u]", extent.width, extent.height)
        }
        
    }
}