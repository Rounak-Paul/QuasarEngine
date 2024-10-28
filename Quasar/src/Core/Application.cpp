#include "Application.h"

namespace Quasar {
    Application* Application::instance = nullptr;

    Application::Application(app_create_info info) : engine_name{info.app_name}, window{info.width, info.height, info.app_name.c_str()} {
        assert(!instance);
        instance = this;
        std::cout << "Starting Quasar Engine..." << std::endl;
    }

    Application::~Application() {

    }

    void Application::run() {
        std::cout << "running..." << std::endl;
        while (!window.should_close() && running)
        {
            if (suspended) { 
                window.wait_events();
                continue; 
            }

            window.poll_events(); 
        }
        
    }
}