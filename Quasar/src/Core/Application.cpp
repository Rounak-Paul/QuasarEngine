#include "Application.h"

namespace Quasar
{
Application::Application(app_create_info info) {
    create_info = info;
}

b8 Application::init() {
    if (!_window.create(create_info.width, create_info.height, create_info.app_name.c_str())) {

    }
    _renderer.init(create_info.app_name, _window);
}

void Application::shutdown() {
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
            
            _renderer.draw();

            _renderer.end_frame();
        }
    }
}
} // namespace Quasar
