#include "Application.h"

namespace Quasar {
    Application* Application::instance = nullptr;

    Application::Application(engine_state state) : state{state} {
        assert(!instance);
        instance = this;
        std::cout << "Starting Quasar Engine..." << std::endl;
    }

    Application::~Application() {

    }

    void Application::run() {
        std::cout << "running..." << std::endl;
        while (true)
        {
            
        }
        
    }
}