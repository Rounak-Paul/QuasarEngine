#include "Application.h"

namespace Quasar
{
Application::Application(app_create_info info) {
    if (!_window.create(800, 600, "Quasar Engine")) {

    }
    _renderer.init();
}

Application::~Application() {
    _renderer.shutdown();
    _window.destroy();
}

void Application::run() {
    while (!_window.should_close() && _running) {
        if (_suspended) { 
            _window.wait_events();
            continue; 
        }
        _window.poll_events();
        
        if (_renderer.begin_frame()) {
            _renderer.end_frame();
        }
    }
}
} // namespace Quasar
