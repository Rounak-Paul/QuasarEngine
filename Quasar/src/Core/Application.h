#pragma once
#include <qspch.h>

#include <Core/Window.h>
#include <Renderer/Renderer.h>

namespace Quasar
{
    typedef struct QS_API app_create_info {
        std::string app_name;
        u32 width;
        u32 height;
    } app_create_info ;

    class QS_API Application
    {
    public:
        Application(app_create_info info);
        ~Application();

        Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

        void run();
    
    private:
        static Application* _instance;
        b8 _running = true;
        b8 _suspended = false;

        Window _window;
        Renderer _renderer;
    };

    Application* CreateApplication();
} // namespace Quasar