#include "Application.h"

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>

#include <utils/EntityManager.h>
#include <utils/Entity.h>

#include <GLFW/glfw3.h>

using namespace filament;
using namespace utils;

namespace Quasar
{
Application::Application(app_create_info info) {
}

Application::~Application() {

}

void Application::run() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Triangle", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    Engine* engine = Engine::create();
    SwapChain* swapChain = engine->createSwapChain(window);
    Renderer* renderer = engine->createRenderer();

    Camera* camera = engine->createCamera(EntityManager::get().create());
    View* view = engine->createView();
    Scene* scene = engine->createScene();

    view->setCamera(camera);
    view->setScene(scene);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }

    Engine::destroy(&engine);
    glfwTerminate();
    return;

}
} // namespace Quasar
