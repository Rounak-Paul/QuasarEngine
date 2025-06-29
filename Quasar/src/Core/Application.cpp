#include "Application.h"

namespace Quasar
{
Application::Application(app_create_info info) {
    renderer.init();
}

Application::~Application() {
    renderer.shutdown();
}

void Application::run() {
    while (true) {
        if (renderer.begin_frame()) {
            renderer.end_frame();
        }
    }
}
} // namespace Quasar
